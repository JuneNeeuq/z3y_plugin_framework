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
 * @brief z3y::Connection 类的实现。
 * @author Yue Liu
 * @date 2025-07-13
 * @copyright Copyright (c) 2025 Yue Liu
 */
#include "framework/connection.h"
#include "framework/i_event_bus.h"  // [核心] 包含 IEventBus 完整定义

namespace z3y {

    /**
     * @brief Connection 私有构造函数的定义 (实现)。
     */
    Connection::Connection(std::weak_ptr<IEventBus> bus,
        std::weak_ptr<void> subscriber, EventId event_id,
        std::weak_ptr<void> sender_key)
        : bus_(std::move(bus)),
        subscriber_(std::move(subscriber)),
        event_id_(event_id),
        sender_key_(std::move(sender_key)),
        is_connected_(true)  // 标记为已连接
    {
    }

    // --- [新增] 手动实现移动语义 ---

    Connection::Connection(Connection&& other) noexcept
        : bus_(std::move(other.bus_)),
        subscriber_(std::move(other.subscriber_)),
        event_id_(other.event_id_),
        sender_key_(std::move(other.sender_key_)) {
        // [核心] 原子地接管状态，并将源对象标记为断开
        // exchange 返回旧值 (即源对象的连接状态)，并将其设为 false
        bool was_connected = other.is_connected_.exchange(false);
        is_connected_.store(was_connected);
    }

    Connection& Connection::operator=(Connection&& other) noexcept {
        if (this != &other) {
            // 1. 先断开当前的连接 (如果已连接)
            Disconnect();

            // 2. 转移资源所有权
            bus_ = std::move(other.bus_);
            subscriber_ = std::move(other.subscriber_);
            event_id_ = other.event_id_;
            sender_key_ = std::move(other.sender_key_);

            // 3. [核心] 原子转移连接状态
            bool was_connected = other.is_connected_.exchange(false);
            is_connected_.store(was_connected);
        }
        return *this;
    }

    /**
     * @brief [API] 手动断开此订阅连接。
     */
    void Connection::Disconnect() {
        // [核心优化] 使用 exchange 保证 CAS (Compare-And-Swap) 原子性
        // 只有当 is_connected_ 原本为 true 时，exchange 才返回 true 并将其置为 false。
        // 如果多个线程同时调用，只有一个线程会进入 if 块。
        if (!is_connected_.exchange(false)) {
            return; // 已经断开，直接返回
        }

        // 1. 尝试提升 EventBus
        if (auto strong_bus = bus_.lock()) {
            // 2. 尝试提升订阅者
            if (auto strong_sub = subscriber_.lock()) {
                // 3. [核心]
                //    两者都存活，可以安全地调用 IEventBus
                //    的内部 Unsubscribe 方法
                strong_bus->Unsubscribe(strong_sub, event_id_, sender_key_);
            }
        }
        // 如果 bus_ 或 subscriber_ 已经过期，
        // 我们无需执行任何操作，Lazy GC 会处理。
    }

    /**
     * @brief [API] 检查此连接句柄是否仍然有效。
     */
    bool Connection::IsConnected() const {
        // 使用 load() 原子读取状态
        return is_connected_.load() && !bus_.expired() && !subscriber_.expired();
    }

}  // namespace z3y