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
 * @file host_event_listener.h
 * @brief [宿主] 定义 `HostEventListener` 类。
 * @author Yue Liu
 * @date 2025-08-23
 * @copyright Copyright (c) 2025 Yue Liu
 *
 * @details
 * [受众：框架使用者 (宿主开发者)]
 *
 * 这是一个宿主 (Host) 端的辅助类，
 * 专门用于监听框架的核心事件
 * (例如 `PluginLoadSuccessEvent`, `ComponentRegisterEvent`)
 * 并在控制台打印。
 *
 * [设计思想：enable_shared_from_this]
 *
 * `IEventBus` 使用 `std::weak_ptr` 来安全地持有订阅者，
 * 防止循环引用。
 *
 * 为了能够订阅事件，
 * 此类必须继承自 `std::enable_shared_from_this`，
 * 以便在调用 `Subscribe...` 时可以安全地传递 `shared_from_this()`。
 *
 * 这也意味着 `HostEventListener` 实例 *必须* 由 `std::make_shared` 创建，
 * 并由 `PluginPtr` (即 `std::shared_ptr`) 持有。
 *
 * @see host_console_demo/main.cpp (此类在 main 函数中被创建和使用)
 */

#pragma once

#ifndef Z3Y_HOST_CONSOLE_DEMO_LISTENER_H_
#define Z3Y_HOST_CONSOLE_DEMO_LISTENER_H_

#include <iostream>
#include "framework/z3y_framework.h"  // 包含框架核心 (z3y::event::...)

namespace z3y {
    namespace demo {

        /**
         * @class HostEventListener
         * @brief 监听并打印核心框架事件(如插件加载、注册)。
         */
        class HostEventListener
            : public std::enable_shared_from_this<HostEventListener> {
        public:
            HostEventListener();
            ~HostEventListener();

            /**
             * @brief 订阅所有关心的框架事件。
             *
             * [受众：框架使用者 (最佳实践)]
             *
             * [设计思想：健壮性]
             * 内部使用 `z3y::TrySubscribeGlobalEvent` (noexcept API)，
             * 确保即使 `IEventBus` 服务获取失败 (理论上在 `main.cpp` 中不会发生)，
             * 也不会抛出异常，从而使宿主启动失败。
             */
            void SubscribeToFrameworkEvents();

        private:
            // --- 事件回调 (在 .cpp 中实现) ---

            /**
             * @brief 回调：当插件成功加载时。
             */
            void OnPluginLoaded(const z3y::event::PluginLoadSuccessEvent& e);
            /**
             * @brief 回调：当插件加载失败时。
             */
            void OnPluginFailed(const z3y::event::PluginLoadFailureEvent& e);
            /**
             * @brief 回调：当组件被注册时。
             */
            void OnComponentRegistered(const z3y::event::ComponentRegisterEvent& e);
        };

    }  // namespace demo
}  // namespace z3y

#endif  // Z3Y_HOST_CONSOLE_DEMO_LISTENER_H_