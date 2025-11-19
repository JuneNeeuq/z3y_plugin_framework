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
 * @file i_demo_event_sender.h
 * @brief 定义 z3y::demo::IDemoEventSender 接口。
 * @author Yue Liu
 * @date 2025-07-26
 * @copyright Copyright (c) 2025 Yue Liu
 *
 * @details
 * [受众：插件开发者 (作为示例)]
 *
 * [设计思想]
 * 这是一个辅助服务接口，
 * 专门用于 `plugin_demo_module_events` 演示。
 *
 * `DemoEventsFeatures` (订阅者) 会获取此服务，
 * 并调用它的 `Fire...()` 方法来 *触发* 事件，
 * 以便测试自己是否能收到。
 *
 * 这演示了插件可以定义服务 (Publisher)，
 * 并被其他插件 (Subscriber) 获取和调用。
 *
 * @see z3y::demo::DemoEventSender (此接口的实现)
 * @see z3y::demo::DemoEventsFeatures (此接口的使用者)
 */

#pragma once

#ifndef Z3Y_INTERFACES_DEMO_I_DEMO_EVENT_SENDER_H_
#define Z3Y_INTERFACES_DEMO_I_DEMO_EVENT_SENDER_H_

#include "framework/z3y_define_interface.h"

namespace z3y {
    namespace demo {

        /**
         * @class IDemoEventSender
         * @brief 演示事件“发布者”的服务接口。
         */
        class IDemoEventSender : public virtual IComponent {
        public:
            //! [插件开发者核心]
            //! 定义接口的元数据 (IID, Name, Version)
            Z3Y_DEFINE_INTERFACE(IDemoEventSender,
                "z3y-demo-IDemoEventSender-IID-D0000007", 1, 0);

            /**
            * @brief 触发一个 `DemoGlobalEvent`。
            */
            virtual void FireGlobal() = 0;

            /**
             * @brief 触发一个 `DemoSenderEvent` (发布者是它自己)。
             */
            virtual void FireSender() = 0;
        };

    }  // namespace demo
}  // namespace z3y

#endif  // Z3Y_INTERFACES_DEMO_I_DEMO_EVENT_SENDER_H_