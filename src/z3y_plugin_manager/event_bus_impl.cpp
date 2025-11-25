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
 * @brief 事件总线的高性能实现。
 *
 * @details
 * 这里实现了 `PluginManager` 中关于 `IEventBus` 的部分。
 * 核心算法：
 * 1. **COW (Copy-On-Write)**: 读取订阅列表时不加锁（或只加极短的锁），修改时复制一份新列表。
 * 这保证了 `FireGlobal` 遍历列表时绝对线程安全，且不会被递归调用（在回调里订阅/取消订阅）搞死锁。
 * 2. **Lazy GC (懒惰垃圾回收)**: 当发现失效的订阅（如弱引用过期）时，不立即删除，
 * 而是标记该事件 ID，稍后异步批量清理。
 */

#include "plugin_manager_pimpl.h"
#include <iostream>
#include "framework/connection.h"

namespace z3y {

    extern void ReportException(PluginManagerPimpl* pimpl, const std::exception& e);
    extern void ReportUnknownException(PluginManagerPimpl* pimpl);

    // =========================================================================
    // GC (垃圾回收) 机制
    // =========================================================================

    /**
     * @brief 调度垃圾回收。
     * @details 将一个“执行 GC”的任务放入事件队列，让工作线程稍后处理。
     */
    void PluginManager::ScheduleGC(EventId event_id) {
        {
            std::lock_guard<std::mutex> lock(pimpl_->gc_status_mutex_);
            if (pimpl_->pending_gc_events_.count(event_id)) return; // 已经在排队了
            pimpl_->pending_gc_events_.insert(event_id);
        }

        // 构造任务
        PluginManagerPimpl::EventTask task;
        task.event_id = event_id; // 标记这是针对哪个事件的 GC
        task.func = [this, event_id]() {
            this->PerformGC(event_id);
            };

        {
            std::lock_guard<std::mutex> lock(pimpl_->queue_mutex_);
            pimpl_->event_queue_.push(std::move(task));
        }
        pimpl_->queue_cv_.notify_one();
    }

    /**
     * @brief 执行垃圾回收 (在工作线程运行)。
     * @details 遍历订阅列表，剔除无效项，生成新列表替换旧列表 (COW)。
     */
    void PluginManager::PerformGC(EventId event_id) {
        {
            std::lock_guard<std::mutex> lock(pimpl_->gc_status_mutex_);
            pimpl_->pending_gc_events_.erase(event_id);
        }

        std::lock_guard<std::mutex> lock(pimpl_->subscriber_map_mutex_);
        auto it = pimpl_->global_subscribers_.find(event_id);
        if (it == pimpl_->global_subscribers_.end() || !it->second) return;

        auto& current_ptr = it->second;
        // 创建新列表，准备搬运有效数据
        auto new_list = std::make_shared<PluginManagerPimpl::SubList>();
        new_list->reserve(current_ptr->size());

        bool garbage_found = false;
        for (const auto& sub : *current_ptr) {
            bool is_dead = sub.subscriber_id.expired();
            bool is_disconnected = (sub.active_token && !sub.active_token->load(std::memory_order_relaxed));

            if (!is_dead && !is_disconnected) {
                new_list->push_back(sub); // 有效，搬运
            } else {
                garbage_found = true; // 发现垃圾
            }
        }

        if (garbage_found) {
            current_ptr = new_list; // 原子替换
        }
    }

    // =========================================================================
    // EventLoop (工作线程)
    // =========================================================================

    /**
     * @brief 异步事件处理循环。
     */
    void PluginManager::EventLoop() {
        while (true) {
            PluginManagerPimpl::EventTask task;
            {
                // 等待任务
                std::unique_lock<std::mutex> lock(pimpl_->queue_mutex_);
                pimpl_->queue_cv_.wait(lock, [this] {
                    return !pimpl_->event_queue_.empty() || !pimpl_->running_;
                    });

                if (!pimpl_->running_ && pimpl_->event_queue_.empty()) return; // 退出

                if (!pimpl_->event_queue_.empty()) {
                    task = std::move(pimpl_->event_queue_.front());
                    pimpl_->event_queue_.pop();
                }
            }

            if (task.func) {
                // [Trace] 埋点：开始执行异步任务
                if (pimpl_->event_trace_hook_) {
                    pimpl_->event_trace_hook_(EventTracePoint::kQueuedExecuteStart, task.event_id, nullptr, "AsyncExecStart");
                }

                try {
                    task.func(); // 执行任务
                } catch (const std::exception& e) {
                    ReportException(pimpl_.get(), e); // 报告异常
                } catch (...) {
                    ReportUnknownException(pimpl_.get());
                }

                // [Trace] 埋点：结束执行
                if (pimpl_->event_trace_hook_) {
                    pimpl_->event_trace_hook_(EventTracePoint::kQueuedExecuteEnd, task.event_id, nullptr, "AsyncExecEnd");
                }
            }
        }
    }

    // =========================================================================
    // Subscribe Global
    // =========================================================================

    /**
     * @brief 全局订阅实现。
     */
    Connection PluginManager::SubscribeGlobalImpl(
        EventId event_id, std::weak_ptr<void> sub,
        std::function<void(const Event&)> cb, ConnectionType type) {

        // 1. 创建原子票据 (默认为 true)
        auto ticket = std::make_shared<std::atomic<bool>>(true);

        std::lock_guard<std::mutex> lock(pimpl_->subscriber_map_mutex_);
        auto& current_ptr = pimpl_->global_subscribers_[event_id];

        // 2. 将订阅加入列表 (COW 逻辑)
        if (current_ptr && current_ptr.use_count() == 1) {
            // 优化：如果当前列表只有我在用（没人在遍历），直接修改
            auto& list = const_cast<PluginManagerPimpl::SubList&>(*current_ptr);
            list.emplace_back(sub, std::weak_ptr<void>(), std::move(cb), type, ticket);
        } else {
            // 标准 COW：有人在读，复制一份新的修改
            auto new_list = current_ptr
                ? std::make_shared<PluginManagerPimpl::SubList>(*current_ptr)
                : std::make_shared<PluginManagerPimpl::SubList>();
            new_list->reserve(new_list->size() + 4);
            new_list->emplace_back(sub, std::weak_ptr<void>(), std::move(cb), type, ticket);
            current_ptr = new_list;
        }

        // 3. 记录反查表
        pimpl_->global_sub_lookup_[sub].insert(event_id);

        // 4. 返回 Connection
        return Connection(std::static_pointer_cast<IEventBus>(shared_from_this()),
            sub, event_id, std::weak_ptr<void>(), ticket);
    }

    // =========================================================================
    // Fire Global
    // =========================================================================

    /**
     * @brief 全局广播实现。
     */
    void PluginManager::FireGlobalImpl(EventId event_id, PluginPtr<Event> e_ptr) {
        PluginManagerPimpl::SubListPtr list_snapshot;
        {
            // 1. 获取列表快照 (持有 shared_ptr，增加引用计数)
            std::lock_guard<std::mutex> lock(pimpl_->subscriber_map_mutex_);
            auto it = pimpl_->global_subscribers_.find(event_id);
            if (it != pimpl_->global_subscribers_.end()) {
                list_snapshot = it->second;
            }
        }

        if (!list_snapshot) return;

        if (pimpl_->event_trace_hook_) pimpl_->event_trace_hook_(EventTracePoint::kEventFired, event_id, nullptr, "GlobalFire");

        bool needs_gc = false;

        // 2. 遍历快照 (此时无需锁，因为列表是 const 的)
        for (const auto& sub : *list_snapshot) {
            // 检查票据：如果已断开，跳过
            if (sub.active_token && !sub.active_token->load(std::memory_order_acquire)) {
                needs_gc = true; continue;
            }
            // 检查弱引用
            if (sub.subscriber_id.expired()) {
                needs_gc = true; continue;
            }

            if (sub.connection_type == ConnectionType::kDirect) {
                // 同步调用
                if (pimpl_->event_trace_hook_) pimpl_->event_trace_hook_(EventTracePoint::kDirectCallStart, event_id, nullptr, "DirectCall");
                try {
                    sub.callback(*e_ptr);
                } catch (const std::exception& e) {
                    ReportException(pimpl_.get(), e);
                } catch (...) {
                    ReportUnknownException(pimpl_.get());
                }
            } else {
                // 异步调用
                auto token = sub.active_token;
                auto cb = sub.callback;

                PluginManagerPimpl::EventTask task;
                task.event_id = event_id;
                // 包装任务：在执行前再次验票 (Double Check)
                task.func = [e_ptr, cb, token]() {
                    if (token && token->load(std::memory_order_acquire)) {
                        cb(*e_ptr);
                    }
                    };

                {
                    std::lock_guard<std::mutex> lock(pimpl_->queue_mutex_);
                    pimpl_->event_queue_.push(std::move(task));
                }
                pimpl_->queue_cv_.notify_one();

                if (pimpl_->event_trace_hook_) pimpl_->event_trace_hook_(EventTracePoint::kQueuedEntry, event_id, nullptr, "QueuePush");
            }
        }

        if (needs_gc) ScheduleGC(event_id);
    }

    // ... (FireToSenderImpl 等其他函数逻辑类似，此处省略重复注释，但代码保留完整) ...
    // [为节省篇幅，Sender 部分代码与 Global 逻辑高度一致，仅查找表不同]

    bool PluginManager::IsSenderSubscribed(const std::weak_ptr<void>& sender_id, EventId event_id) const {
        std::lock_guard<std::mutex> lock(pimpl_->subscriber_map_mutex_);
        auto sender_it = pimpl_->sender_subscribers_.find(sender_id);
        if (sender_it == pimpl_->sender_subscribers_.end()) return false;
        auto event_it = sender_it->second.find(event_id);
        return event_it != sender_it->second.end() && event_it->second && !event_it->second->empty();
    }

    bool PluginManager::IsGlobalSubscribed(EventId event_id) const {
        std::lock_guard<std::mutex> lock(pimpl_->subscriber_map_mutex_);
        auto it = pimpl_->global_subscribers_.find(event_id);
        return it != pimpl_->global_subscribers_.end() && it->second && !it->second->empty();
    }

    Connection PluginManager::SubscribeToSenderImpl(
        EventId event_id, std::weak_ptr<void> sub_id,
        std::weak_ptr<void> sender_id, std::function<void(const Event&)> cb,
        ConnectionType connection_type) {
        auto ticket = std::make_shared<std::atomic<bool>>(true);
        std::lock_guard<std::mutex> lock(pimpl_->subscriber_map_mutex_);

        auto& event_map = pimpl_->sender_subscribers_[sender_id];
        auto& current_ptr = event_map[event_id];

        if (current_ptr && current_ptr.use_count() == 1) {
            auto& list = const_cast<PluginManagerPimpl::SubList&>(*current_ptr);
            list.emplace_back(sub_id, sender_id, std::move(cb), connection_type, ticket);
        } else {
            auto new_list = current_ptr
                ? std::make_shared<PluginManagerPimpl::SubList>(*current_ptr)
                : std::make_shared<PluginManagerPimpl::SubList>();
            new_list->reserve(new_list->size() + 4);
            new_list->emplace_back(sub_id, sender_id, std::move(cb), connection_type, ticket);
            current_ptr = new_list;
        }

        pimpl_->sender_sub_lookup_[sub_id].insert({ sender_id, event_id });
        return Connection(std::static_pointer_cast<IEventBus>(shared_from_this()),
            sub_id, event_id, sender_id, ticket);
    }

    void PluginManager::FireToSenderImpl(const std::weak_ptr<void>& sender_id, EventId event_id, PluginPtr<Event> e_ptr) {
        PluginManagerPimpl::SubListPtr list_snapshot;
        {
            std::lock_guard<std::mutex> lock(pimpl_->subscriber_map_mutex_);
            auto sender_it = pimpl_->sender_subscribers_.find(sender_id);
            if (sender_it != pimpl_->sender_subscribers_.end()) {
                auto event_it = sender_it->second.find(event_id);
                if (event_it != sender_it->second.end()) list_snapshot = event_it->second;
            }
        }

        if (!list_snapshot) return;

        if (pimpl_->event_trace_hook_) pimpl_->event_trace_hook_(EventTracePoint::kEventFired, event_id, nullptr, "SenderFire");

        bool needs_gc = false;
        for (const auto& sub : *list_snapshot) {
            if (sub.active_token && !sub.active_token->load(std::memory_order_acquire)) { needs_gc = true; continue; }
            if (sub.subscriber_id.expired()) { needs_gc = true; continue; }

            if (sub.connection_type == ConnectionType::kDirect) {
                if (pimpl_->event_trace_hook_) pimpl_->event_trace_hook_(EventTracePoint::kDirectCallStart, event_id, nullptr, "DirectCall");
                try { sub.callback(*e_ptr); } catch (const std::exception& e) { ReportException(pimpl_.get(), e); } catch (...) { ReportUnknownException(pimpl_.get()); }
            } else {
                auto token = sub.active_token;
                auto cb = sub.callback;

                PluginManagerPimpl::EventTask task;
                task.event_id = event_id;
                task.func = [e_ptr, cb, token]() {
                    if (token && token->load(std::memory_order_acquire)) cb(*e_ptr);
                    };

                {
                    std::lock_guard<std::mutex> lock(pimpl_->queue_mutex_);
                    pimpl_->event_queue_.push(std::move(task));
                }
                pimpl_->queue_cv_.notify_one();
                if (pimpl_->event_trace_hook_) pimpl_->event_trace_hook_(EventTracePoint::kQueuedEntry, event_id, nullptr, "QueuePush");
            }
        }

        if (needs_gc) {
            ScheduleGC(event_id);
        }
    }

    // 辅助：从列表中物理移除订阅
    static bool RemoveSubscriberFromList(PluginManagerPimpl::SubListPtr& current_ptr, const std::weak_ptr<void>& target_sub) {
        if (!current_ptr) return false;
        auto new_list = std::make_shared<PluginManagerPimpl::SubList>(*current_ptr);
        auto it = std::remove_if(new_list->begin(), new_list->end(),
            [&target_sub](const auto& sub) {
                return !sub.subscriber_id.owner_before(target_sub) && !target_sub.owner_before(sub.subscriber_id);
            });
        if (it != new_list->end()) {
            new_list->erase(it, new_list->end());
            current_ptr = new_list;
            return true;
        }
        return false;
    }

    void PluginManager::Unsubscribe(std::shared_ptr<void> subscriber, EventId event_id, const std::weak_ptr<void>& sender_key) {
        std::lock_guard<std::mutex> lock(pimpl_->subscriber_map_mutex_);
        std::weak_ptr<void> weak_sub = subscriber;
        bool is_global = !sender_key.owner_before(std::weak_ptr<void>()) && !std::weak_ptr<void>().owner_before(sender_key);

        if (is_global) {
            auto it = pimpl_->global_subscribers_.find(event_id);
            if (it != pimpl_->global_subscribers_.end()) RemoveSubscriberFromList(it->second, weak_sub);
            auto look_it = pimpl_->global_sub_lookup_.find(weak_sub);
            if (look_it != pimpl_->global_sub_lookup_.end()) {
                look_it->second.erase(event_id);
                if (look_it->second.empty()) pimpl_->global_sub_lookup_.erase(look_it);
            }
        } else {
            auto sender_it = pimpl_->sender_subscribers_.find(sender_key);
            if (sender_it != pimpl_->sender_subscribers_.end()) {
                auto& event_map = sender_it->second;
                auto evt_it = event_map.find(event_id);
                if (evt_it != event_map.end()) RemoveSubscriberFromList(evt_it->second, weak_sub);
            }
            auto look_it = pimpl_->sender_sub_lookup_.find(weak_sub);
            if (look_it != pimpl_->sender_sub_lookup_.end()) {
                look_it->second.erase({ sender_key, event_id });
                if (look_it->second.empty()) pimpl_->sender_sub_lookup_.erase(look_it);
            }
        }
    }

    void PluginManager::Unsubscribe(std::shared_ptr<void> subscriber) {
        std::lock_guard<std::mutex> lock(pimpl_->subscriber_map_mutex_);
        std::weak_ptr<void> weak_sub = subscriber;

        auto global_it = pimpl_->global_sub_lookup_.find(weak_sub);
        if (global_it != pimpl_->global_sub_lookup_.end()) {
            for (auto eid : global_it->second) {
                auto it = pimpl_->global_subscribers_.find(eid);
                if (it != pimpl_->global_subscribers_.end()) RemoveSubscriberFromList(it->second, weak_sub);
            }
            pimpl_->global_sub_lookup_.erase(global_it);
        }

        auto sender_it = pimpl_->sender_sub_lookup_.find(weak_sub);
        if (sender_it != pimpl_->sender_sub_lookup_.end()) {
            for (const auto& pair : sender_it->second) {
                auto sit = pimpl_->sender_subscribers_.find(pair.first);
                if (sit != pimpl_->sender_subscribers_.end()) {
                    auto eit = sit->second.find(pair.second);
                    if (eit != sit->second.end()) RemoveSubscriberFromList(eit->second, weak_sub);
                }
            }
            pimpl_->sender_sub_lookup_.erase(sender_it);
        }
    }

} // namespace z3y