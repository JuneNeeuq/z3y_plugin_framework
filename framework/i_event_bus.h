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
 * @brief [核心接口] 定义事件总线 IEventBus 和事件基类 Event。
 * @author Yue Liu
 * @date 2025
 *
 * @details
 * **文件作用：**
 * 此文件定义了插件间通信的协议。
 * `IEventBus` 是一个纯虚接口，定义了如何发布 (Fire) 和订阅 (Subscribe) 事件。
 *
 * **核心概念：**
 * 1. **事件 (Event)**: 一个纯数据结构 (struct)，携带信息。
 * 2. **发布者 (Sender)**: 产生事件的组件。
 * 3. **订阅者 (Subscriber)**: 对事件感兴趣并处理它的组件。
 *
 * **通信模式：**
 * - **Global (全局)**: 喊话模式。发布者大喊一声，所有感兴趣的人都能听到。
 * - **Sender (单播)**: 专线模式。订阅者只关心“张三”发出的消息，不关心“李四”发出的同类型消息。
 */

#pragma once

#ifndef Z3Y_FRAMEWORK_I_EVENT_BUS_H_
#define Z3Y_FRAMEWORK_I_EVENT_BUS_H_

#include <functional>
#include <memory>
#include <typeindex>
#include <utility>
#include "framework/class_id.h"
#include "framework/connection_type.h"
#include "framework/i_component.h"
#include "framework/interface_helpers.h"
#include "framework/connection.h"

namespace z3y {

    /**
     * @struct Event
     * @brief 自定义事件的基类。
     *
     * @details
     * **设计思想：**
     * 所有的事件都必须继承自这个类。这允许我们在底层使用多态来传递事件指针。
     * 实际上，事件的分发是基于 `EventId` (整数) 的，这比 RTTI 更快。
     *
     * **使用方法：**
     * ```cpp
     * struct MyEvent : public z3y::Event {
     * Z3Y_DEFINE_EVENT(MyEvent, "uuid-string-..."); // 必须有宏
     * int data;
     * };
     * ```
     */
    struct Event {
        virtual ~Event() = default;
    };

    /**
     * @class IEventBus
     * @brief 事件总线接口。提供发布-订阅功能。
     */
    class IEventBus : public virtual IComponent {
    public:
        // 定义接口 ID
        Z3Y_DEFINE_INTERFACE(IEventBus, "z3y-core-IEventBus-IID-A0000002", 1, 0)

            virtual ~IEventBus() = default;

        // =========================================================================
        // 1. 全局订阅 (Global Broadcast)
        // =========================================================================

        /**
         * @brief 订阅全局事件（模板辅助函数）。
         *
         * @tparam TEvent 事件类型 (例如 MyEvent)。
         * @tparam TSubscriber 订阅者类型 (例如 MyPlugin)。
         * @tparam TCallback 回调函数类型 (成员函数指针)。
         *
         * @param subscriber 订阅者对象的 shared_ptr。**必须继承自 enable_shared_from_this**。
         * @param callback 回调函数指针 (如 &MyPlugin::OnEvent)。
         * @param type 连接类型 (默认 kDirect)。
         * @return Connection 对象，用于断开连接。
         *
         * @note 这是一个模板包装器，它会自动提取事件 ID，并将成员函数转换为 lambda，
         * 然后调用底层的 `SubscribeGlobalImpl`。
         */
        template <typename TEvent, typename TSubscriber, typename TCallback>
        [[nodiscard]]
        Connection SubscribeGlobal(std::shared_ptr<TSubscriber> subscriber,
            TCallback&& callback,
            ConnectionType type = ConnectionType::kDirect) {
            // 编译期检查：确保类型正确
            static_assert(std::is_base_of_v<Event, TEvent>, "TEvent must derive from z3y::Event");
            static_assert(std::is_base_of_v<std::enable_shared_from_this<TSubscriber>, TSubscriber>,
                "Subscriber must inherit from std::enable_shared_from_this");

            EventId event_id = TEvent::kEventId;
            std::weak_ptr<TSubscriber> weak_sub = subscriber;

            // 构造类型擦除的包装器
            // 在调用用户回调前，先 lock() 弱引用，确保对象还活着
            std::function<void(const Event&)> wrapper =
                [weak_sub, cb = std::forward<TCallback>(callback)](const Event& e) {
                if (auto sub = weak_sub.lock()) {
                    std::invoke(cb, sub.get(), static_cast<const TEvent&>(e));
                }
                };

            return SubscribeGlobalImpl(event_id, subscriber, std::move(wrapper), type);
        }

        /**
         * @brief 发布全局事件。
         *
         * @tparam TEvent 事件类型。
         * @tparam Args 构造事件所需的参数。
         */
        template <typename TEvent, typename... Args>
        void FireGlobal(Args&&... args) {
            static_assert(std::is_base_of_v<Event, TEvent>, "TEvent must derive from z3y::Event");
            EventId event_id = TEvent::kEventId;

            // 性能优化：如果没有人订阅这个事件，就不构造对象了
            if (!this->IsGlobalSubscribed(event_id)) {
                return;
            }
            // 构造事件对象
            PluginPtr<TEvent> event_ptr = std::make_shared<TEvent>(std::forward<Args>(args)...);
            // 转发给实现层
            FireGlobalImpl(event_id, event_ptr);
        }

        // =========================================================================
        // 2. 特定发布者订阅 (Sender-Specific)
        // =========================================================================

        /**
         * @brief 订阅特定发送者的事件。
         *
         * @param sender 我只关心这个对象发出的事件。
         * @param subscriber 我是订阅者。
         * @param callback 我的回调函数。
         */
        template <typename TEvent, typename TSubscriber, typename TCallback>
        [[nodiscard]]
        Connection SubscribeToSender(PluginPtr<IComponent> sender,
            std::shared_ptr<TSubscriber> subscriber,
            TCallback&& callback,
            ConnectionType type = ConnectionType::kDirect) {
            static_assert(std::is_base_of_v<Event, TEvent>, "TEvent must derive from z3y::Event");
            static_assert(std::is_base_of_v<std::enable_shared_from_this<TSubscriber>, TSubscriber>,
                "Subscriber must inherit from std::enable_shared_from_this");

            EventId event_id = TEvent::kEventId;
            std::weak_ptr<TSubscriber> weak_sub = subscriber;

            std::function<void(const Event&)> wrapper =
                [weak_sub, cb = std::forward<TCallback>(callback)](const Event& e) {
                if (auto sub = weak_sub.lock()) {
                    std::invoke(cb, sub.get(), static_cast<const TEvent&>(e));
                }
                };

            return SubscribeToSenderImpl(event_id, subscriber, sender, std::move(wrapper), type);
        }

        /**
         * @brief 作为特定发送者发布事件。
         * @param sender "我" 是谁。
         */
        template <typename TEvent, typename... Args>
        void FireToSender(PluginPtr<IComponent> sender, Args&&... args) {
            static_assert(std::is_base_of_v<Event, TEvent>, "TEvent must derive from z3y::Event");

            EventId event_id = TEvent::kEventId;
            std::weak_ptr<void> weak_sender_id = sender;

            // 性能优化：检查是否有针对"我"的订阅
            if (!this->IsSenderSubscribed(weak_sender_id, event_id)) {
                return;
            }
            PluginPtr<TEvent> event_ptr = std::make_shared<TEvent>(std::forward<Args>(args)...);
            FireToSenderImpl(std::move(weak_sender_id), event_id, event_ptr);
        }

        // =========================================================================
        // 3. 状态查询与清理
        // =========================================================================

        /**
         * @brief [查询] 检查是否有任何活动的全局订阅。
         * @details 这是一个“无锁读”或极低开销的操作。建议在 Fire 之前调用。
         */
        [[nodiscard]] virtual bool IsGlobalSubscribed(EventId event_id) const = 0;

        /**
         * @brief [查询] 检查是否有针对特定发布者的订阅。
         */
        [[nodiscard]] virtual bool IsSenderSubscribed(
            const std::weak_ptr<void>& sender_id, EventId event_id) const = 0;

        /**
         * @brief 取消某订阅者的所有订阅。
         * @deprecated 不推荐使用。请优先使用 Connection::Disconnect。
         */
        virtual void Unsubscribe(std::shared_ptr<void> subscriber) = 0;

        /**
         * @brief [内部使用] 细粒度取消订阅。由 Connection 类调用。
         */
        virtual void Unsubscribe(std::shared_ptr<void> subscriber, EventId event_id,
            const std::weak_ptr<void>& sender_key = std::weak_ptr<void>()) = 0;

    protected:
        // --- 纯虚实现接口 (Implementation Detail) ---
        // 真正的逻辑在 PluginManager 中实现

        [[nodiscard]] virtual Connection SubscribeGlobalImpl(
            EventId event_id, std::weak_ptr<void> sub,
            std::function<void(const Event&)> cb,
            ConnectionType connection_type) = 0;

        virtual void FireGlobalImpl(EventId event_id, PluginPtr<Event> e_ptr) = 0;

        [[nodiscard]] virtual Connection SubscribeToSenderImpl(
            EventId event_id, std::weak_ptr<void> sub_id,
            std::weak_ptr<void> sender_id, std::function<void(const Event&)> cb,
            ConnectionType connection_type) = 0;

        virtual void FireToSenderImpl(const std::weak_ptr<void>& sender_id,
            EventId event_id, PluginPtr<Event> e_ptr) = 0;
    };

    namespace clsid {
        /** @brief EventBus 服务的全局唯一 ClassId。 */
        constexpr ClassId kEventBus = ConstexprHash("z3y-core-event-bus-SERVICE-UUID-D54E82F1");
    }

}  // namespace z3y

#endif  // Z3Y_FRAMEWORK_I_EVENT_BUS_H_