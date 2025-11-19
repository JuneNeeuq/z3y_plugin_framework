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
 * @file demo_core_features.h
 * @brief z3y::demo::DemoCoreFeatures (IDemoModule 核心功能演示) 的头文件。
 * @author Yue Liu
 * @date 2025-08-10
 * @copyright Copyright (c) 2025 Yue Liu
 *
 * @details
 * [受众：插件开发者 (作为示例)]
 *
 * 演示了如何实现一个 `IDemoModule` 接口。
 *
 * 这是一个瞬态组件 (Component)，
 * 它不会被注册为单例服务。
 * `DemoRunnerService` 将会通过 `IPluginQuery` 发现它（因为它实现了 `IDemoModule`），
 * 然后调用 `CreateInstance` 创建一个新实例，
 * 最后调用 `RunTest()`。
 *
 * @see IDemoModule (它实现的接口)
 * @see Z3Y_AUTO_REGISTER_COMPONENT (在 .cpp 文件中用于注册)
 * @see DemoRunnerService (调用此模块的协调器)
 */

#pragma once

#ifndef Z3Y_PLUGIN_DEMO_MODULE_CORE_H_
#define Z3Y_PLUGIN_DEMO_MODULE_CORE_H_

#include "framework/z3y_define_impl.h"
#include "interfaces_demo/i_demo_module.h"  // 包含 IDemoModule 接口
#include "interfaces_demo/i_demo_logger.h"  // 包含 IDemoLogger 接口

namespace z3y {
    namespace demo {

        /**
         * @class DemoCoreFeatures
         * @brief [演示模块] 实现了 IDemoModule，
         * 演示核心 API (Get/Create, Exceptions)。
         */
        class DemoCoreFeatures : public PluginImpl<DemoCoreFeatures, IDemoModule> {
        public:
            //! [插件开发者核心]
            //! 定义组件的唯一 ClassId
            Z3Y_DEFINE_COMPONENT_ID("z3y-demo-CDemoCoreFeatures-UUID-D0000004");

            DemoCoreFeatures();
            virtual ~DemoCoreFeatures();

            /**
             * @brief [生命周期钩子] 重写 IComponent::Initialize()。
             *
             * [受众：插件开发者 (最佳实践)]
             *
             * [禁忌]
             * **不要** 在此函数中调用 `z3y::GetService`
             * 或 `z3y::CreateInstance` 来获取 *其他* 插件。
             *
             * [原因]
             * 存在启动死锁的风险。(详情请见 .cpp 文件中的注释)。
             *
             * 依赖项应在 *首次使用时* (例如 `RunTest` 中)
             * 通过“懒加载”来获取。
             */
            void Initialize() override;

            // --- IDemoModule 接口实现 ---

            /**
             * @brief 获取此演示模块的名称。
             * @return `std::string` 名称。
             */
            std::string GetDemoName() override;

            /**
             * @brief [核心] 执行此模块的测试。
             * @details
             * 将演示 `GetDefaultService`, `CreateDefaultInstance`, `CreateInstance` (按别名),
             * 以及 `Try...` 系列 API 如何处理预期中的失败。
             */
            void RunTest() override;

        private:
            //! [受众：插件开发者 (最佳实践)]
            //! **懒加载** 依赖：
            //! 测试单元也需要日志服务来报告结果。
            //! 此指针在 `RunTest()` 首次需要时才被初始化。
            PluginPtr<IDemoLogger> logger_;
        };

    }  // namespace demo
}  // namespace z3y

#endif  // Z3Y_PLUGIN_DEMO_MODULE_CORE_H_