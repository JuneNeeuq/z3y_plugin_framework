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
 * @file demo_events.h
 * @brief 定义演示项目 (Demo) 所使用的自定义事件。
 * @author Yue Liu
 * @date 2025-07-26
 * @copyright Copyright (c) 2025 Yue Liu
 *
 * @details
 * [受众：插件开发者 (作为示例)]
 *
 * 演示了插件开发者应如何定义自己的事件类型。
 *
 * [事件定义清单]
 * 1. `#include "framework/z3y_define_interface.h"`
 * (它包含了 `Event` 和 `Z3Y_DEFINE_EVENT`)
 * 2. 结构体必须继承自 `z3y::Event`。
 * 3. 必须在结构体内部使用 `Z3Y_DEFINE_EVENT` 宏。
 *
 * @see plugin_demo_module_events (使用这些事件的插件)
 */

#pragma once

#ifndef Z3Y_INTERFACES_DEMO_DEMO_EVENTS_H_
#define Z3Y_INTERFACES_DEMO_DEMO_EVENTS_H_

#include <string>
#include "framework/z3y_define_interface.h"  // 包含 Event 和 Z3Y_DEFINE_EVENT

namespace z3y {
    namespace demo {

        /**
         * @struct DemoGlobalEvent
         * @brief 演示用的全局广播事件。
         * @see IEventBus::FireGlobal
         */
        struct DemoGlobalEvent : public Event {
            //! [插件开发者核心]
            //! 定义事件的元数据 (EventId, Name)
            Z3Y_DEFINE_EVENT(DemoGlobalEvent, "z3y-demo-event-global-E0000001");

            std::string message;
            DemoGlobalEvent(std::string msg) : message(std::move(msg)) {}
        };

        /**
         * @struct DemoSenderEvent
         * @brief 演示用的特定发布者 (Sender-Specific) 事件。
         * @see IEventBus::FireToSender
         */
        struct DemoSenderEvent : public Event {
            //! [插件开发者核心]
            //! 定义事件的元数据 (EventId, Name)
            Z3Y_DEFINE_EVENT(DemoSenderEvent, "z3y-demo-event-sender-E0000002");

            int value;
            DemoSenderEvent(int val) : value(val) {}
        };

    }  // namespace demo
}  // namespace z3y

#endif  // Z3Y_INTERFACES_DEMO_DEMO_EVENTS_H_