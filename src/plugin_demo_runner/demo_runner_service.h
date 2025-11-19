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
 * @file demo_runner_service.h
 * @brief z3y::demo::DemoRunnerService (IDemoRunner 接口实现) 的头文件。
 * @author Yue Liu
 * @date 2025-08-09
 * @copyright Copyright (c) 2025 Yue Liu
 *
 * @details
 * [受众：框架使用者 (宿主 Host)]
 * 这是宿主程序 (Host)
 * 在 `main.cpp` 中获取的 *主要* 服务，
 * 用于启动所有演示业务逻辑。
 *
 * [受众：插件开发者 (作为高级示例)]
 * 演示了如何实现一个“协调器”(Orchestrator) 服务。
 * 它的职责是使用 `IPluginQuery` 服务
 * 查找并运行所有已注册的 `IDemoModule` 插件。
 *
 * @see IDemoRunner (它实现的接口)
 * @see Z3Y_AUTO_REGISTER_SERVICE (在 .cpp 文件中用于注册)
 */

#pragma once

#ifndef Z3Y_PLUGIN_DEMO_RUNNER_SERVICE_H_
#define Z3Y_PLUGIN_DEMO_RUNNER_SERVICE_H_

#include "framework/z3y_define_impl.h"
#include "interfaces_demo/i_demo_runner.h"  // 包含 IDemoRunner 接口
#include "interfaces_demo/i_demo_logger.h"  // 包含 IDemoLogger 接口

namespace z3y {
    namespace demo {

        /**
         * @class DemoRunnerService
         * @brief IDemoRunner 接口的实现。
         */
        class DemoRunnerService : public PluginImpl<DemoRunnerService, IDemoRunner> {
        public:
            //! [插件开发者核心]
            //! 定义组件的唯一 ClassId
            Z3Y_DEFINE_COMPONENT_ID("z3y-demo-CDemoRunnerService-UUID-D0000003");

            DemoRunnerService();
            virtual ~DemoRunnerService();

            /**
             * @brief [生命周期钩子] 重写 IComponent::Shutdown()。
             *
             * [受众：插件开发者 (最佳实践)]
             *
             * 当插件被卸载时，此函数会在析构函数之前被调用。
             *
             * [使用时机]
             * 这是一个在清理阶段 *安全地* 使用依赖项 (如 `IDemoLogger`)
             * 进行日志记录的地方。
             *
             * [禁忌]
             * 必须使用 `z3y::TryGet...` (noexcept API) 来获取服务，
             * 因为此时 `IDemoLogger` 服务可能已经被 `Shutdown()` 或销毁，
             * 使用 `z3y::Get...` (抛出 API) 可能会导致崩溃。
             */
            void Shutdown() override;

            /**
             * @brief [核心] 实现 IDemoRunner::RunAllDemos 接口。
             * @details
             * 查找并执行所有 `IDemoModule` 模块。
             */
            void RunAllDemos() override;

        private:
            //! [受众：插件开发者 (最佳实践)]
            //! **懒加载** 依赖：
            //! Runner 需要 Logger 来报告其执行结果。
            //! 此指针 *不* 在构造函数中初始化， 而是保持 `nullptr`，
            //! 直到 `RunAllDemos()` 首次被调用时才获取。
            PluginPtr<IDemoLogger> logger_;
        };

    }  // namespace demo
}  // namespace z3y

#endif  // Z3Y_PLUGIN_DEMO_RUNNER_SERVICE_H_