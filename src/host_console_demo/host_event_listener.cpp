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
 * @file host_event_listener.cpp
 * @brief [宿主] `HostEventListener` 类的实现。
 * @author Yue Liu
 * @date 2025-08-23
 * @copyright Copyright (c) 2025 Yue Liu
 */

#include "host_event_listener.h"
#include <iomanip>  // 用于 std::hex, std::setw (格式化 CLSID 输出)

namespace z3y {
    namespace demo {

        HostEventListener::HostEventListener() {
            std::cout << "[Host] HostEventListener Created." << std::endl;
        }

        HostEventListener::~HostEventListener() {
            std::cout << "[Host] HostEventListener Destroyed." << std::endl;
            // [受众：框架维护者]
            //
            // [设计思想：自动 GC]
            // 无需手动调用 Unsubscribe。
            // 当 `main.cpp` 中的 `host_listener.reset()` 被调用时，
            // 此对象的 `shared_ptr` 引用计数归零， 析构函数被调用。
            //
            // `IEventBus` 中持有的所有 `std::weak_ptr` 将自动 `expired()`。
            // `event_bus_impl.cpp` 中的 `CleanupExpiredSubscriptions` (Lazy GC)
            // 机制 会在下一次 `Fire` 时自动清理这些失效的订阅。
        }

        /**
         * @brief [实现] 订阅所有关心的框架事件。
         */
        void HostEventListener::SubscribeToFrameworkEvents() {
            std::cout << "[Host] Subscribing to framework events..." << std::endl;

            // [受众：框架使用者 (最佳实践)]
            //
            // 使用 `TrySubscribeGlobalEvent` (noexcept API)
            // 和 C++17 的 if-init 结构化绑定。
            // 这种方式在宿主初始化代码中非常健壮。
            //

            if (auto [conn, err] =
                z3y::TrySubscribeGlobalEvent<z3y::event::PluginLoadSuccessEvent>(
                    shared_from_this(), &HostEventListener::OnPluginLoaded);
                err != z3y::InstanceError::kSuccess) {
                std::cerr
                    << "[Host] HostEventListener failed to subscribe to "
                    "PluginLoadSuccessEvent!"
                    << std::endl;
                return;
            }

            if (auto [conn, err] =
                z3y::TrySubscribeGlobalEvent<z3y::event::PluginLoadFailureEvent>(
                    shared_from_this(), &HostEventListener::OnPluginFailed);
                err != z3y::InstanceError::kSuccess) {
                std::cerr
                    << "[Host] HostEventListener failed to subscribe to "
                    "PluginLoadFailureEvent!"
                    << std::endl;
                return;
            }

            if (auto [conn, err] =
                z3y::TrySubscribeGlobalEvent<z3y::event::ComponentRegisterEvent>(
                    shared_from_this(), &HostEventListener::OnComponentRegistered);
                err != z3y::InstanceError::kSuccess) {
                std::cerr
                    << "[Host] HostEventListener failed to subscribe to "
                    "ComponentRegisterEvent!"
                    << std::endl;
                return;
            }
        }

        // --- 回调实现 ---

        void HostEventListener::OnPluginLoaded(
            const z3y::event::PluginLoadSuccessEvent& e) {
            std::cout << "[Host FW Event] Plugin Loaded: " << e.plugin_path_ << std::endl;
        }

        void HostEventListener::OnPluginFailed(
            const z3y::event::PluginLoadFailureEvent& e) {
            std::cout << "[Host FW Event] PLUGIN FAILED: " << e.plugin_path_
                << " (Error: " << e.error_message_ << ")" << std::endl;
        }

        void HostEventListener::OnComponentRegistered(
            const z3y::event::ComponentRegisterEvent& e) {
            std::cout << "[Host FW Event] Component Registered:\n"
                << "       - Alias: " << (e.alias_.empty() ? "<none>" : e.alias_)
                << "\n"
                // 格式化 CLSID (uint64_t) 为 0x... 十六进制
                << "       - CLSID: 0x" << std::hex << std::setw(16)
                << std::setfill('0') << e.clsid_ << std::dec << "\n"
                << "       - Type: "
                << (e.is_singleton_ ? "Service (Singleton)"
                    : "Component (Transient)")
                << "\n"
                << "       - From: " << e.plugin_path_ << std::endl;
        }

    }  // namespace demo
}  // namespace z3y