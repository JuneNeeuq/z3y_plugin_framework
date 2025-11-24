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
 * 3. `CleanupExpiredSubscriptions`: (Lazy GC) 垃圾回收机制，用于清理已销毁的 `weak_ptr` 订阅。
 *
 * [线程安全模型 (EventBus)]
 * 1. `event_mutex_` (读写锁):
 * - `Fire...`: [高频] 获取 **写锁** (`std::unique_lock`)。
 * (注：必须是 *写锁* 而不是读锁，因为 `CleanupExpiredSubscriptions` (Lazy GC)
 * 需要在遍历前 *修改* 订阅列表，剔除无效订阅)。
 * 复制回调后 **立即释放写锁**。
 * - `Subscribe...` / `Unsubscribe...`: [低频] 获取 **写锁**。 修改订阅列表 (maps)。 释放写锁。
 *
 * 2. [无锁执行]
 * - `Fire...` 在释放锁 *之后*， 开始遍历 *局部的 vector* 并执行回调。
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
 * - `CleanupExpiredSubscriptions` 是一个辅助函数。
 * - 它在 *下一次* `Fire...` 某个特定事件时被调用 (在写锁下)，
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
    * [Bug修复说明]
    * 原逻辑直接检查 `sender_id.expired()`，导致全局订阅（sender_id 为空 weak_ptr）也被误删。
    * 修复后，增加了对“空 weak_ptr”的特判。
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
                    // 1. 检查订阅者 (subscriber) 是否已销毁
                    bool subscriber_expired = s.subscriber_id.expired();

                    // 2. 检查发布者 (sender) 是否已销毁
                    // [关键修复] 区分 "Empty WeakPtr" (全局订阅) 和 "Expired WeakPtr" (对象已死)
                    // - 如果 owner_before(empty) 为 false 且 empty.owner_before(ptr) 为 false，说明 ptr 等于 empty。
                    bool is_sender_empty = !s.sender_id.owner_before(std::weak_ptr<void>()) &&
                        !std::weak_ptr<void>().owner_before(s.sender_id);

                    // 只有当它 *不是* 全局订阅 (即 sender 不为空) 且已经过期时，才算 Sender 失效。
                    // 全局订阅的 sender_id 永远是空的 weak_ptr (expired==true)，不能因此删除。
                    bool sender_expired = !is_sender_empty && s.sender_id.expired();

                    // 3. 如果任一失效，则标记为待移除
                    if (subscriber_expired || sender_expired) {
                        gc_queue.push(s.subscriber_id);  // 放入 GC 队列 (用于后续清理反向索引)
                        return true;                     // true = 标记为移除
                    }
                    return false;  // false = 保留
                }),
            subs.end());
    }

    // --- IEventBus 订阅查询 (IsSubscribed) ---

    /**
     * @brief [实现] IEventBus::IsGlobalSubscribed。
     * @details (const, 用于 `FireGlobal` 的早期优化退出)。
     *
     * [锁] 需要 `event_mutex_` 的 **读锁**。
     */
    bool PluginManager::IsGlobalSubscribed(EventId event_id) const {
        // [读锁]
        std::shared_lock lock(pimpl_->event_mutex_);
        auto it = pimpl_->global_subscribers_.find(event_id);

        // 只要列表不为空，就认为有订阅者
        // [注意] 我们 *不* 在这里执行 Lazy GC (因为这是 const 函数)。
        // 列表可能包含已失效的 weak_ptr， 但 `FireGlobalImpl` 会处理它们。
        return (it != pimpl_->global_subscribers_.end()) && !it->second.empty();
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

        // (使用 owner_less 比较查找 map)
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
     * (由 `PluginManager::Create` 在 `std::thread` 中启动)。
     * 负责处理 `kQueued` 类型的事件回调以及执行 GC 清理。
     */
    void PluginManager::EventLoop() {
        // 循环超时时间 (用于定期醒来执行 GC，即使没有事件)
        const auto kLoopTimeout = std::chrono::milliseconds(50);

        while (true) {
            PluginManagerPimpl::EventTask task_to_run;
            std::weak_ptr<void> expired_sub_to_gc;

            // --- 1. 获取异步任务 (加锁) ---
            {
                // [锁] 锁住 queue_mutex_
                std::unique_lock lock(pimpl_->queue_mutex_);

                // 等待任务，或超时
                pimpl_->queue_cv_.wait_for(lock, kLoopTimeout, [this] {
                    // 仅当有任务或收到停止信号时才“立即”唤醒
                    return !pimpl_->event_queue_.empty() || !pimpl_->running_;
                    });

                // 检查退出条件
                if (!pimpl_->running_ && pimpl_->event_queue_.empty()) {
                    return;  // 析构函数已发出停止信号且队列为空，安全退出线程
                }

                // 取出一个任务
                if (!pimpl_->event_queue_.empty()) {
                    task_to_run = std::move(pimpl_->event_queue_.front());
                    pimpl_->event_queue_.pop();
                }
            }  // [锁释放] queue_mutex_

            // --- 2. 执行异步任务 (无锁) ---
            if (task_to_run) {
                // [调试] 触发钩子：异步任务开始执行
                if (pimpl_->event_trace_hook_) {
                    pimpl_->event_trace_hook_(EventTracePoint::kQueuedExecuteStart, 0, nullptr, "AsyncExecStart");
                }

                // [核心] (OOB 异常处理)
                // 在 `try...catch` 中执行异步回调，防止插件崩溃导致整个程序退出
                try {
                    task_to_run();
                } catch (const std::exception& e) {
                    // 使用 OOB 处理器向宿主报告
                    ReportException(pimpl_.get(), e);
                } catch (...) {
                    ReportUnknownException(pimpl_.get());
                }

                // [调试] 触发钩子：异步任务执行结束
                if (pimpl_->event_trace_hook_) {
                    pimpl_->event_trace_hook_(EventTracePoint::kQueuedExecuteEnd, 0, nullptr, "AsyncExecEnd");
                }
            }

            // --- 3. 垃圾回收 (GC) 阶段 ---
            // (清理因 Lazy GC 而产生的反向查找表，需要在 event_mutex_ 下操作)

            // 3a. 尝试从 GC 队列中获取一个失效指针
            {
                // [锁] (注意：是 event_mutex_, 不是 queue_mutex_)
                std::unique_lock lock(pimpl_->event_mutex_);
                if (!pimpl_->gc_queue_.empty()) {
                    expired_sub_to_gc = pimpl_->gc_queue_.front();
                    pimpl_->gc_queue_.pop();
                }
            }

            // 3b. 执行清理 (如果拿到了需要清理的指针)
            if (!expired_sub_to_gc.owner_before(std::weak_ptr<void>())) {
                // 再次加锁执行删除操作
                std::unique_lock lock(pimpl_->event_mutex_);
                // (从两个反向查找表中删除该订阅者的记录)
                pimpl_->global_sub_lookup_.erase(expired_sub_to_gc);
                pimpl_->sender_sub_lookup_.erase(expired_sub_to_gc);
            }
        }  // end while(true)
    }

    // --- IEventBus 实现 (Subscribe / Fire / Unsubscribe) ---

    /**
     * @brief [实现] IEventBus::SubscribeGlobalImpl。
     * @details
     * [锁] 需要 `event_mutex_` 的 **写锁**。
     */
    Connection PluginManager::SubscribeGlobalImpl(
        EventId event_id, std::weak_ptr<void> sub,
        std::function<void(const Event&)> cb, ConnectionType connection_type) {

        // [写锁]
        std::unique_lock lock(pimpl_->event_mutex_);

        // 1. 添加到主订阅列表 (Global Map)
        // 注意：对于全局订阅，sender_id 字段留空 (weak_ptr)
        pimpl_->global_subscribers_[event_id].push_back(
            { sub, std::weak_ptr<void>(), std::move(cb), connection_type });

        // 2. 添加到反向查找表 (Global Lookup) - 用于 Unsubscribe 时的快速查找
        pimpl_->global_sub_lookup_[pimpl_->global_subscribers_[event_id].back().subscriber_id].insert(event_id);

        // 3. 创建并返回 Connection 句柄
        std::weak_ptr<IEventBus> self_bus = std::static_pointer_cast<IEventBus>(shared_from_this());

        // Connection 构造函数最后一个参数是 sender_key，全局订阅为空
        return Connection(self_bus, sub, event_id, std::weak_ptr<void>());
    }

    /**
     * @brief [实现] IEventBus::FireGlobalImpl。
     * @details
     * 流程：[写锁 -> GC -> 复制回调 -> 释放锁] -> [执行回调]
     */
    void PluginManager::FireGlobalImpl(EventId event_id, PluginPtr<Event> e_ptr) {
        // [调试] 触发钩子：事件已触发
        if (pimpl_->event_trace_hook_) {
            pimpl_->event_trace_hook_(EventTracePoint::kEventFired, event_id, nullptr, "GlobalFire");
        }

        // 准备两个列表，分别存放需要同步执行和异步执行的回调
        std::vector<std::function<void(const Event&)>> direct_calls;
        std::vector<std::function<void(const Event&)>> queued_calls;

        {
            // [写锁]
            // (注意：必须是写锁，因为 CleanupExpiredSubscriptions 会修改 vector)
            std::unique_lock lock(pimpl_->event_mutex_);
            auto it = pimpl_->global_subscribers_.find(event_id);
            if (it == pimpl_->global_subscribers_.end()) return;

            // [核心] (Lazy GC)
            // 在复制回调之前，先清理掉已销毁的 weak_ptr，避免调用无效回调
            CleanupExpiredSubscriptions(it->second, pimpl_->gc_queue_);

            // 复制回调 (将回调从受锁保护的 map 中复制到局部 vector)
            for (const auto& sub : it->second) {
                if (sub.connection_type == ConnectionType::kDirect) {
                    direct_calls.push_back(sub.callback);
                } else {
                    queued_calls.push_back(sub.callback);
                }
            }
        }  // [写锁释放] - 后续执行不再持有锁，防止死锁

        // 1. [无锁] 执行 kDirect (同步) 回调
        for (const auto& cb : direct_calls) {
            // [调试] 触发钩子：同步回调开始
            if (pimpl_->event_trace_hook_) {
                pimpl_->event_trace_hook_(EventTracePoint::kDirectCallStart, event_id, nullptr, "DirectCall");
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

        // 2. [有锁] 将 kQueued (异步) 回调推入队列
        if (!queued_calls.empty()) {
            // [调试] 触发钩子：推入异步队列
            if (pimpl_->event_trace_hook_) {
                pimpl_->event_trace_hook_(EventTracePoint::kQueuedEntry, event_id, nullptr, "QueuePush");
            }

            // 创建一个任务 (lambda)，捕获事件指针和回调列表
            // 注意：e_ptr (shared_ptr) 被捕获，保证事件对象在异步执行期间存活
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
     * [锁] 需要 `event_mutex_` 的 **写锁**。
     */
    Connection PluginManager::SubscribeToSenderImpl(
        EventId event_id, std::weak_ptr<void> sub_id,
        std::weak_ptr<void> sender_id, std::function<void(const Event&)> cb,
        ConnectionType connection_type) {

        // [写锁]
        std::unique_lock lock(pimpl_->event_mutex_);

        // 1. 添加到特定发布者订阅列表 (Sender Map)
        // 使用 sender_id (weak_ptr) 作为一级 Key
        pimpl_->sender_subscribers_[sender_id][event_id].push_back(
            { std::move(sub_id), sender_id, std::move(cb), connection_type });

        // 2. 添加到反向查找表 (Sender Lookup)
        pimpl_->sender_sub_lookup_[pimpl_->sender_subscribers_[sender_id][event_id].back().subscriber_id].insert({ sender_id, event_id });

        // 3. 创建并返回 Connection
        std::weak_ptr<IEventBus> self_bus = std::static_pointer_cast<IEventBus>(shared_from_this());

        // 这里的 sender_id 是有效的 (非空)
        return Connection(self_bus, pimpl_->sender_subscribers_[sender_id][event_id].back().subscriber_id, event_id, sender_id);
    }

    /**
     * @brief [实现] IEventBus::FireToSenderImpl。
     * @details
     * 使用 `weak_ptr` 作为 Key 查找订阅者。
     * 流程与 FireGlobalImpl 类似：[锁 -> 复制 -> 解锁 -> 执行]。
     */
    void PluginManager::FireToSenderImpl(const std::weak_ptr<void>& sender_id, EventId event_id, PluginPtr<Event> e_ptr) {
        // [调试] 触发钩子
        if (pimpl_->event_trace_hook_) {
            pimpl_->event_trace_hook_(EventTracePoint::kEventFired, event_id, nullptr, "SenderFire");
        }

        std::vector<std::function<void(const Event&)>> direct_calls;
        std::vector<std::function<void(const Event&)>> queued_calls;

        {
            // [写锁]
            std::unique_lock lock(pimpl_->event_mutex_);

            // 1. 查找发布者
            auto sender_it = pimpl_->sender_subscribers_.find(sender_id);
            if (sender_it == pimpl_->sender_subscribers_.end()) return;

            // 2. 查找事件
            auto event_it = sender_it->second.find(event_id);
            if (event_it == sender_it->second.end()) return;

            // 3. (Lazy GC) 清理
            CleanupExpiredSubscriptions(event_it->second, pimpl_->gc_queue_);

            // 4. 复制回调
            for (const auto& sub : event_it->second) {
                if (sub.connection_type == ConnectionType::kDirect) {
                    direct_calls.push_back(sub.callback);
                } else {
                    queued_calls.push_back(sub.callback);
                }
            }
        }  // [写锁释放]

        // 1. 执行同步回调
        for (const auto& cb : direct_calls) {
            if (pimpl_->event_trace_hook_) {
                pimpl_->event_trace_hook_(EventTracePoint::kDirectCallStart, event_id, nullptr, "DirectCall");
            }
            try {
                cb(*e_ptr);
            } catch (const std::exception& e) {
                ReportException(pimpl_.get(), e);
            } catch (...) {
                ReportUnknownException(pimpl_.get());
            }
        }

        // 2. 推入异步队列
        if (!queued_calls.empty()) {
            if (pimpl_->event_trace_hook_) {
                pimpl_->event_trace_hook_(EventTracePoint::kQueuedEntry, event_id, nullptr, "QueuePush");
            }

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
     * @brief [实现] IEventBus::Unsubscribe (特定订阅)。
     * @details
     * (由 `Connection::Disconnect` 调用)。
     *
     * [锁] 需要 `event_mutex_` 的 **写锁**。
     */
    void PluginManager::Unsubscribe(std::shared_ptr<void> subscriber, EventId event_id, const std::weak_ptr<void>& sender_key) {
        // [写锁]
        std::unique_lock lock(pimpl_->event_mutex_);
        std::weak_ptr<void> weak_id = subscriber;

        // 辅助 Lambda: 比较 weak_ptr 是否指向同一个对象
        auto is_same_subscriber = [&weak_id](const PluginManagerPimpl::Subscription& s) {
            return !s.subscriber_id.owner_before(weak_id) && !weak_id.owner_before(s.subscriber_id);
            };

        // [Bug修复说明]
        // 原逻辑仅使用 `sender_key.expired()` 来判断是否为全局订阅。
        // 但如果订阅的是一个已经销毁的对象 (Dead Sender)，expired 也会返回 true，导致误入全局清理逻辑。
        // 修复：使用 owner_before 技巧精准判断 weak_ptr 是否原本就是空的 (Global 标志)。
        bool is_global_subscription = !sender_key.owner_before(std::weak_ptr<void>()) &&
            !std::weak_ptr<void>().owner_before(sender_key);

        if (is_global_subscription) {
            // --- 1. 清理全局订阅 (sender_key 为空) ---
            auto event_list_it = pimpl_->global_subscribers_.find(event_id);
            if (event_list_it != pimpl_->global_subscribers_.end()) {
                auto& subs = event_list_it->second;
                subs.erase(std::remove_if(subs.begin(), subs.end(), is_same_subscriber), subs.end());
            }
            // 同步清理反向查找表
            auto global_it = pimpl_->global_sub_lookup_.find(weak_id);
            if (global_it != pimpl_->global_sub_lookup_.end()) {
                global_it->second.erase(event_id);
                if (global_it->second.empty()) {
                    pimpl_->global_sub_lookup_.erase(global_it);
                }
            }
        } else {
            // --- 2. 清理特定发布者订阅 (sender_key 不为空) ---
            // 注意：即使 sender_key 已经 expired (对象已死)，owner_less 查找仍然有效。

            auto sender_map_it = pimpl_->sender_subscribers_.find(sender_key);
            if (sender_map_it != pimpl_->sender_subscribers_.end()) {
                auto event_list_it = sender_map_it->second.find(event_id);
                if (event_list_it != sender_map_it->second.end()) {
                    auto& subs = event_list_it->second;
                    subs.erase(std::remove_if(subs.begin(), subs.end(), is_same_subscriber), subs.end());
                }
            }
            // 同步清理反向查找表
            auto sender_it = pimpl_->sender_sub_lookup_.find(weak_id);
            if (sender_it != pimpl_->sender_sub_lookup_.end()) {
                sender_it->second.erase({ sender_key, event_id });
                if (sender_it->second.empty()) {
                    pimpl_->sender_sub_lookup_.erase(sender_it);
                }
            }
        }
    }

    /**
     * @brief [实现] IEventBus::Unsubscribe (全部订阅)。
     * @details
     * 清除指定订阅者的 *所有* 订阅 (包括全局和特定)。
     * 依赖反向查找表 (`global_sub_lookup_`, `sender_sub_lookup_`) 提高效率。
     *
     * [锁] 需要 `event_mutex_` 的 **写锁**。
     */
    void PluginManager::Unsubscribe(std::shared_ptr<void> subscriber) {
        // [写锁]
        std::unique_lock lock(pimpl_->event_mutex_);
        std::weak_ptr<void> weak_id = subscriber;

        auto is_same_subscriber = [&weak_id](const PluginManagerPimpl::Subscription& s) {
            return !s.subscriber_id.owner_before(weak_id) && !weak_id.owner_before(s.subscriber_id);
            };

        // 1. 清理全局订阅
        // (通过反向表找到该用户订阅了哪些事件 ID，然后去主表中删除)
        auto global_it = pimpl_->global_sub_lookup_.find(weak_id);
        if (global_it != pimpl_->global_sub_lookup_.end()) {
            for (const EventId& event_id : global_it->second) {
                auto event_list_it = pimpl_->global_subscribers_.find(event_id);
                if (event_list_it != pimpl_->global_subscribers_.end()) {
                    auto& subs = event_list_it->second;
                    subs.erase(std::remove_if(subs.begin(), subs.end(), is_same_subscriber), subs.end());
                }
            }
            pimpl_->global_sub_lookup_.erase(global_it);
        }

        // 2. 清理特定发布者订阅
        // (同样通过反向表查找)
        auto sender_it = pimpl_->sender_sub_lookup_.find(weak_id);
        if (sender_it != pimpl_->sender_sub_lookup_.end()) {
            for (const auto& [sender_key, event_id] : sender_it->second) {
                auto sender_map_it = pimpl_->sender_subscribers_.find(sender_key);
                if (sender_map_it != pimpl_->sender_subscribers_.end()) {
                    auto event_list_it = sender_map_it->second.find(event_id);
                    if (event_list_it != sender_map_it->second.end()) {
                        auto& subs = event_list_it->second;
                        subs.erase(std::remove_if(subs.begin(), subs.end(), is_same_subscriber), subs.end());
                    }
                }
            }
            pimpl_->sender_sub_lookup_.erase(sender_it);
        }
    }

} // namespace z3y