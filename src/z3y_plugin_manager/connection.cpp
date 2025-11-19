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
 *
 * @details
 * [受众：框架维护者]
 *
 * 此文件是 `z3y_plugin_manager` 库的一部分。
 * 它提供了 `framework/connection.h` 中声明的
 * `Connection` 类非内联成员函数的定义。
 *
 * [设计思想：Pimpl 与 ABI]
 * `Connection` 类本身 *不是* Pimpl 模式，但它的实现被分离到 `.cpp` 文件中，
 * 是为了 *隐藏* `framework/connection.h` (公共头文件)
 * 对 `framework/i_event_bus.h` 完整定义的依赖，
 * 从而打破头文件循环。
 *
 * `connection.h` 只需前向声明 `IEventBus`。
 * 此 `.cpp` 文件可以安全地 `#include "framework/i_event_bus.h"`
 * 来获取 `IEventBus::Unsubscribe` 的完整定义。
 */
#include "framework/connection.h"
#include "framework/i_event_bus.h"  // [核心] 包含 IEventBus 完整定义

namespace z3y {

    /**
     * @brief Connection 私有构造函数的定义 (实现)。
     *
     * [受众：框架维护者]
     *
     * 此函数由 `PluginManager` (作为 `friend`) 在 `Subscribe...Impl` 中调用。
     * 它初始化所有成员变量， 并将 `is_connected_` 标记为 `true`。
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
        // 构造函数体为空
    }

    /**
     * @brief [API] 手动断开此订阅连接。
     *
     * [受众：框架维护者]
     *
     * `Disconnect` 的核心逻辑：
     * 1. 检查 `is_connected_` 标志。
     * 2. 尝试 `lock()` `bus_` (IEventBus) 和 `subscriber_` (订阅者) 的 `weak_ptr`。
     * 3. 如果两者都 *仍然存活*，则调用 `strong_bus->Unsubscribe(...)` (内部的、特定于 Connection 的版本)，
     * 传入 `event_id_` 和 `sender_key_`。
     */
    void Connection::Disconnect() {
        if (!is_connected_) {
            return; // 已经断开
        }
        // 立即标记为断开，防止重入
        is_connected_ = false;

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
        // 我们无需执行任何操作，
        // 因为 Lazy GC (在 event_bus_impl.cpp 中)
        // 会在下一次 Fire 时自动清理这个订阅。
    }

    /**
     * @brief [API] 检查此连接句柄是否仍然有效。
     */
    bool Connection::IsConnected() const {
        // `is_connected_`
        // 标志必须为 true，且 bus 和 subscriber
        // 都必须尚未过期。
        return is_connected_ && !bus_.expired() && !subscriber_.expired();
    }

}  // namespace z3y