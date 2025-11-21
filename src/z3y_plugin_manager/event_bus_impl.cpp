/*
* Copyright [2025] [Yue Liu]
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
* http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*/

/**
 * @file event_bus_impl.cpp
 * @brief z3y::PluginManager 类的 IEventBus 接口实现。
 * @author Yue Liu
 * @date 2025-07-13
 * @copyright Copyright (c) 2025 Yue Liu
 *
 * @details
 * [设计思想：IEventBus 实现]
 * 此文件是 `PluginManager` 中关于事件总线的所有功能的实现。
 * 它包括：
 * 1. `Subscribe...Impl` / `Fire...Impl`: `IEventBus` 纯虚函数的实现。
 * 2. `EventLoop`: 异步事件 (`kQueued`) 的工作线程函数。
 * 3. `CleanupExpiredSubscriptions`:(Lazy GC) 垃圾回收机制，用于清理已销毁的 `weak_ptr` 订阅。
 *
 * [线程安全模型 (EventBus)]
 * 1. `event_mutex_` (读写锁):
 * - `Fire...`: [高频] 获取 **写锁** (`std::unique_lock`)。
 * (注：必须是 *写锁* 而不是读锁，
 * 因为 `CleanupExpiredSubscriptions` (Lazy GC)
 * 需要在遍历前 *修改* 订阅列表)。
 * 复制回调后 **立即释放写锁**。
 * - `Subscribe...` / `Unsubscribe...`: [低频] 获取 **写锁**。 修改订阅列表 (maps)。 释放写锁。
 *
 * 2. [无锁执行]
 * - `Fire...` 在释放读锁 *之后*， 开始遍历 *局部的 vector* 并执行回调。
 * - (关键) 这意味着， 即使一个 `kDirect` 回调函数 *内部* 又调用了 `Subscribe...` (需要写锁)，
 * 也不会发生死锁， 因为 `Fire...` 此时已不持有任何锁。
 *
 * 3. `EventLoop` (异步)
 * - `queue_mutex_`: 保护 `event_queue_` 和 `running_`。
 * - `Fire...` (kQueued 模式) 获取 `queue_mutex_` 的锁， 将任务 (lambda) `push` 到队列， `notify_one()`， 然后释放锁。
 * - `EventLoop`: 等待 `queue_cv_`。 被唤醒后， 获取 `queue_mutex_` 的锁， `pop` 一个任务， 释放锁。 (无锁) 执行任务。
 *
 * [垃圾回收 (Lazy GC)]
 * - `weak_ptr` (订阅者) 过期后不会自动从 `map` 中移除。
 * `CleanupExpiredSubscriptions` 是一个辅助函数。
 * - 它在 *下一次* `Fire...`某个特定事件时被调用 (在写锁下)，
 * 检查该事件的 `vector<Subscription>`， 移除所有 `expired()` 的 `weak_ptr`。
 * - 这种“懒惰”GC 的方式避免了在 `Unsubscribe` 时进行昂贵的全局搜索。
 */

#include "plugin_manager_pimpl.h"  // Pimpl 私有头文件
#include <iostream>    // 用于 std::cerr (ReportException)
#include <shared_mutex>  // C++17
#include "framework/connection.h"  // 依赖 Connection (作为返回值)

namespace z3y {

    // 声明在 plugin_manager.cpp 中定义的外部辅助函数
    extern void ReportException(PluginManagerPimpl* pimpl, const std::exception& e);
    extern void ReportUnknownException(PluginManagerPimpl* pimpl);



    // --- [内部] 订阅垃圾回收 (Lazy GC) ---

    /**
    * @brief [内部] (Lazy GC) 清理 vector 中已失效的订阅者。
    * @details
    *
    * [锁]
    * 此函数必须在 `event_mutex_` 的 **写锁** 保护下调用。
    *
    * @param subs 要清理的 `vector<Subscription>` (通过引用传递)。
    * @param gc_queue [内部] GC 队列，用于稍后清理反向查找表。
    */
    static void CleanupExpiredSubscriptions(
        std::vector<PluginManagerPimpl::Subscription>& subs,
        std::queue<std::weak_ptr<void>>& gc_queue) {
        // C++ Erase-Remove Idiom
        subs.erase(
            std::remove_if(
                subs.begin(), subs.end(),
                [&gc_queue](const PluginManagerPimpl::Subscription& s) {
                    // 1. 检查订阅者 (subscriber)是否已销毁
                    bool subscriber_expired = s.subscriber_id.expired();
                    // 2. 检查发布者 (sender)是否已销毁
                    // (s.sender_id.owner_before(...)是一个检查 weak_ptr 是否“非空”的技巧)
                    bool sender_expired =
                        !s.sender_id.owner_before(std::weak_ptr<void>()) &&
                        s.sender_id.expired();

                    // 3. 如果任一失效，则标记为待移除
                    if (subscriber_expired || sender_expired) {
                        gc_queue.push(s.subscriber_id);  // 放入 GC
                        // 队列
                        return true;                     // true = 标记为移除
                    }
                    return false;  // false = 保留
                }),
            subs.end());
    }

    /**
     * @brief [内部] (Lazy GC) 清理 `SenderMap` 中已失效的顶级键 (发布者)。
     * @details
     *
     * [锁]
     * 此函数必须在 `event_mutex_` 的 **写锁** 保护下调用。
     *
     * @param sender_map 要清理的 `SenderMap` (通过引用传递)。
     * @param gc_queue [内部] GC 队列。
     */
    static void CleanupExpiredSenderMap(
        PluginManagerPimpl::SenderMap& sender_map,
        std::queue<std::weak_ptr<void>>& gc_queue) {
        // (C++17 之前，遍历 map 并删除的标准方式)
        for (auto sender_it = sender_map.begin(); sender_it != sender_map.end();) {
            // 1. 检查 map 的 key (sender) 是否已销毁
            if (sender_it->first.expired()) {
                // 2. (GC) 将此 sender 下的所有订阅者都加入 GC
                for (auto& event_it : sender_it->second) {
                    for (const auto& sub : event_it.second) {
                        gc_queue.push(sub.subscriber_id);
                    }
                }
                // 3. 从 map 中删除
                sender_it = sender_map.erase(sender_it);
                continue;
            }

            // 4. 如果 Sender 仍然存活，则递归清理其下的 Subscriptions
            for (auto& event_it : sender_it->second) {
                CleanupExpiredSubscriptions(event_it.second, gc_queue);
            }
            ++sender_it;
        }
    }

    // --- IEventBus 订阅查询 (IsSubscribed) ---

    /**
     * @brief [实现] IEventBus::IsGlobalSubscribed。
     * @details (const,用于 `FireGlobal`的早期优化退出)。
     *
     * [锁] 需要 `event_mutex_` 的 **读锁**。
     */
    bool PluginManager::IsGlobalSubscribed(EventId event_id) const {
        // [读锁]
        std::shared_lock lock(pimpl_->event_mutex_);
        auto it = pimpl_->global_subscribers_.find(event_id);
        if (it == pimpl_->global_subscribers_.end())
            return false;

        // [注意]
        // 我们 *不* 在这里执行 Lazy GC (因为这是 const 函数)。
        // 列表可能包含已失效的 weak_ptr， 但 `FireGlobalImpl`会处理它们。
        return !it->second.empty();
    }

    /**
     * @brief [实现] IEventBus::IsSenderSubscribed。
     * @details
     *
     * [锁] 需要 `event_mutex_` 的 **读锁**。
     */
    bool PluginManager::IsSenderSubscribed(const std::weak_ptr<void>& sender_id,
        EventId event_id) const {
        // [读锁]
        std::shared_lock lock(pimpl_->event_mutex_);

        // (使用 owner_less 比较)
        auto sender_it = pimpl_->sender_subscribers_.find(sender_id);
        if (sender_it == pimpl_->sender_subscribers_.end())
            return false;

        auto event_it = sender_it->second.find(event_id);
        if (event_it == sender_it->second.end())
            return false;

        return !event_it->second.empty();
    }

    // --- 事件循环 (EventLoop) ---

    /**
     * @brief [内部] 异步事件循环的工作线程函数。
     * @details
     * (由 `PluginManager::Create`在 `std::thread`中启动)。
     */
    void PluginManager::EventLoop() {
        // 循环超时时间 (用于 GC)
        const auto kLoopTimeout = std::chrono::milliseconds(50);

        while (true) {
            PluginManagerPimpl::EventTask task_to_run;
            std::weak_ptr<void> expired_sub_to_gc;

            // --- 1. 异步事件 (EventTask) 阶段 ---
            {
                // [锁] 锁住 queue_mutex_
                std::unique_lock lock(pimpl_->queue_mutex_);

                // 等待任务，或超时
                pimpl_->queue_cv_.wait_for(lock, kLoopTimeout, [this] {
                    // 仅当有任务或停止时才“立即”唤醒
                    return !pimpl_->event_queue_.empty() || !pimpl_->running_;
                    });

                // 检查退出条件
                if (!pimpl_->running_ && pimpl_->event_queue_.empty()) {
                    return;  // 析构函数已发出停止信号，安全退出线程
                }

                // 获取任务
                if (!pimpl_->event_queue_.empty()) {
                    task_to_run = std::move(pimpl_->event_queue_.front());
                    pimpl_->event_queue_.pop();
                }
            }  // [锁释放] queue_mutex_

            // 1a. (无锁) 执行异步事件
            if (task_to_run) {
                if (pimpl_->event_trace_hook_) {
                    // ... (追踪)
                }

                // [核心] (OOB 异常处理)
                // 在 `try...catch`中执行异步回调
                try {
                    task_to_run();
                } catch (const std::exception& e) {
                    // (v3 修复)
                    // 使用 OOB 处理器报告
                    ReportException(pimpl_.get(), e);
                } catch (...) {
                    ReportUnknownException(pimpl_.get());
                }
            }

            // --- 2. 垃圾回收 (GC) 阶段 ---
            // (清理因 Lazy GC 而产生的反向查找表)

            // 2a.
            // 尝试从 GC 队列中获取一个失效指针
            {
                // [锁]
                // (注意：是 event_mutex_, 而不是 queue_mutex_)
                std::unique_lock lock(pimpl_->event_mutex_);
                if (!pimpl_->gc_queue_.empty()) {
                    expired_sub_to_gc = pimpl_->gc_queue_.front();
                    pimpl_->gc_queue_.pop();
                }
            }  // [锁释放] event_mutex_

            // 2b. (无锁)
            // 检查指针是否有效
            if (!expired_sub_to_gc.owner_before(std::weak_ptr<void>())) {
                // 2c. (有锁)
                // 执行清理
                // [锁]
                std::unique_lock lock(pimpl_->event_mutex_);
                // (从两个反向查找表中删除)
                pimpl_->global_sub_lookup_.erase(expired_sub_to_gc);
                pimpl_->sender_sub_lookup_.erase(expired_sub_to_gc);
            }
        }  // end while(true)
    }

    // --- IEventBus 实现 (Subscribe / Fire / Unsubscribe) ---

    /**
     * @brief [实现]
     * IEventBus::SubscribeGlobalImpl。
     *
     * [锁] 需要 `event_mutex_`
     * 的 **写锁**。
     */
    Connection PluginManager::SubscribeGlobalImpl(
        EventId event_id, std::weak_ptr<void> sub,
        std::function<void(const Event&)> cb, ConnectionType connection_type) {
        // [写锁]
        std::unique_lock lock(pimpl_->event_mutex_);

        // 1. 添加到主订阅列表
        pimpl_->global_subscribers_[event_id].push_back(
            { sub, std::weak_ptr<void>(), std::move(cb), connection_type });

        // 2. 添加到反向查找表 (用于 GC)
        pimpl_
            ->global_sub_lookup_[pimpl_->global_subscribers_[event_id]
            .back()
            .subscriber_id]
            .insert(event_id);

        // 3. [NEW] 创建并返回 Connection 句柄
        std::weak_ptr<IEventBus> self_bus =
            std::static_pointer_cast<IEventBus>(shared_from_this());

        return Connection(self_bus, sub, event_id, std::weak_ptr<void>());
    }

    /**
     * @brief [实现] IEventBus::FireGlobalImpl。
     * @details (读锁 -> 复制 -> 释放锁 -> 执行)
     */
    void PluginManager::FireGlobalImpl(EventId event_id, PluginPtr<Event> e_ptr) {
        void* event_ptr = e_ptr.get();
        if (pimpl_->event_trace_hook_) {
            // ... (追踪)
        }

        // [设计] 局部的 vector，用于在锁外执行
        std::vector<std::function<void(const Event&)>> direct_calls;
        std::vector<std::function<void(const Event&)>> queued_calls;

        {
            // [写锁]
            // (注意：这里需要 *写锁* 而不是读锁，
            // 因为 Lazy GC (CleanupExpiredSubscriptions) 会 *修改* 订阅列表)
            std::unique_lock lock(pimpl_->event_mutex_);
            auto it = pimpl_->global_subscribers_.find(event_id);
            if (it == pimpl_->global_subscribers_.end())
                return;

            // [核心] (Lazy GC)
            // 在复制回调之前，先清理掉已销毁的 weak_ptr
            CleanupExpiredSubscriptions(it->second, pimpl_->gc_queue_);

            // 复制回调 (线程安全)
            for (const auto& sub : it->second) {
                if (sub.connection_type == ConnectionType::kDirect) {
                    direct_calls.push_back(sub.callback);
                } else {
                    queued_calls.push_back(sub.callback);
                }
            }
        }  // [写锁释放]

        // 1. [无锁] 执行 kDirect (同步) 回调
        for (const auto& cb : direct_calls) {
            if (pimpl_->event_trace_hook_) {
                // ... (追踪)
            }

            // [核心] (OOB 异常处理)
            try {
                cb(*e_ptr);
            } catch (const std::exception& e) {
                ReportException(pimpl_.get(), e);
            } catch (...) {
                ReportUnknownException(pimpl_.get());
            }
        }

        // 2. [有锁] 将 kQueued (异步)
        // 回调推入队列
        if (!queued_calls.empty()) {
            if (pimpl_->event_trace_hook_) {
                // ... (追踪)
            }

            // 创建一个任务 (lambda)，
            // 捕获事件指针和回调列表
            PluginManagerPimpl::EventTask task = [e_ptr, queued_calls]() {
                for (const auto& cb : queued_calls) {
                    cb(*e_ptr);  // (在工作线程中执行)
                }
                };

            {
                // [锁] 锁住 queue_mutex_
                std::lock_guard lock(pimpl_->queue_mutex_);
                pimpl_->event_queue_.push(std::move(task));
            }
            pimpl_->queue_cv_.notify_one();  // 唤醒事件循环
        }
    }

    /**
     * @brief [实现] IEventBus::SubscribeToSenderImpl。
     * @details
     *
     * [锁] 需要 `event_mutex_` 的 **写锁**。
     */
    Connection PluginManager::SubscribeToSenderImpl(
        EventId event_id, std::weak_ptr<void> sub_id,
        std::weak_ptr<void> sender_id, std::function<void(const Event&)> cb,
        ConnectionType connection_type) {
        // [写锁]
        std::unique_lock lock(pimpl_->event_mutex_);

        // 1. 使用 sender_id (weak_ptr) 作为 map 的 key
        pimpl_->sender_subscribers_[sender_id][event_id].push_back(
            { std::move(sub_id), sender_id, std::move(cb), connection_type });

        // 2. 添加到反向查找表 (GC)
        pimpl_
            ->sender_sub_lookup_
            [pimpl_->sender_subscribers_[sender_id][event_id].back().subscriber_id]
            .insert({ sender_id, event_id });

        // 3. 创建并返回 Connection
        std::weak_ptr<IEventBus> self_bus =
            std::static_pointer_cast<IEventBus>(shared_from_this());

        return Connection(self_bus,
            pimpl_->sender_subscribers_[sender_id][event_id]
            .back()
            .subscriber_id,
            event_id, sender_id);
    }

    /**
     * @brief [实现] IEventBus::FireToSenderImpl。
     * @details
     * 使用 `weak_ptr`m作为 key。
     * (读锁 -> 复制 -> 释放锁 -> 执行)
     */
    void PluginManager::FireToSenderImpl(const std::weak_ptr<void>& sender_id,
        EventId event_id,
        PluginPtr<Event> e_ptr) {
        if (pimpl_->event_trace_hook_) {
            // ... (追踪)
        }

        std::vector<std::function<void(const Event&)>> direct_calls;
        std::vector<std::function<void(const Event&)>> queued_calls;

        {
            // [写锁]
            // (同样，需要写锁以支持 Lazy GC)
            std::unique_lock lock(pimpl_->event_mutex_);

            // 使用 weak_ptr 查找
            auto sender_it = pimpl_->sender_subscribers_.find(sender_id);
            if (sender_it == pimpl_->sender_subscribers_.end())
                return;

            auto event_it = sender_it->second.find(event_id);
            if (event_it == sender_it->second.end())
                return;

            // (Lazy GC) 清理
            CleanupExpiredSubscriptions(event_it->second, pimpl_->gc_queue_);

            // 复制回调
            for (const auto& sub : event_it->second) {
                if (sub.connection_type == ConnectionType::kDirect) {
                    direct_calls.push_back(sub.callback);
                } else {
                    queued_calls.push_back(sub.callback);
                }
            }
        }  // [写锁释放]

        // 1. [无锁] 执行 kDirect
        for (const auto& cb : direct_calls) {
            try {
                cb(*e_ptr);
            } catch (const std::exception& e) {
                ReportException(pimpl_.get(), e);
            } catch (...) {
                ReportUnknownException(pimpl_.get());
            }
        }

        // 2. [有锁] 推入 kQueued 队列
        if (!queued_calls.empty()) {
            PluginManagerPimpl::EventTask task = [e_ptr, queued_calls]() {
                for (const auto& cb : queued_calls) {
                    cb(*e_ptr);
                }
                };
            {
                std::lock_guard lock(pimpl_->queue_mutex_);
                pimpl_->event_queue_.push(std::move(task));
            }
            pimpl_->queue_cv_.notify_one();
        }
    }

    /**
     * @brief [实现]
     * IEventBus::Unsubscribe (特定订阅)。
     * @details
     * (由 `Connection::Disconnect` 调用)。
     *
     * [锁] 需要 `event_mutex_` 的 **写锁**。
     */
    void PluginManager::Unsubscribe(std::shared_ptr<void> subscriber,
        EventId event_id,
        const std::weak_ptr<void>& sender_key) {
        // [写锁]
        std::unique_lock lock(pimpl_->event_mutex_);
        std::weak_ptr<void> weak_id = subscriber;

        // 比较 weak_ptr
        // 是否指向同一个控制块
        auto is_same_subscriber = [&weak_id](
            const PluginManagerPimpl::Subscription& s) {
                return !s.subscriber_id.owner_before(weak_id) &&
                    !weak_id.owner_before(s.subscriber_id);
            };

        // 使用 sender_key.expired() 检查)
        if (sender_key.expired()) {
            // --- 1. 清理全局订阅 ---
            auto event_list_it = pimpl_->global_subscribers_.find(event_id);
            if (event_list_it != pimpl_->global_subscribers_.end()) {
                auto& subs = event_list_it->second;
                subs.erase(std::remove_if(subs.begin(), subs.end(), is_same_subscriber),
                    subs.end());
            }
            // 清理反向查找表 (GC)
            auto global_it = pimpl_->global_sub_lookup_.find(weak_id);
            if (global_it != pimpl_->global_sub_lookup_.end()) {
                global_it->second.erase(event_id);
                if (global_it->second.empty()) {
                    pimpl_->global_sub_lookup_.erase(global_it);
                }
            }

        } else {
            // --- 2. 清理特定发布者订阅 ---

            // 使用 sender_key 查找)
            auto sender_map_it = pimpl_->sender_subscribers_.find(sender_key);
            if (sender_map_it != pimpl_->sender_subscribers_.end()) {
                auto event_list_it = sender_map_it->second.find(event_id);
                if (event_list_it != sender_map_it->second.end()) {
                    auto& subs = event_list_it->second;
                    subs.erase(
                        std::remove_if(subs.begin(), subs.end(), is_same_subscriber),
                        subs.end());
                }
            }
            // 清理反向查找表 (GC)
            auto sender_it = pimpl_->sender_sub_lookup_.find(weak_id);
            if (sender_it != pimpl_->sender_sub_lookup_.end()) {
                // (解决方案 8)
                sender_it->second.erase({ sender_key, event_id });
                if (sender_it->second.empty()) {
                    pimpl_->sender_sub_lookup_.erase(sender_it);
                }
            }
        }
    }

    /**
     * @brief [实现]
     * IEventBus::Unsubscribe (全部订阅)。
     *
     * [锁] 需要 `event_mutex_` 的 **写锁**。
     */
    void PluginManager::Unsubscribe(std::shared_ptr<void> subscriber) {
        // [写锁]
        std::unique_lock lock(pimpl_->event_mutex_);
        std::weak_ptr<void> weak_id = subscriber;

        auto is_same_subscriber = [&weak_id](
            const PluginManagerPimpl::Subscription& s) {
                return !s.subscriber_id.owner_before(weak_id) &&
                    !weak_id.owner_before(s.subscriber_id);
            };

        // 1. 清理全局订阅
        // (使用反向查找表 `global_sub_lookup_` 进行 O(logN) 查找)
        auto global_it = pimpl_->global_sub_lookup_.find(weak_id);
        if (global_it != pimpl_->global_sub_lookup_.end()) {
            for (const EventId& event_id : global_it->second) {
                auto event_list_it = pimpl_->global_subscribers_.find(event_id);
                if (event_list_it != pimpl_->global_subscribers_.end()) {
                    auto& subs = event_list_it->second;
                    subs.erase(
                        std::remove_if(subs.begin(), subs.end(), is_same_subscriber),
                        subs.end());
                }
            }
            pimpl_->global_sub_lookup_.erase(global_it);
        }

        // 2. 清理特定发布者订阅
        // (使用反向查找表 `sender_sub_lookup_` 进行 O(logN) 查找)
        auto sender_it = pimpl_->sender_sub_lookup_.find(weak_id);
        if (sender_it != pimpl_->sender_sub_lookup_.end()) {
            // (C++17 结构化绑定)
            for (const auto& [sender_key, event_id] : sender_it->second) {
                auto sender_map_it = pimpl_->sender_subscribers_.find(sender_key);
                if (sender_map_it != pimpl_->sender_subscribers_.end()) {
                    auto event_list_it = sender_map_it->second.find(event_id);
                    if (event_list_it != sender_map_it->second.end()) {
                        auto& subs = event_list_it->second;
                        subs.erase(
                            std::remove_if(subs.begin(), subs.end(), is_same_subscriber),
                            subs.end());
                    }
                }
            }
            pimpl_->sender_sub_lookup_.erase(sender_it);
        }
    }

}  // namespace z3y