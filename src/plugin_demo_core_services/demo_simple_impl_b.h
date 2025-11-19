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
 * @file demo_simple_impl_b.h
 * @brief z3y::demo::DemoSimpleImplB (IDemoSimple 接口实现 B) 的头文件。
 * @author Yue Liu
 * @date 2025-08-03
 * @copyright Copyright (c) 2025 Yue Liu
 *
 * @details
 * [受众：插件开发者 (作为示例)]
 *
 * 演示了如何为同一个接口 (`IDemoSimple`)
 * 提供 *第二个*（非默认）的实现。
 *
 * @see IDemoSimple (它实现的接口)
 * @see z3y::demo::DemoSimpleImplA (同一接口的默认实现)
 */

#pragma once

#ifndef Z3Y_PLUGIN_DEMO_CORE_DEMO_SIMPLE_IMPL_B_H_
#define Z3Y_PLUGIN_DEMO_CORE_DEMO_SIMPLE_IMPL_B_H_

#include <string>
#include "framework/z3y_define_impl.h"
#include "interfaces_demo/i_demo_simple.h"  // 包含 IDemoSimple 接口

namespace z3y {
    namespace demo {

        /**
         * @class DemoSimpleImplB
         * @brief IDemoSimple 接口的 *非默认* 实现。
         * @details
         * 这是一个瞬态组件 (Component)。
         * 它将被注册为 `IsDefault = false`。
         */
        class DemoSimpleImplB : public PluginImpl<DemoSimpleImplB, IDemoSimple> {
        public:
            //! [插件开发者核心]
            //! 定义组件的唯一 ClassId
            //! (必须与 DemoSimpleImplA 不同)
            Z3Y_DEFINE_COMPONENT_ID("z3y-demo-DemoSimpleImplB-UUID-B6ED7068");

            DemoSimpleImplB();
            virtual ~DemoSimpleImplB();
            std::string GetSimpleString() override;
        };

    }  // namespace demo
}  // namespace z3y

#endif  // Z3Y_PLUGIN_DEMO_CORE_DEMO_SIMPLE_IMPL_B_H_