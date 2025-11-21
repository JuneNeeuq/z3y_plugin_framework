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
 * @file demo_simple_impl_a.h
 * @brief z3y::demo::DemoSimpleImplA (IDemoSimple 接口实现 A) 的头文件。
 * @author Yue Liu
 * @date 2025-08-03
 * @copyright Copyright (c) 2025 Yue Liu
 *
 * @details
 * [受众：插件开发者 (作为示例)]
 *
 * 演示了如何实现一个“瞬态组件”(Component)。
 * 它还演示了插件如何 *依赖* 另一个服务 (IDemoLogger)。
 *
 * @see IDemoSimple (它实现的接口)
 * @see Z3Y_AUTO_REGISTER_COMPONENT (在 .cpp 文件中用于注册)
 */

#pragma once

#ifndef Z3Y_PLUGIN_DEMO_CORE_DEMO_SIMPLE_IMPL_A_H_
#define Z3Y_PLUGIN_DEMO_CORE_DEMO_SIMPLE_IMPL_A_H_

#include <string>
#include "framework/z3y_define_impl.h"
#include "interfaces_demo/i_demo_logger.h"  // 包含 IDemoLogger (依赖)
#include "interfaces_demo/i_demo_simple.h"  // 包含 IDemoSimple 接口

namespace z3y {
    namespace demo {

        /**
         * @class DemoSimpleImplA
         * @brief IDemoSimple 接口的 *默认* 实现。
         * @details
         * 这是一个瞬态组件 (Component)。
         * (瞬态 = 每次 `CreateInstance` 都会创建新实例)
         */
        class DemoSimpleImplA : public PluginImpl<DemoSimpleImplA, IDemoSimple> {
        public:
            //! [插件开发者核心]
            //! 定义组件的唯一 ClassId
            Z3Y_DEFINE_COMPONENT_ID("z3y-demo-DemoSimpleImplA-UUID-A9407176");

            DemoSimpleImplA();
            virtual ~DemoSimpleImplA();

            //! [实现] IDemoSimple::GetSimpleString 接口
            std::string GetSimpleString() override;

        private:
            /**
             * @brief IDemoLogger 服务的缓存指针。
             *
             * [受众：插件开发者 (最佳实践)]
             *
             * [设计思想：懒加载 (Lazy Loading)]
             * **不要**在构造函数中初始化这个指针！
             *
             * 1. **为什么？**
             * 如果在 `DemoSimpleImplA` 的构造函数中调用
             * `z3y::GetDefaultService<IDemoLogger>()`，
             * 而 `DemoLoggerService` (在其构造函数或 `Initialize` 钩子中)
             * 也试图创建 `DemoSimpleImplA`，
             * 这将导致循环依赖和启动死锁。
             *
             * 2. **怎么做？**
             * 保持它在构造时为 `nullptr`。
             * 在 *首次* 需要它的时候（例如在 `GetSimpleString()` 中）
             * 才调用 `z3y::GetDefaultService`
             * 来获取它并缓存到此成员变量中。
             *
             * @see DemoSimpleImplA::GetSimpleString() (在 .cpp
             * 文件中查看实现)
             */
            PluginPtr<IDemoLogger> logger_;
        };

    }  // namespace demo
}  // namespace z3y

#endif  // Z3Y_PLUGIN_DEMO_CORE_DEMO_SIMPLE_IMPL_A_H_