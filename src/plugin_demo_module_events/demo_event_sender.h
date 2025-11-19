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
 * @file demo_event_sender.h
 * @brief z3y::demo::DemoEventSender (IDemoEventSender 接口实现) 的头文件。
 * @author Yue Liu
 * @date 2025-08-16
 * @copyright Copyright (c) 2025 Yue Liu
 *
 * @details
 * [受众：插件开发者 (作为示例)]
 *
 * 这是一个辅助服务类，用于演示插件间的通信。
 *
 * [设计思想]
 * `DemoEventsFeatures` (测试模块)
 * 会获取此服务 (`IDemoEventSender`)，
 * 并调用它的 `Fire...()` 方法。
 *
 * `DemoEventSender` (本类)
 * 在被调用时，会使用 `IEventBus` 服务来 *触发* 事件。
 *
 * `DemoEventsFeatures` (订阅者)
 * 会收到它自己触发的事件，从而完成测试闭环。
 *
 * @see IDemoEventSender (它实现的接口)
 * @see DemoEventsFeatures (使用此服务的测试模块)
 */

#pragma once

#ifndef Z3Y_PLUGIN_DEMO_MODULE_EVENTS_SENDER_H_
#define Z3Y_PLUGIN_DEMO_MODULE_EVENTS_SENDER_H_

#include "framework/z3y_define_impl.h"
#include "interfaces_demo/i_demo_event_sender.h"
#include "interfaces_demo/i_demo_logger.h" // 包含 IDemoLogger

namespace z3y {
    namespace demo {

        /**
         * @class DemoEventSender
         * @brief IDemoEventSender 接口的实现。
         * @details
         * 这是一个单例服务，专门用于触发事件。
         */
        class DemoEventSender
            : public PluginImpl<DemoEventSender, IDemoEventSender> {
        public:
            //! [插件开发者核心]
            //! 定义组件的唯一 ClassId
            Z3Y_DEFINE_COMPONENT_ID("z3y-demo-CDemoEventSender-UUID-D0000005");

            DemoEventSender();
            virtual ~DemoEventSender();

            /**
             * @brief [生命周期钩子] 重写 IComponent::Initialize()。
             *
             * [受众：插件开发者 (最佳实践)]
             *
             * [禁忌]
             * **不要** 在此函数中获取 *其他* 插件的服务
             * (有死锁风险)。
             *
             * (此处的 `TryGetDefaultService`
             * 演示了如何进行 *可选* 的日志记录， 即使日志服务不存在也不会失败)。
             */
            void Initialize() override;

            // --- IDemoEventSender 接口实现 ---

            /**
             * @brief [实现] 触发一个全局 DemoGlobalEvent。
             */
            void FireGlobal() override;

            /**
             * @brief [实现]
             * 触发一个特定发布者 DemoSenderEvent (发布者是`shared_from_this()`)。
             */
            void FireSender() override;

        private:
            /**
             * @brief [受众：插件开发者 (最佳实践)]
             * **懒加载** 确保依赖的服务 (DemoLogger 和 EventBus) 已被获取。
             *
             * @details
             * [设计思想：懒加载 (Lazy Loading)]
             * 在 `FireGlobal` 或 `FireSender` *首次* 被调用时执行。
             *
             * [禁忌]
             * **不要** 在构造函数或 `Initialize()` 中执行此操作，
             * 以避免启动死锁。
             *
             * @return `true` 如果服务都已成功获取，`false` 则表示获取失败。
             */
            bool EnsureServices();

            //! 缓存的 Logger 服务指针 (懒加载)
            PluginPtr<IDemoLogger> logger_;
            //! 缓存的 EventBus 服务指针 (懒加载)
            PluginPtr<IEventBus> bus_;
        };

    }  // namespace demo
}  // namespace z3y

#endif  // Z3Y_PLUGIN_DEMO_MODULE_EVENTS_SENDER_H_