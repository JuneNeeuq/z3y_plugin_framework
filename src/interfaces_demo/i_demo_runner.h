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
 * @file i_demo_runner.h
 * @brief 定义 z3y::demo::IDemoRunner 接口。
 * @author Yue Liu
 * @date 2025-07-20
 * @copyright Copyright (c) 2025 Yue Liu
 *
 * @details
 * [受众：框架使用者 (宿主 Host)]
 *
 * 这是宿主程序 (Host) 依赖的“主执行”服务。
 *
 * [设计思想]
 * 宿主 `main.cpp` 的职责被简化为：
 * 1. `PluginManager::Create()`
 * 2. `manager->LoadPluginsFromDirectory(...)`
 * 3. `auto runner = z3y::GetDefaultService<IDemoRunner>();`
 * 4. `runner->RunAllDemos();`
 *
 * 宿主 *不* 关心加载了多少插件，
 * 也不关心 `IDemoModule` 接口的存在。
 * 它只将“执行业务逻辑”的职责委托给 `IDemoRunner` 服务。
 *
 * @see z3y::demo::DemoRunnerService (此接口的实现)
 * @see host_console_demo/main.cpp (此接口的使用者)
 */

#pragma once

#ifndef Z3Y_INTERFACES_DEMO_I_DEMO_RUNNER_H_
#define Z3Y_INTERFACES_DEMO_I_DEMO_RUNNER_H_

#include "framework/z3y_define_interface.h"

namespace z3y {
    namespace demo {

        /**
         * @class IDemoRunner
         * @brief 自动查找并执行所有 `IDemoModule` 的服务。
         * @details 宿主 (Host)
         * 启动后获取此服务，并将其作为业务逻辑的“主入口点”。
         */
        class IDemoRunner : public virtual IComponent {
        public:
            //! [插件开发者核心]
            //! 定义接口的元数据 (IID, Name, Version)
            Z3Y_DEFINE_INTERFACE(IDemoRunner, "z3y-demo-IDemoRunner-IID-D0000002", 1, 0);

            /**
            * @brief [核心] 运行所有已加载的演示模块。
            *
            * [受众：框架维护者]
            * 此函数的实现 (在 `DemoRunnerService` 中)
            * 应该：
            * 1. 获取 `IPluginQuery` 服务。
            * 2. 调用 `query->FindComponentsImplementing(IDemoModule::kIid)`。
            * 3. 遍历列表， `CreateInstance<IDemoModule>`。
            * 4. 调用 `module->RunTest()`。
            */
            virtual void RunAllDemos() = 0;
        };

    }  // namespace demo
}  // namespace z3y

#endif  // Z3Y_INTERFACES_DEMO_I_DEMO_RUNNER_H_