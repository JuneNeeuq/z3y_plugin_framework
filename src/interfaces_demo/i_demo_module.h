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
 * @file i_demo_module.h
 * @brief 定义 z3y::demo::IDemoModule 接口。
 * @author Yue Liu
 * @date 2025-07-20
 * @copyright Copyright (c) 2025 Yue Liu
 *
 * @details
 * [受众：插件开发者 (作为示例)]
 *
 * 这是演示项目“模块化”的核心接口。
 * 它定义了一个“可被发现和执行的测试单元”。
 *
 * [设计思想]
 * 框架的使用者 (Host) 不直接调用测试代码，
 * 而是依赖 `IDemoRunner` 服务。
 * `IDemoRunner` 服务则会使用 `IPluginQuery`
 * 来查找 *所有* 实现了 `IDemoModule` 接口的插件，
 * 并依次执行它们。
 *
 * 任何实现了此接口的插件 (例如 `DemoCoreFeatures`)
 * 都会被 `DemoRunnerService` 自动发现和执行。
 *
 * @see z3y::demo::IDemoRunner (此接口的使用者)
 * @see z3y::demo::DemoCoreFeatures (此接口的实现)
 */

#pragma once

#ifndef Z3Y_INTERFACES_DEMO_I_DEMO_MODULE_H_
#define Z3Y_INTERFACES_DEMO_I_DEMO_MODULE_H_

#include <string>
#include "framework/z3y_define_interface.h"

namespace z3y {
    namespace demo {

        /**
         * @class IDemoModule
         * @brief 模块化测试单元的统一接口。
         * @details
         * 所有“测试单元”插件 (例如 `plugin_demo_module_core`)
         * 都必须实现此接口，
         * 才能被 `DemoRunnerService` 发现和执行。
         */
        class IDemoModule : public virtual IComponent {
        public:
            //! [插件开发者核心]
            //! 定义接口的元数据 (IID, Name, Version)
            Z3Y_DEFINE_INTERFACE(IDemoModule, "z3y-demo-IDemoModule-IID-D0000001", 1, 0);

            /**
            * @brief 获取此测试模块的名称 (用于日志显示)。
            * @return `std::string` 形式的演示名称。
            */
            virtual std::string GetDemoName() = 0;

            /**
             * @brief 执行此模块的测试。
             * @details `DemoRunnerService` 将调用此函数。
             */
            virtual void RunTest() = 0;
        };

    }  // namespace demo
}  // namespace z3y

#endif  // Z3Y_INTERFACES_DEMO_I_DEMO_MODULE_H_