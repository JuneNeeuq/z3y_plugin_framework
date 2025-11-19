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
 * @file i_demo_simple.h
 * @brief 定义 z3y::demo::IDemoSimple 接口。
 * @author Yue Liu
 * @date 2025-07-20
 * @copyright Copyright (c) 2025 Yue Liu
 *
 * @details
 * [受众：插件开发者 (作为示例)]
 *
 * 这是一个演示“瞬态组件”(Component)的示例接口。
 *
 * “组件”是指每次请求 (`z3y::CreateInstance`)
 * 都会创建一个 *新* 实例的对象。
 * 这与“服务” (Service, 如 `IDemoLogger`)
 * 的单例 (Singleton) 行为形成对比。
 *
 * [接口定义清单]
 * 1. `#include "framework/z3y_define_interface.h"`
 * 2. 继承 `public virtual z3y::IComponent`
 * 3. 在 `public:` 区域使用 `Z3Y_DEFINE_INTERFACE` 宏
 * 4. 定义纯虚函数
 *
 * @see z3y::demo::DemoSimpleImplA (此接口的实现)
 * @see z3y::demo::DemoSimpleImplB (此接口的另一个实现)
 */

#pragma once

#ifndef Z3Y_INTERFACES_DEMO_I_DEMO_SIMPLE_H_
#define Z3Y_INTERFACES_DEMO_I_DEMO_SIMPLE_H_

#include <string>
#include "framework/z3y_define_interface.h" // 包含 IComponent 和 Z3Y_DEFINE_INTERFACE

namespace z3y {
    namespace demo {

        /**
         * @class IDemoSimple
         * @brief 示例“组件”接口 (瞬态)。
         * @details
         * 演示一个简单的、无状态的、每次调用都会创建新实例的组件。
         */
        class IDemoSimple : public virtual IComponent {
        public:
            //! [插件开发者核心]
            //! 定义接口的元数据 (IID, Name, Version)
            Z3Y_DEFINE_INTERFACE(IDemoSimple, "z3y-demo-IDemoSimple-IID-A4736128", 1, 0);

            /**
            * @brief 获取一个示例字符串。
            * @return 一个 `std::string`。
            */
            virtual std::string GetSimpleString() = 0;
        };

    }  // namespace demo
}  // namespace z3y

#endif  // Z3Y_INTERFACES_DEMO_I_DEMO_SIMPLE_H_