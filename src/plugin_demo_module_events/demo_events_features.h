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
 * @file demo_events_features.h
 * @brief z3y::demo::DemoEventsFeatures (IDemoModule 事件总线演示) 的头文件。
 * @author Yue Liu
 * @date 2025-08-16
 * @copyright Copyright (c) 2025 Yue Liu
 *
 * @details
 * [受众：插件开发者 (作为示例)]
 *
 * 这是一个实现了 `IDemoModule` 接口的“测试模块”。
 * `DemoRunnerService` 将会发现并创建此类的实例，然后调用 `RunTest()`。
 *
 * `RunTest()` 方法将演示事件系统的所有功能。
 *
 * @see IDemoModule (它实现的接口)
 * @see Z3Y_AUTO_REGISTER_COMPONENT (在 .cpp 文件中用于注册)
 */

#pragma once

#ifndef Z3Y_PLUGIN_DEMO_MODULE_EVENTS_FEATURES_H_
#define Z3Y_PLUGIN_DEMO_MODULE_EVENTS_FEATURES_H_

#include "framework/z3y_define_impl.h"
#include "framework/connection.h"  // 包含 Connection 和 ScopedConnection
#include "interfaces_demo/demo_events.h"
#include "interfaces_demo/i_demo_module.h"
#include "interfaces_demo/i_demo_logger.h" // 包含 IDemoLogger

namespace z3y {
    namespace demo {

        /**
         * @class DemoEventsFeatures
         * @brief [演示模块] 实现了 IDemoModule， 演示事件系统的所有功能。
         */
        class DemoEventsFeatures : public PluginImpl<DemoEventsFeatures, IDemoModule> {
        public:
            //! [插件开发者核心]
            //! 定义组件的唯一 ClassId
            Z3Y_DEFINE_COMPONENT_ID("z3y-demo-CDemoEventsFeatures-UUID-D0000006");

            DemoEventsFeatures();
            virtual ~DemoEventsFeatures();

            /**
             * @brief [生命周期钩子] 重写 IComponent::Initialize()。
             *
             * [受众：插件开发者 (最佳实践)]
             * [禁忌]
             * **不要** 在此函数中获取 *其他* 插件的服务
             * (有死锁风险)。
             */
            void Initialize() override;

            /**
             * @brief [生命周期钩子] 重写 IComponent::Shutdown()。
             *
             * [受众：插件开发者 (最佳实践)]
             * 这是在卸载时 *安全* 记录日志的地方。
             * 必须使用 `z3y::TryGet...` (noexcept API)。
             */
            void Shutdown() override;

            // --- IDemoModule 接口实现 ---

            /**
             * @brief 获取此演示模块的名称。
             * @return `std::string` 名称。
             */
            std::string GetDemoName() override;

            /**
             * @brief [核心] 执行此模块的测试。
             * @details
             * 将演示全局、特定发布者、kQueued、异步异常、
             * 手动 Unsubscribe (Connection) 和
             * 自动 Unsubscribe (ScopedConnection) 功能。
             */
            void RunTest() override;

        private:
            // --- 回调函数 (在 .cpp 中实现) ---
            void OnGlobalEvent(const DemoGlobalEvent& e);
            void OnSenderEvent(const DemoSenderEvent& e);
            void OnSenderEventQueued(const DemoSenderEvent& e);
            void OnGlobalEvent_WillThrow(const DemoGlobalEvent& e);
            void OnGlobalEvent_Unsub(const DemoGlobalEvent& e);
            void OnGlobalEvent_Scoped(const DemoGlobalEvent& e);

            //! [懒加载] 缓存的服务指针
            PluginPtr<IDemoLogger> logger_;
            PluginPtr<IEventBus> bus_;

            //! 用于验证回调是否被执行的标志
            bool global_event_received_ = false;
            bool sender_event_received_ = false;
            bool queued_event_received_ = false;
            bool unsub_event_received_ = false;
            bool scoped_event_received_ = false;

            /**
             * @brief [演示]
             * 用于演示手动 `Disconnect()` 的句柄。
             */
            z3y::Connection manual_conn_;

            /**
             * @brief [演示]
             * 用于演示 RAII 自动 `Disconnect()` 的句柄。
             *
             * [受众：插件开发者 (最佳实践)]
             * 推荐使用 `ScopedConnection` 作为成员变量来管理订阅。
             * 当此 `DemoEventsFeatures` 实例被析构时，
             * `scoped_conn_test_` 的析构函数会自动调用 `Disconnect()`。
             */
            z3y::ScopedConnection scoped_conn_test_;
        };

    }  // namespace demo
}  // namespace z3y

#endif  // Z3Y_PLUGIN_DEMO_MODULE_EVENTS_FEATURES_H_