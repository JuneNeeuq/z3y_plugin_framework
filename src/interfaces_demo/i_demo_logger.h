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
 * @file i_demo_logger.h
 * @brief 定义 z3y::demo::IDemoLogger 接口。
 * @author Yue Liu
 * @date 2025-07-19
 * @copyright Copyright (c) 2025 Yue Liu
 *
 * @details
 * [受众：插件开发者 (作为示例)]
 *
 * 这是一个演示“服务”(Service)的示例接口。
 *
 * “服务”是指在框架中通常只存在一个实例 (单例)，
 * 并且被许多其他插件共享和依赖的组件。
 *
 * [接口定义清单]
 * 1. `#include "framework/z3y_define_interface.h"`
 * 2. 继承 `public virtual z3y::IComponent`
 * 3. 在 `public:` 区域使用 `Z3Y_DEFINE_INTERFACE` 宏
 * 4. 定义纯虚函数
 *
 * @see z3y::demo::DemoLoggerService (此接口的实现)
 */

#pragma once

#ifndef Z3Y_INTERFACES_DEMO_I_DEMO_LOGGER_H_
#define Z3Y_INTERFACES_DEMO_I_DEMO_LOGGER_H_

#include <string>
#include "framework/z3y_define_interface.h" // 包含 IComponent 和 Z3Y_DEFINE_INTERFACE

namespace z3y {
    namespace demo {

        /**
         * @class IDemoLogger
         * @brief 示例“服务”接口 (日志服务)。
         * @details
         * 演示一个被其他插件依赖的单例服务。
         */
        class IDemoLogger : public virtual IComponent {
        public:
            //! [插件开发者核心]
            //! 定义接口的元数据 (IID, Name, Version)
            //! 第一个参数必须与 C++ 类名 (IDemoLogger) 严格一致。
            Z3Y_DEFINE_INTERFACE(IDemoLogger, "z3y-demo-IDemoLogger-IID-B1B542F8", 1, 0);

            /**
            * @brief 记录一条消息。
            * @param[in] message 要记录的字符串消息。
            */
            virtual void Log(const std::string& message) = 0;
        };

    }  // namespace demo
}  // namespace z3y

#endif  // Z3Y_INTERFACES_DEMO_I_DEMO_LOGGER_H_