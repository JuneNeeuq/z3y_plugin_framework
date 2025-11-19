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
 * @file framework/connection.h
 * @brief 定义 z3y::Connection 和 z3y::ScopedConnection 类。
 * @author Yue Liu
 * @date 2025-06-15
 * @copyright Copyright (c) 2025 Yue Liu
 *
 * @details
 * [受众：插件开发者]
 *
 * 此文件定义了用于管理事件订阅生命周期的句柄类。
 *
 * 1. `z3y::Connection`：
 * - `IEventBus::Subscribe...` 函数返回此对象。
 * - 这是一个“手动”句柄，必须调用 `Disconnect()` 才能取消订阅。
 *
 * 2. `z3y::ScopedConnection`：
 * - **[推荐]** 这是一个 RAII 包装器，
 * 它会在其析构时 *自动* 调用 `Disconnect()`。
 * - 你应该在你的类中使用它作为成员变量，
 * 以确保订阅的生命周期与你的对象绑定。
 */

#pragma once

#ifndef Z3Y_FRAMEWORK_CONNECTION_H_
#define Z3Y_FRAMEWORK_CONNECTION_H_

#include <memory>  // 用于 std::weak_ptr, std::shared_ptr
#include "framework/class_id.h"         // 依赖 EventId
#include "framework/z3y_framework_api.h"  // Z3Y_FRAMEWORK_API 导出

namespace z3y {

    // [受众：框架维护者]
    // 前向声明 IEventBus，以避免 #include "i_event_bus.h" 导致的循环头文件依赖。
    // (connection.cpp 必须包含 i_event_bus.h 完整定义)
    class IEventBus;

    /**
     * @class Connection
     * @brief 一个轻量级的事件订阅句柄。
     *
     * [受众：插件开发者]
     *
     * `IEventBus::Subscribe...` 函数返回此对象，代表一个已建立的订阅连接。
     *
     * @note
     * **警告：** 此类是“移动语义”(Move-Only)，
     * 且 *不会* 自动断开连接。
     * 强烈推荐使用 `ScopedConnection` 来代替。
     *
     * @see ScopedConnection (推荐的 RAII 包装器)
     */
    class Z3Y_FRAMEWORK_API Connection {
    public:
        /**
         * @brief 默认构造函数。
         * @details 创建一个无效的、"已断开"的连接。
         */
        Connection() = default;

        /**
         * @brief [核心] 手动断开此订阅连接。
         *
         * [受众：框架维护者]
         * 此函数内部会提升 `weak_ptr` 并调用
         * `IEventBus::Unsubscribe(subscriber, event_id, sender_key)`。
         */
        void Disconnect();

        /**
         * @brief 检查此连接句柄是否仍然有效。
         * @return `true` 如果连接尚未被 `Disconnect()`，
         * 且 `IEventBus` 和订阅者仍然存活。
         */
        bool IsConnected() const;

        // --- [受众：框架维护者] 移动语义 ---
        Connection(const Connection&) = delete;
        Connection& operator=(const Connection&) = delete;
        Connection(Connection&&) = default;
        Connection& operator=(Connection&&) = default;

    private:
        // [受众：框架维护者]
        // 仅允许 IEventBus (PluginManager) 构造有效的 Connection
        friend class IEventBus;
        friend class PluginManager;  // PluginManager 实现了 IEventBus

        /**
         * @brief [内部] 构造函数，由 `IEventBus` 实现调用。
         *
         * [受众：框架维护者]
         * 这是一个 *非内联* 声明。
         * 其 *定义* (实现) 位于 `src/z3y_plugin_manager/connection.cpp`
         * 中。
         */
        Connection(std::weak_ptr<IEventBus> bus, std::weak_ptr<void> subscriber,
            EventId event_id, std::weak_ptr<void> sender_key);

        std::weak_ptr<IEventBus> bus_;
        std::weak_ptr<void> subscriber_;
        EventId event_id_ = 0;
        std::weak_ptr<void> sender_key_;
        bool is_connected_ = false;
    };

    /**
     * @class ScopedConnection
     * @brief [推荐] `Connection` 的 RAII (C++ 作用域) 包装器。
     *
     * [受众：插件开发者]
     *
     * 这是一个非常方便的工具类，用于自动管理订阅的生命周期。
     * 当 `ScopedConnection` 对象离开其作用域 (例如函数返回、
     * 或持有它的类实例被析构) 时，
     * 它会自动调用其内部 `Connection` 的 `Disconnect()` 方法。
     *
     * [使用方法]
     * 在你的类中添加一个 `ScopedConnection` 成员变量。
     *
     * @example
     * \code{.cpp}
     * class MyClass : public z3y::PluginImpl<...> {
     * z3y::ScopedConnection my_event_conn_; // [!!] 成员变量
     *
     * void Initialize() override {
     * auto bus = z3y::GetService<IEventBus>(clsid::kEventBus);
     * // 订阅事件，并将返回的 Connection 赋值给 ScopedConnection
     * my_event_conn_ = bus->SubscribeGlobal<MyEvent>(
     * shared_from_this(), &MyClass::OnMyEvent
     * );
     * }
     *
     * // 当 MyClass 实例析构时，
     * // my_event_conn_ 的析构函数会被调用，
     * // 从而自动 Disconnect() 订阅。
     * };
     * \endcode
     */
    class ScopedConnection {
    public:
        ScopedConnection() = default;
        ScopedConnection(Connection connection) : conn_(std::move(connection)) {}
        ~ScopedConnection() { conn_.Disconnect(); }

        // 禁用拷贝
        ScopedConnection(const ScopedConnection&) = delete;
        ScopedConnection& operator=(const ScopedConnection&) = delete;

        // 启用移动
        ScopedConnection(ScopedConnection&& other) noexcept
            : conn_(std::move(other.conn_)) {
        }

        ScopedConnection& operator=(ScopedConnection&& other) noexcept {
            if (this != &other) {
                conn_.Disconnect();          // 断开当前持有的连接
                conn_ = std::move(other.conn_);  // 接管新的连接
            }
            return *this;
        }

        /**
         * @brief 手动提前断开连接。
         */
        void Disconnect() { conn_.Disconnect(); }

    private:
        Connection conn_;
    };

}  // namespace z3y

#endif  // Z3Y_FRAMEWORK_CONNECTION_H_