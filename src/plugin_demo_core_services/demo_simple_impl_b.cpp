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
 * @file demo_simple_impl_b.cpp
 * @brief z3y::demo::DemoSimpleImplB (IDemoSimple 接口实现 B) 的源文件。
 * @author Yue Liu
 * @date 2025-08-03
 * @copyright Copyright (c) 2025 Yue Liu
 */

#include "demo_simple_impl_b.h"
#include <iostream>
#include "framework/z3y_define_impl.h"  // 包含 Z3Y_AUTO_REGISTER_COMPONENT

 // [插件开发者核心]
 // **自动注册**
 // 将 DemoSimpleImplB 注册为一个 *瞬态组件* (Component)。
 //
 // [参数说明]
 // 1. ClassName: `z3y::demo::DemoSimpleImplB`
 // 2. Alias: `"Demo.Simple.B"`
 // 3. IsDefault: `false`
 //    - `true`:  `z3y::CreateDefaultInstance<IDemoSimple>()` 会找到它。
 //    - `false`: `z3y::CreateDefaultInstance<IDemoSimple>()` *不会* 找到它。
 //      它只能通过别名 `z3y::CreateInstance<IDemoSimple>("Demo.Simple.B")`
 //      或其 CLSID 来创建。
Z3Y_AUTO_REGISTER_COMPONENT(z3y::demo::DemoSimpleImplB, "Demo.Simple.B", false);

namespace z3y {
    namespace demo {

        DemoSimpleImplB::DemoSimpleImplB() {
            std::cout << "  [DemoSimpleImplB] Instance Created." << std::endl;
        }

        DemoSimpleImplB::~DemoSimpleImplB() {
            std::cout << "  [DemoSimpleImplB] Instance Destroyed." << std::endl;
        }

        std::string DemoSimpleImplB::GetSimpleString() {
            return "Hello from DemoSimpleImplB";
        }

    }  // namespace demo
}  // namespace z3y