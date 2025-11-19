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
 * @file framework_events.h
 * @brief 定义由 PluginManager 触发的核心框架事件。
 * @author Yue Liu
 * @date 2025-06-22
 * @copyright Copyright (c) 2025 Yue Liu
 *
 * @details
 * [受众：插件开发者 和 框架使用者]
 *
 * 此文件定义了由 `PluginManager`
 * 自身触发的、可被订阅的“框架事件”。
 *
 * 你可以订阅这些事件来监听框架的生命周期，
 * 例如在宿主中打印所有已加载的插件。
 *
 * @example
 * \code{.cpp}
 * // 订阅插件加载成功事件
 * z3y::SubscribeGlobalEvent<z3y::event::PluginLoadSuccessEvent>(
 * shared_from_this(),
 * &MyHostListener::OnPluginLoaded
 * );
 * \endcode
 */

#pragma once

#ifndef Z3Y_FRAMEWORK_FRAMEWORK_EVENTS_H_
#define Z3Y_FRAMEWORK_FRAMEWORK_EVENTS_H_

#include <string>                       // 用于 std::string
#include "framework/class_id.h"         // 依赖 ClassId
#include "framework/event_helpers.h"    // 依赖 Z3Y_DEFINE_EVENT
#include "framework/i_event_bus.h"      // 依赖 Event

namespace z3y {
    namespace event {

        // --- 1. 插件加载事件 ---

        /**
         * @struct PluginLoadSuccessEvent
         * @brief 当一个插件 (DLL/SO) 被成功加载和初始化时触发的事件。
         * @details 这是一个全局广播事件。
         */
        struct PluginLoadSuccessEvent : public Event {
            //! 定义事件的元数据 (ID 和 Name)
            Z3Y_DEFINE_EVENT(PluginLoadSuccessEvent,
                "z3y-event-plugin-load-success-E0000001");

            //! 成功加载的插件的完整路径
            std::string plugin_path_;

            explicit PluginLoadSuccessEvent(std::string path)
                : plugin_path_(std::move(path)) {
            }
        };

        /**
         * @struct PluginLoadFailureEvent
         * @brief 当一个插件 (DLL/SO) 加载或初始化失败时触发的事件。
         * @details 这是一个全局广播事件。
         */
        struct PluginLoadFailureEvent : public Event {
            //! 定义事件的元数据 (ID 和 Name)
            Z3Y_DEFINE_EVENT(PluginLoadFailureEvent,
                "z3y-event-plugin-load-failure-E0000002");

            //! 尝试加载但失败的插件的完整路径
            std::string plugin_path_;
            //! 描述失败原因的错误信息
            std::string error_message_;

            PluginLoadFailureEvent(std::string path, std::string error)
                : plugin_path_(std::move(path)), error_message_(std::move(error)) {
            }
        };

        // --- 2. 组件注册事件 ---

        /**
         * @struct ComponentRegisterEvent
         * @brief 当一个组件/服务在 `z3yPluginInit` 中被成功注册时触发的事件。
         * @details 这是一个全局广播事件。
         * @note
         * 框架的核心服务 (如 EventBus 自身)
         * 也会在启动时触发此事件。
         */
        struct ComponentRegisterEvent : public Event {
            //! 定义事件的元数据 (ID 和 Name)
            Z3Y_DEFINE_EVENT(ComponentRegisterEvent,
                "z3y-event-component-register-E0000003");

            //! 被注册组件的 ClassId
            ClassId clsid_;
            //! 被注册组件的别名 (如果为空，则表示没有别名)
            std::string alias_;
            //! 来源插件的完整路径
            std::string plugin_path_;
            //! 是服务 (true) 还是瞬态组件 (false)
            bool is_singleton_;

            ComponentRegisterEvent(ClassId id, const std::string& a,
                const std::string& path, bool singleton)
                : clsid_(id),
                alias_(a),
                plugin_path_(path),
                is_singleton_(singleton) {
            }
        };

    }  // namespace event
}  // namespace z3y

#endif  // Z3Y_FRAMEWORK_FRAMEWORK_EVENTS_H_