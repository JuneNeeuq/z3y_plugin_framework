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
 * @file demo_events_features.cpp
 * @brief z3y::demo::DemoEventsFeatures (IDemoModule 事件总线演示) 的源文件。
 * @author Yue Liu
 * @date 2025-08-16
 * @copyright Copyright (c) 2025 Yue Liu
 */

#include "demo_events_features.h"
#include <iostream>
#include <stdexcept>  // 用于 throw std::runtime_error
#include <thread>     // 用于 std::this_thread::sleep_for
#include "framework/z3y_define_impl.h"
#include "interfaces_demo/i_demo_event_sender.h"

 // [插件开发者核心]
 // **自动注册**
 // 将 DemoEventsFeatures 注册为一个 *瞬态组件* (Component)。
 //
 // [参数说明]
 // 1. ClassName: `z3y::demo::DemoEventsFeatures`
 // 2. Alias: `"Demo.EventFeatures"`
 // 3. IsDefault: `false`
 //
 // [设计思想：组件 (Component) vs 服务 (Service)]
 // `DemoEventsFeatures` 是一个“测试单元”，
 // 每次 `DemoRunnerService` 运行它时，
 // 都应该创建一个 *新* 实例，因此它是一个瞬态组件 (Component)。
Z3Y_AUTO_REGISTER_COMPONENT(z3y::demo::DemoEventsFeatures,
    "Demo.EventFeatures", false);

namespace z3y {
    namespace demo {

        /**
         * @brief 构造函数。
         */
        DemoEventsFeatures::DemoEventsFeatures() {}

        /**
         * @brief 析构函数。
         *
         * [受众：插件开发者 (最佳实践) 和 框架维护者]
         *
         * [设计思想：自动 GC vs RAII]
         *
         * 1. **[禁忌]**
         * 在 C++ 中， 在析构函数中调用 `shared_from_this()`
         * 是 **未定义行为 (Undefined Behavior)**。
         * 因此，你 *不能* 在析构函数中调用 `z3y::Unsubscribe(shared_from_this())`。
         *
         * 2. **自动 GC (Lazy GC)**：
         * `IEventBus` (在 `event_bus_impl.cpp` 中)
         * 被设计为在订阅时 *只* 存储 `std::weak_ptr`。
         * 当此对象 (`DemoEventsFeatures`) 被析构时， `weak_ptr` 会自动 `expired()`。
         * `IEventBus` 的“懒惰 GC” (`CleanupExpiredSubscriptions`)
         * 机制 会在 *下一次* `Fire` 该事件时自动清理这些失效的订阅。
         * (对于大多数情况，这是足够的)。
         *
         * 3. **RAII (推荐的确定性清理)**：
         * 如果你需要 *确定性* 的取消订阅 (例如， 在对象销毁 *前* 必须停止接收事件)，
         * 开发者 *必须* 使用 `z3y::ScopedConnection`
         * 作为成员变量 (如此文件中 `scoped_conn_test_` 所示)。
         * 它会在析构时安全地调用 `Disconnect()`。
         */
        DemoEventsFeatures::~DemoEventsFeatures() {
            std::cout << "  [DemoEventsFeatures] Destructor called." << std::endl;
        }

        /**
         * @brief [生命周期钩子]
         *
         * [受众：插件开发者 (最佳实践)]
         * [禁忌]
         * **不要** 在此函数中获取 *其他* 插件的服务
         * (有死锁风险)。
         */
        void DemoEventsFeatures::Initialize() {
            std::cout << "  [DemoEventsFeatures] Instance Initialized (Initialize() "
                "called)."
                << std::endl;
        }

        /**
         * @brief [生命周期钩子]
         *
         * [受众：插件开发者 (最佳实践)]
         * 使用 `TryGet...`(noexcept API) 在卸载时安全地记录日志。
         */
        void DemoEventsFeatures::Shutdown() {
            if (auto [logger, err] = z3y::TryGetDefaultService<IDemoLogger>();
                err == InstanceError::kSuccess) {
                logger->Log("DemoEventsFeatures Shutting Down (safe).");
            }
        }

        std::string DemoEventsFeatures::GetDemoName() {
            return "EventBus Features (Global, Sender, Queued, Async Error, "
                "Unsubscribe)";
        }

        /**
         * @brief [核心] 执行此模块的测试。
         *
         * [受众：插件开发者 (示例)]
         */
        void DemoEventsFeatures::RunTest() {
            // [1. 懒加载依赖]
            try {
                if (!logger_) {
                    logger_ = z3y::GetDefaultService<IDemoLogger>();
                }
                if (!bus_) {
                    bus_ = z3y::GetService<IEventBus>(clsid::kEventBus);
                }
            } catch (const z3y::PluginException& e) {
                std::cerr << "DemoEventsFeatures failed to get services in RunTest: "
                    << e.what() << std::endl;
                return;
            }

            try {
                // 1. 获取事件发送者服务 (我们将调用它来触发事件)
                auto sender_service =
                    z3y::GetService<IDemoEventSender>("Demo.EventSender");

                // --- 2. 演示 Global 订阅 (kDirect) ---
                logger_->Log("[EventDemo] 2. Subscribing to DemoGlobalEvent...");
                // [演示]
                // (void)
                // 用于消除 `[[nodiscard]]` 警告。
                // 我们在这里 *故意* 不存储 Connection 句柄， 依赖 `weak_ptr` 的 Lazy GC
                // 在析构时自动清理。
                (void)z3y::SubscribeGlobalEvent<DemoGlobalEvent>(
                    shared_from_this(), &DemoEventsFeatures::OnGlobalEvent);

                sender_service->FireGlobal();  // 触发
                if (!global_event_received_) {
                    logger_->Log("   ...[FAIL] Did not receive global event!");
                }

                // --- 3. 演示 Sender 订阅 (kDirect) ---
                logger_->Log("[EventDemo] 3. Subscribing to DemoSenderEvent (kDirect)...");
                (void)bus_->SubscribeToSender<DemoSenderEvent>(
                    sender_service,  // [关键] 只订阅 `sender_service` 实例
                    shared_from_this(), &DemoEventsFeatures::OnSenderEvent,
                    ConnectionType::kDirect); // 同步

                sender_service->FireSender();  // 触发
                if (!sender_event_received_) {
                    logger_->Log("   ...[FAIL] Did not receive sender event (kDirect)!");
                }

                // --- 4. 演示 Sender 订阅 (kQueued) ---
                logger_->Log("[EventDemo] 4. Subscribing to DemoSenderEvent (kQueued)...");
                (void)bus_->SubscribeToSender<DemoSenderEvent>(
                    sender_service, shared_from_this(),
                    &DemoEventsFeatures::OnSenderEventQueued,
                    ConnectionType::kQueued  // [关键] 异步
                );
                sender_service->FireSender();  // 触发

                // [演示]
                // 给予异步线程一点时间执行
                logger_->Log("[EventDemo]    (Waiting 20ms for async event...)");
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                if (!queued_event_received_) {
                    logger_->Log("   ...[FAIL] Did not receive sender event (kQueued)!");
                }

                // --- 5. [演示] 异步异常 (OOB Handler) ---
                logger_->Log(
                    "[EventDemo] 5. Testing Async Exception (Host should log error via "
                    "OOB Handler)...");
                (void)z3y::SubscribeGlobalEvent<DemoGlobalEvent>(
                    shared_from_this(), &DemoEventsFeatures::OnGlobalEvent_WillThrow,
                    ConnectionType::kQueued  // [关键] 必须是异步
                );
                sender_service->FireGlobal();  // 触发
                logger_->Log("[EventDemo]    (Waiting 20ms for async exception...)");
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                // (此时 `main.cpp` 中的 `HostExceptionHandler`
                // 应该已经在 `std::cerr` 中打印了错误)

                // --- 6. [演示] 手动取消订阅 (Connection) ---
                logger_->Log("[EventDemo] 6. Testing Manual Unsubscribe (Connection)...");

                // [核心]
                // 存储 `Connection` 句柄
                manual_conn_ = bus_->SubscribeGlobal<DemoGlobalEvent>(
                    shared_from_this(), &DemoEventsFeatures::OnGlobalEvent_Unsub);

                sender_service->FireGlobal();  // 触发第一次
                if (!unsub_event_received_) {
                    logger_->Log("   ...[FAIL] Did not receive event before unsub!");
                }

                logger_->Log(
                    "[EventDemo]    (Now Disconnecting specific connection...)");
                unsub_event_received_ = false;  // 重置标志

                // [核心] 调用句柄的 Disconnect
                manual_conn_.Disconnect();

                sender_service->FireGlobal();  // 触发第二次
                if (unsub_event_received_) {
                    logger_->Log("   ...[FAIL] Received event *after* granular unsub!");
                } else {
                    logger_->Log("   ...Success: Did not receive event after granular unsub.");
                }

                // --- 7. [演示] 自动取消订阅 (ScopedConnection / RAII) ---
                logger_->Log("[EventDemo] 7. Testing ScopedConnection (RAII)...");
                scoped_event_received_ = false;

                {  // -- 开始新作用域 --
                    logger_->Log(
                        "[EventDemo]    (Entering scope, creating ScopedConnection...)");
                    // [核心]
                    // 我们将 `Connection`
                    // 句柄赋值给 *栈上* 的 `ScopedConnection`
                    // 变量。
                    z3y::ScopedConnection scoped_conn_test_on_stack;
                    scoped_conn_test_on_stack = bus_->SubscribeGlobal<DemoGlobalEvent>(
                        shared_from_this(), &DemoEventsFeatures::OnGlobalEvent_Scoped);

                    // (我们也可以赋值给成员变量 `scoped_conn_test_`，
                    //  它将在 `DemoEventsFeatures` 析构时自动断开)

                    sender_service->FireGlobal();  // 触发
                    if (!scoped_event_received_) {
                        logger_->Log(
                            "   ...[FAIL] Did not receive scoped event inside scope!");
                    }
                    // -- 结束作用域 --
                    // [核心]
                    // `scoped_conn_test_on_stack` 在此处析构，
                    // 其析构函数会自动调用 `Disconnect()`。
                    logger_->Log(
                        "[EventDemo]    (Exiting scope, ScopedConnection destroyed)...");
                }

                scoped_event_received_ = false;  // 重置标志
                sender_service->FireGlobal();  // 再次触发

                if (scoped_event_received_) {
                    logger_->Log(
                        "   ...[FAIL] Received event *after* ScopedConnection left "
                        "scope!");
                } else {
                    logger_->Log(
                        "   ...Success: Did not receive event after RAII unsubscribe.");
                }

            } catch (const z3y::PluginException& e) {
                // (捕获 `GetService<IDemoEventSender>` 可能的失败)
                logger_->Log("[EventDemo] [FAIL] " + std::string(e.what()));
            }
        }

        // --- [受众：插件开发者 (示例)] 回调实现 ---
        void DemoEventsFeatures::OnGlobalEvent(const DemoGlobalEvent& e) {
            logger_->Log("   ...[Callback] Received Global Event: " + e.message);
            global_event_received_ = true;
        }
        void DemoEventsFeatures::OnSenderEvent(const DemoSenderEvent& e) {
            logger_->Log("   ...[Callback] Received Sender Event (kDirect): " +
                std::to_string(e.value));
            sender_event_received_ = true;
        }
        void DemoEventsFeatures::OnSenderEventQueued(const DemoSenderEvent& e) {
            logger_->Log("   ...[Callback] Received Sender Event (kQueued): " +
                std::to_string(e.value));
            queued_event_received_ = true;
        }
        void DemoEventsFeatures::OnGlobalEvent_WillThrow(const DemoGlobalEvent& e) {
            logger_->Log(
                "   ...[Callback] (Async Throw) This callback will throw...");
            // [核心]
            // 这是一个在 `kQueued` (异步) 线程中抛出的异常。
            // 它将被 `event_bus_impl.cpp` 中的 `EventLoop` 捕获，
            // 并传递给 `main.cpp` 中注册的 `HostExceptionHandler`。
            throw std::runtime_error(
                "DemoEventsFeatures: This is an intentional async exception.");
        }
        void DemoEventsFeatures::OnGlobalEvent_Unsub(const DemoGlobalEvent& e) {
            logger_->Log("   ...[Callback] (Unsub Test) Received unsub event.");
            unsub_event_received_ = true;
        }
        void DemoEventsFeatures::OnGlobalEvent_Scoped(const DemoGlobalEvent& e) {
            logger_->Log("   ...[Callback] (Scoped Test) Received scoped event.");
            scoped_event_received_ = true;
        }

    }  // namespace demo
}  // namespace z3y