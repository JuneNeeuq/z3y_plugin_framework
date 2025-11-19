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
 * @file demo_event_sender.cpp
 * @brief z3y::demo::DemoEventSender (IDemoEventSender 接口实现) 的源文件。
 * @author Yue Liu
 * @date 2025-08-16
 * @copyright Copyright (c) 2025 Yue Liu
 */

#include "demo_event_sender.h"
#include <iostream>
#include "framework/z3y_define_impl.h"
#include "interfaces_demo/demo_events.h"  // 包含要触发的事件定义

 // [插件开发者核心]
 // **自动注册**
 // 将 DemoEventSender 注册为 *单例服务* (Service)。
 //
 // [参数说明]
 // 1. ClassName: `z3y::demo::DemoEventSender`
 // 2. Alias: `"Demo.EventSender"`
 // 3. IsDefault: `false`
 //    - `false` 表示：
 //      `z3y::GetDefaultService<IDemoEventSender>()` *不会* 找到它。
 //      它只能通过别名 `"Demo.EventSender"` 获取。
Z3Y_AUTO_REGISTER_SERVICE(z3y::demo::DemoEventSender, "Demo.EventSender",
    false);

namespace z3y {
    namespace demo {

        /**
         * @brief 构造函数。
         *
         * [受众：插件开发者 (最佳实践)]
         * [禁忌]
         * 不在构造函数中获取任何服务，以避免死锁风险。
         */
        DemoEventSender::DemoEventSender() {}
        DemoEventSender::~DemoEventSender() {}

        /**
         * @brief [生命周期钩子]
         *
         * [受众：插件开发者 (最佳实践)]
         *
         * [禁忌]
         * 不在此处获取 *其他* 插件的服务 (有死锁风险)。
         *
         * (健壮性演示):
         * 我们可以 *尝试* (`TryGet...`) 获取 `IDemoLogger`(如果它存在)，
         * 并使用 `noexcept` API 来确保即使日志服务尚未加载，也不会抛出异常。
         */
        void DemoEventSender::Initialize() {
            try {
                if (auto [logger, err] = z3y::TryGetDefaultService<IDemoLogger>();
                    err == InstanceError::kSuccess) {
                    logger_ = logger; // 缓存可选的日志服务
                    logger_->Log("DemoEventSender Service Initialized (Initialize() called).");
                }
                else {
                    // 日志服务不可用，打印到 cerr
                    std::cerr << "DemoEventSender::Initialize failed to get logger."
                        << std::endl;
                }
            }
            catch (const z3y::PluginException& e) {
                // [受众：框架维护者]
                // 理论上 TryGet...不会抛出异常，但捕获总是一个好习惯。
                std::cerr << "DemoEventSender::Initialize exception: " << e.what()
                    << std::endl;
            }
        }

        /**
         * @brief [懒加载] 确保依赖的服务已获取。
         *
         * [受众：插件开发者 (最佳实践)]
         *
         * [设计思想：懒加载 (Lazy Loading)]
         * 这是在插件中安全获取依赖服务的 **推荐模式**。
         * 仅在 *首次* 需要时 (即 `Fire...` 被调用时) 才获取依赖。
         *
         * [Get... vs TryGet...]
         * 在 `Initialize` 或 `Shutdown` 中， 我们使用 `TryGet...` (noexcept)。
         * 但在这里，`Fire...` 方法 *必须* 依赖 `IDemoLogger` 和 `IEventBus` 才能工作，
         * 因此我们使用 `Get...` (抛出异常 API)， 并在 `EnsureServices` 内部的 `try...catch`
         * 中处理失败。
         *
         * @return `true` 成功，`false` 失败。
         */
        bool DemoEventSender::EnsureServices() {
            // 1. 如果已经获取过， 直接返回 true
            if (logger_ && bus_) {
                return true;
            }

            // 2. 如果服务缺失，尝试获取 (必须使用抛出异常的 Get... API)
            try {
                // (logger_ 可能在 Initialize 中已获取，也可能失败了)
                if (!logger_) {
                    logger_ = z3y::GetDefaultService<IDemoLogger>();
                }
                if (!bus_) {
                    bus_ = z3y::GetService<IEventBus>(clsid::kEventBus);
                }
                return true;
            }
            catch (const z3y::PluginException& e) {
                // [健壮性]
                // 如果获取失败，打印到 cerr
                // (因为 logger_ 可能就是失败的那个)
                std::cerr << "DemoEventSender failed to acquire services: " << e.what()
                    << std::endl;
                return false;
            }
        }

        /**
         * @brief [实现] 触发一个全局事件。
         */
        void DemoEventSender::FireGlobal() {
            // 1. 在调用时确保服务存在
            if (!EnsureServices()) {
                return;  // 获取服务失败
            }

            logger_->Log("   [EventSender] Firing DemoGlobalEvent...");

            // 2. [演示]
            // 使用全局辅助函数 FireGlobalEvent (在 z3y_service_locator.h 中)
            z3y::FireGlobalEvent<DemoGlobalEvent>("This is a global broadcast!");
        }

        /**
         * @brief [实现] 触发一个特定发布者事件。
         */
        void DemoEventSender::FireSender() {
            // 1. 在调用时确保服务存在
            if (!EnsureServices()) {
                return;  // 获取服务失败
            }

            logger_->Log("   [EventSender] Firing DemoSenderEvent (to self)...");

            // 2. [演示]
            // 使用 IEventBus 成员函数 FireToSender
            // (我们传入 `shared_from_this()` 作为“发布者” 实例)
            bus_->FireToSender<DemoSenderEvent>(shared_from_this(), 123);
        }

    }  // namespace demo
}  // namespace z3y