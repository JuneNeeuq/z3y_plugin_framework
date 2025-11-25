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
 * @file connection.cpp
 * @brief Connection 类的实现细节。
 *
 * @details
 * 这里实现了 Connection 的核心逻辑，特别是 `Disconnect`。
 * 重点在于如何安全地“撕毁”票据。
 */

#include "framework/connection.h"
#include "framework/i_event_bus.h"

namespace z3y {

    /**
     * @brief 构造函数。
     * 初始化所有状态。注意我们接管了 `active_token` 的 shared_ptr。
     */
    Connection::Connection(std::weak_ptr<IEventBus> bus,
        std::weak_ptr<void> subscriber, EventId event_id,
        std::weak_ptr<void> sender_key,
        std::shared_ptr<std::atomic<bool>> active_token)
        : bus_(std::move(bus)),
        subscriber_(std::move(subscriber)),
        event_id_(event_id),
        sender_key_(std::move(sender_key)),
        is_connected_(true),
        active_token_(std::move(active_token)) {
    }

    /**
     * @brief 移动构造函数。
     * 资源转移。
     */
    Connection::Connection(Connection&& other) noexcept
        : bus_(std::move(other.bus_)),
        subscriber_(std::move(other.subscriber_)),
        event_id_(other.event_id_),
        sender_key_(std::move(other.sender_key_)),
        active_token_(std::move(other.active_token_)) {
        // 原子地将对方标记为未连接，并接管状态
        bool was_connected = other.is_connected_.exchange(false);
        is_connected_.store(was_connected);
    }

    /**
     * @brief 移动赋值函数。
     */
    Connection& Connection::operator=(Connection&& other) noexcept {
        if (this != &other) {
            Disconnect(); // 先断开自己当前的
            bus_ = std::move(other.bus_);
            subscriber_ = std::move(other.subscriber_);
            event_id_ = other.event_id_;
            sender_key_ = std::move(other.sender_key_);
            active_token_ = std::move(other.active_token_);
            bool was_connected = other.is_connected_.exchange(false);
            is_connected_.store(was_connected);
        }
        return *this;
    }

    /**
     * @brief 断开连接 (核心实现)。
     *
     * @details
     * 1. `exchange(false)`: 原子地检查并设置状态，防止多线程重复调用 Disconnect。
     * 2. `active_token_->store(false)`: **这是最关键的一步！**
     * 这个 `active_token_` 是和 EventBus 共享的内存。
     * 只要把它置为 false，所有持有着这个 Token 的待执行回调（无论在队列里还是刚取出来）
     * 在执行前检查时都会发现 false，从而停止执行。
     * 使用了 `memory_order_release` 保证写入对其他线程可见。
     * 3. `strong_bus->Unsubscribe(...)`: 通知 Bus 做物理删除。这一步即使慢一点也没关系，
     * 因为逻辑上已经断开了。
     */
    void Connection::Disconnect() {
        if (!is_connected_.exchange(false)) return;

        // [撕票] 强一致性断开
        if (active_token_) {
            active_token_->store(false, std::memory_order_release);
        }

        // [物理清理] 尝试锁定 Bus 并移除记录
        if (auto strong_bus = bus_.lock()) {
            if (auto strong_sub = subscriber_.lock()) {
                strong_bus->Unsubscribe(strong_sub, event_id_, sender_key_);
            }
        }
        active_token_.reset(); // 释放我对 token 的引用
    }

    bool Connection::IsConnected() const {
        // 连接有效的条件：自身标记为 true + Bus 还在 + 订阅者还在
        return is_connected_.load() && !bus_.expired() && !subscriber_.expired();
    }

}  // namespace z3y