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
 * @file introspection_features.h
 * @brief z3y::demo::IntrospectionFeatures (IDemoModule 内省功能演示) 的头文件。
 * @author Yue Liu
 * @date 2025-08-17
 * @copyright Copyright (c) 2025 Yue Liu
 *
 * @details
 * [受众：插件开发者 (作为高级示例)]
 *
 * 这是一个实现了 `IDemoModule` 接口的“测试模块”。
 * `DemoRunnerService` 将会发现并创建此类的实例，然后调用 `RunTest()`。
 *
 * `RunTest()`
 * 方法将演示 `IPluginQuery` (内省) 服务的各种功能，
 * 以及接口继承、版本不匹配等高级主题。
 *
 * @see IDemoModule (它实现的接口)
 * @see IPluginQuery (它使用的服务)
 * @see Z3Y_AUTO_REGISTER_COMPONENT (在 .cpp 文件中用于注册)
 */

#pragma once

#ifndef Z3Y_PLUGIN_DEMO_MODULE_INTROSPECTION_H_
#define Z3Y_PLUGIN_DEMO_MODULE_INTROSPECTION_H_

#include "framework/z3y_define_impl.h"
#include "interfaces_demo/i_demo_module.h"  // 包含 IDemoModule 接口
#include "interfaces_demo/i_demo_logger.h"  // 包含 IDemoLogger 接口 (用于依赖)

namespace z3y {
    namespace demo {

        /**
         * @class IntrospectionFeatures
         * @brief [演示模块] 实现了 IDemoModule，演示 IPluginQuery 的所有功能。
         */
        class IntrospectionFeatures
            : public PluginImpl<IntrospectionFeatures, IDemoModule> {
        public:
            //! [插件开发者核心]
            //! 定义组件的唯一 ClassId
            Z3Y_DEFINE_COMPONENT_ID(
                "z3y-demo-CIntrospectionFeatures-UUID-D0000012");

            IntrospectionFeatures();
            virtual ~IntrospectionFeatures();

            /**
             * @brief [生命周期钩子] 重写 IComponent::Initialize()。
             *
             * [受众：插件开发者 (最佳实践)]
             * [禁忌]
             * **不要** 在此函数中获取 *其他* 插件的服务
             * (有死锁风险)。
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
             * 将演示 `IPluginQuery`的所有 API
             * (`GetAllComponents`, `FindComponentsImplementing`等)。
             */
            void RunTest() override;

        private:
            //! [懒加载] 缓存 IDemoLogger 服务的指针。
            PluginPtr<IDemoLogger> logger_;
            //! [懒加载] 缓存 IPluginQuery 服务的指针。
            PluginPtr<IPluginQuery> query_;
        };

    }  // namespace demo
}  // namespace z3y

#endif  // Z3Y_PLUGIN_DEMO_MODULE_INTROSPECTION_H_