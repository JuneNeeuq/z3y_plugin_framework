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
 * @file i_event_bus.h
 * @brief 定义 z3y 框架的事件总线接口 IEventBus 和事件基类 Event。
 * @author Yue Liu
 * @date 2025-06-14
 * @copyright Copyright (c) 2025 Yue Liu
 *
 * @details
 * [受众：插件开发者 和 框架使用者]
 *
 * 此文件定义了 `IEventBus` 接口，
 * 这是一个核心服务，用于在插件之间或插件与宿主之间
 * 进行解耦的“发布-订阅”式通信。
 *
 * [使用方法]
 * 1. **定义事件**：创建继承自 `z3y::Event`
 * 并使用 `Z3Y_DEFINE_EVENT` 的结构体。
 * 2. **获取服务**：`auto bus = z3y::GetService<IEventBus>(z3y::clsid::kEventBus);`
 * 3. **订阅事件**：`bus->SubscribeGlobal<MyEvent>(...)`
 * 4. **发布事件**：`bus->FireGlobal<MyEvent>(...)`
 *
 * [核心功能]
 * 1. **订阅模式**：
 * - `Global` (全局)：发布给所有订阅者。
 * - `Sender-Specific` (特定发布者)：仅发布给订阅了 *特定* 实例的订阅者。
 * 2. **连接类型**：
 * - `kDirect` (直接)：同步。回调在 `Fire` 调用的 *同一线程* 上立即执行。
 * - `kQueued` (队列)：异步。回调被放入 `PluginManager`
 * 的工作线程队列中稍后执行。
 */

#pragma once

#ifndef Z3Y_FRAMEWORK_I_EVENT_BUS_H_
#define Z3Y_FRAMEWORK_I_EVENT_BUS_H_

#include <functional>  // 用于 std::function, std::invoke
#include <memory>      // 用于 std::shared_ptr, enable_shared_from_this
#include <typeindex>   // 用于 std::type_index
#include <utility>     // 用于 std::forward
#include "framework/class_id.h"         // 依赖 EventId
#include "framework/connection_type.h"  // 依赖 ConnectionType
#include "framework/i_component.h"      // 依赖 IComponent, PluginPtr
#include "framework/interface_helpers.h"  // 依赖 Z3Y_DEFINE_INTERFACE
#include "framework/connection.h"  // 依赖 Connection (作为返回值)

namespace z3y {

    /**
     * @struct Event
     * @brief 所有事件类型的基类。
     *
     * [受众：插件开发者]
     * 你定义的所有自定义事件结构体都必须继承自 `z3y::Event`。
     *
     * @see Z3Y_DEFINE_EVENT
     * @example
     * \code{.cpp}
     * struct MyCustomEvent : public z3y::Event {
     * // [!!] 必须使用 Z3Y_DEFINE_EVENT 宏
     * Z3Y_DEFINE_EVENT(MyCustomEvent, "my-event-uuid-string")
     *
     * int some_data;
     * MyCustomEvent(int d) : data(d) {}
     * };
     * \endcode
     */
    struct Event {
        virtual ~Event() = default;
    };

    /**
     * @class IEventBus
     * @brief [核心服务] 框架的事件总线接口。
     *
     * [受众：插件开发者 和 框架使用者]
     *
     * `IEventBus` 是一个单例服务。
     * 请使用 `z3y::GetService<IEventBus>(clsid::kEventBus)`
     * 或全局辅助函数 `z3y::SubscribeGlobalEvent(...)` 来访问。
     */
    class IEventBus : public virtual IComponent {
    public:
        Z3Y_DEFINE_INTERFACE(IEventBus, "z3y-core-IEventBus-IID-A0000002", 1, 0)

            virtual ~IEventBus() = default;

        // --- 1. 全局订阅 (Global Broadcast) ---

        /**
         * @brief 订阅一个全局广播事件。
         *
         * [受众：插件开发者]
         *
         * @tparam TEvent 要订阅的事件类型 (例如 `MyCustomEvent`)。
         * @tparam TSubscriber 订阅者的类类型 (例如 `MyClass`)。
         * @tparam TCallback 订阅者的成员函数指针 (例如 `&MyClass::OnEvent`)。
         *
         * @param[in] subscriber 订阅者实例的 `shared_ptr` (通常传入
         * `shared_from_this()`)。
         * @param[in] callback 指向订阅者成员函数的回调。
         * @param[in] type 连接类型 (`kDirect` 或 `kQueued`)。
         *
         * @return 一个 `z3y::Connection` 对象。
         *
         * [警告：生命周期]
         * 返回的 `Connection` 对象必须被妥善管理。
         * 推荐使用 `z3y::ScopedConnection` (RAII)
         * 成员变量来存储它，以确保在订阅者析构时自动取消订阅。
         *
         * @example
         * \code{.cpp}
         * class MyClass : public z3y::PluginImpl<...> {
         * z3y::ScopedConnection my_conn_; // [!!] 推荐使用 RAII
         *
         * void Initialize() override {
         * auto bus = z3y::GetService<IEventBus>(clsid::kEventBus);
         * my_conn_ = bus->SubscribeGlobal<MyEvent>(
         * shared_from_this(),
         * &MyClass::OnMyEvent,
         * z3y::ConnectionType::kQueued
         * );
         * }
         *
         * void OnMyEvent(const MyEvent& e) { ... }
         *
         * // 当 MyClass 析构时，my_conn_ 会自动调用 Disconnect()
         * };
         * \endcode
         */
        template <typename TEvent, typename TSubscriber, typename TCallback>
        [[nodiscard]]  // 提示用户必须管理 Connection 的生命周期
        Connection SubscribeGlobal(std::shared_ptr<TSubscriber> subscriber,
            TCallback&& callback,
            ConnectionType type = ConnectionType::kDirect) {
            // 静态断言检查类型安全
            static_assert(std::is_base_of_v<Event, TEvent>,
                "TEvent must derive from z3y::Event");
            static_assert(
                std::is_base_of_v<std::enable_shared_from_this<TSubscriber>,
                TSubscriber>,
                "Subscriber must inherit from std::enable_shared_from_this");

            EventId event_id = TEvent::kEventId;
            std::weak_ptr<TSubscriber> weak_sub = subscriber;

            // [受众：框架维护者]
            // 创建一个类型擦除的包装器 (lambda)，捕获 weak_ptr 和成员函数。
            // 当事件触发时，此 lambda 被调用。
            std::function<void(const Event&)> wrapper =
                [weak_sub, cb = std::forward<TCallback>(callback)](const Event& e) {
                // 检查订阅者是否仍然存活
                if (auto sub = weak_sub.lock()) {
                    // 存活，则安全地调用其成员函数
                    // (使用 C++17 的 std::invoke 来安全调用成员函数指针)
                    std::invoke(cb, sub.get(), static_cast<const TEvent&>(e));
                }
                };
            std::weak_ptr<void> weak_id = subscriber;
            // 调用纯虚的底层实现
            return SubscribeGlobalImpl(event_id, std::move(weak_id), std::move(wrapper),
                type);
        }

        /**
         * @brief 发布一个全局广播事件。
         *
         * [受众：插件开发者 和 框架使用者]
         *
         * @tparam TEvent 要发布的事件类型 (例如 `MyCustomEvent`)。
         * @tparam Args 事件 `TEvent` 构造函数所需的参数。
         *
         * @param[in] args 用于在 `std::make_shared` 中构造 `TEvent` 实例的参数。
         *
         * @example
         * \code{.cpp}
         * // 假设 MyEvent(int, std::string)
         * bus->FireGlobal<MyEvent>(10, "hello");
         *
         * // 或使用全局辅助函数 (在 z3y_service_locator.h 中定义)
         * z3y::FireGlobalEvent<MyEvent>(10, "hello");
         * \endcode
         */
        template <typename TEvent, typename... Args>
        void FireGlobal(Args&&... args) {
            static_assert(std::is_base_of_v<Event, TEvent>,
                "TEvent must derive from z3y::Event");
            EventId event_id = TEvent::kEventId;

            // [受众：框架维护者]
            // 优化：如果没有任何订阅者，则不构造事件对象，避免不必要的内存分配。
            if (!this->IsGlobalSubscribed(event_id)) {
                return;
            }
            // 构造事件对象 (通过 PluginPtr/shared_ptr 管理生命周期)
            PluginPtr<TEvent> event_ptr =
                std::make_shared<TEvent>(std::forward<Args>(args)...);
            PluginPtr<Event> base_event = event_ptr;
            // 调用纯虚的底层实现
            FireGlobalImpl(event_id, base_event);
        }

        // --- 2. 特定发布者 (Sender-Specific) ---

        /**
         * @brief 仅订阅 *特定* 发布者实例所发布的事件。
         *
         * [受众：插件开发者]
         *
         * 当你只关心某个特定实例（例如 `window_close_button`）
         * 而不是所有实例（例如 *所有* `button`）
         * 发布的事件时，使用此方法。
         *
         * @param[in] sender 要“监听”的发布者实例 (必须是 `PluginPtr<IComponent>`)。
         * @param[in] subscriber 订阅者实例的 `shared_ptr` (通常 `shared_from_this()`)。
         * @param[in] callback 回调的成员函数指针。
         * @param[in] type 连接类型 (`kDirect` 或 `kQueued`)。
         *
         * @return 一个 `z3y::Connection` 对象 (推荐使用 `ScopedConnection` 管理)。
         */
        template <typename TEvent, typename TSubscriber, typename TCallback>
        [[nodiscard]]
        Connection SubscribeToSender(PluginPtr<IComponent> sender,
            std::shared_ptr<TSubscriber> subscriber,
            TCallback&& callback,
            ConnectionType type = ConnectionType::kDirect) {
            static_assert(std::is_base_of_v<Event, TEvent>,
                "TEvent must derive from z3y::Event");
            static_assert(
                std::is_base_of_v<std::enable_shared_from_this<TSubscriber>,
                TSubscriber>,
                "Subscriber must inherit from std::enable_shared_from_this");

            EventId event_id = TEvent::kEventId;
            std::weak_ptr<TSubscriber> weak_sub = subscriber;

            // [受众：框架维护者] 创建类型擦除的包装器
            std::function<void(const Event&)> wrapper =
                [weak_sub, cb = std::forward<TCallback>(callback)](const Event& e) {
                if (auto sub = weak_sub.lock()) {
                    std::invoke(cb, sub.get(), static_cast<const TEvent&>(e));
                }
                };

            std::weak_ptr<void> weak_sub_id = subscriber;
            std::weak_ptr<void> weak_sender_id = sender;

            // [受众：框架维护者]
            // 内部使用 weak_ptr 作为 map 的 key
            return SubscribeToSenderImpl(event_id, std::move(weak_sub_id),
                std::move(weak_sender_id), std::move(wrapper),
                type);
        }

        /**
         * @brief 作为特定发布者发布一个事件。
         *
         * [受众：插件开发者]
         *
         * 只有通过 `SubscribeToSender(sender, ...)`
         * 订阅了此 `sender` 实例的订阅者才会收到此事件。
         *
         * @param[in] sender 发布者实例 (通常传入 `shared_from_this()`)。
         * @param[in] args 用于构造 `TEvent` 实例的参数。
         *
         * @example
         * \code{.cpp}
         * // 在 MySenderClass 内部 (必须继承自 PluginImpl)
         * void MySenderClass::DoSomething() {
         * auto bus = z3y::GetService<IEventBus>(clsid::kEventBus);
         * bus->FireToSender<MySenderEvent>(
         * shared_from_this(), // [!!] 关键：传入自己
         * 42
         * );
         * }
         * \endcode
         */
        template <typename TEvent, typename... Args>
        void FireToSender(PluginPtr<IComponent> sender, Args&&... args) {
            static_assert(std::is_base_of_v<Event, TEvent>,
                "TEvent must derive from z3y::Event");

            EventId event_id = TEvent::kEventId;
            // [受众：框架维护者] 内部使用 weak_ptr 作为 key
            std::weak_ptr<void> weak_sender_id = sender;

            // [受众：框架维护者] 优化：检查是否有订阅者
            if (!this->IsSenderSubscribed(weak_sender_id, event_id)) {
                return;
            }
            PluginPtr<TEvent> event_ptr =
                std::make_shared<TEvent>(std::forward<Args>(args)...);
            PluginPtr<Event> base_event = event_ptr;
            FireToSenderImpl(std::move(weak_sender_id), event_id, base_event);
        }

        // --- 3. 取消订阅 ---

        /**
         * @brief 取消该订阅者 *所有* 的订阅 (包括全局和特定发布者)。
         *
         * [受众：插件开发者]
         *
         * **警告：这是一个重量级操作，应避免使用。**
         *
         * 推荐使用 `z3y::ScopedConnection` (RAII)
         * 或 `z3y::Connection::Disconnect()`
         * 来精确管理订阅的生命周期，而不是调用此函数。
         *
         * @param[in] subscriber 订阅者实例的 `shared_ptr`。
         */
        virtual void Unsubscribe(std::shared_ptr<void> subscriber) = 0;

        /**
         * @brief [框架内部] 取消一个 *特定* 的订阅。
         *
         * [受众：插件开发者]
         * **警告：不要直接调用此函数。**
         * 此函数由 `z3y::Connection::Disconnect()` 内部调用。
         *
         * [受众：框架维护者]
         * `PluginManager` 必须实现此函数。
         *
         * @param[in] subscriber 订阅者实例。
         * @param[in] event_id 事件 ID。
         * @param[in] sender_key
         * 如果是全局订阅，则为 `expired()`；
         * 如果是特定发布者订阅，则为发布者的 `weak_ptr`。
         */
        virtual void Unsubscribe(std::shared_ptr<void> subscriber, EventId event_id,
            const std::weak_ptr<void>& sender_key =
            std::weak_ptr<void>()) = 0;

    protected:
        // --- [受众：框架维护者] 纯虚函数 (由 PluginManager 实现) ---

        /**
         * @brief [纯虚] 检查是否有任何活动的全局订阅。
         * @note `const`成员函数，用于 `FireGlobal` 中的无锁/读锁优化。
         */
        [[nodiscard]] virtual bool IsGlobalSubscribed(EventId event_id) const = 0;

        /**
         * @brief [纯虚] 检查是否有任何活动的特定发布者订阅。
         * @note `const` 成员函数，(使用 `weak_ptr` 作为 key)。
         */
        [[nodiscard]] virtual bool IsSenderSubscribed(
            const std::weak_ptr<void>& sender_id, EventId event_id) const = 0;

        /**
         * @brief [纯虚] `SubscribeGlobal` 的底层实现。
         */
        [[nodiscard]] virtual Connection SubscribeGlobalImpl(
            EventId event_id, std::weak_ptr<void> sub,
            std::function<void(const Event&)> cb,
            ConnectionType connection_type) = 0;

        /**
         * @brief [纯虚] `FireGlobal` 的底层实现。
         */
        virtual void FireGlobalImpl(EventId event_id, PluginPtr<Event> e_ptr) = 0;

        /**
         * @brief [纯虚] `SubscribeToSender` 的底层实现。
         */
        [[nodiscard]] virtual Connection SubscribeToSenderImpl(
            EventId event_id, std::weak_ptr<void> sub_id,
            std::weak_ptr<void> sender_id, std::function<void(const Event&)> cb,
            ConnectionType connection_type) = 0;

        /**
         * @brief [纯虚] `FireToSender` 的底层实现。
         */
        virtual void FireToSenderImpl(const std::weak_ptr<void>& sender_id,
            EventId event_id, PluginPtr<Event> e_ptr) = 0;
    };

    /**
     * @brief `IEventBus` 服务的全局唯一 ClassId。
     *
     * [受众：插件开发者 和 框架使用者]
     * 在调用 `z3y::GetService<IEventBus>(...)` 时使用此 ID。
     */
    namespace clsid {
        constexpr ClassId kEventBus =
            ConstexprHash("z3y-core-event-bus-SERVICE-UUID-D54E82F1");
    }  // namespace clsid

}  // namespace z3y

#endif  // Z3Y_FRAMEWORK_I_EVENT_BUS_H_