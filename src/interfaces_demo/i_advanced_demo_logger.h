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
 * @file i_advanced_demo_logger.h
 * @brief 定义 z3y::demo::IAdvancedDemoLogger 接口，演示接口继承。
 * @author Yue Liu
 * @date 2025-07-19
 * @copyright Copyright (c) 2025 Yue Liu
 *
 * @details
 * [受众：插件开发者 (作为高级示例)]
 *
 * 演示了 C++ 插件接口如何继承自 *另一个* 插件接口。
 * `IAdvancedDemoLogger` 继承自 `IDemoLogger`。
 *
 * [设计思想：接口继承]
 * 1. 继承 `IDemoLogger` 时必须使用 `public virtual`。
 * 2. `IComponent` 也必须通过 `public virtual` 继承
 * (虽然 `IDemoLogger` 已经这样做了，但显式写出更清晰)。
 * 3. [关键] 实现了此接口的类 (如 `AdvancedDemoLogger`)
 * 必须同时实现 *两个* 接口的方法：`Log()` 和 `LogWarning()`。
 * 4. [关键]
 * 实现了此接口的类，在 `PluginImpl` 的模板参数中必须 *同时*
 * 列出 `IAdvancedDemoLogger` 和 `IDemoLogger`。
 *
 * @see z3y::demo::AdvancedDemoLogger (此接口的实现)
 */

#pragma once

#ifndef Z3Y_INTERFACES_DEMO_I_ADVANCED_DEMO_LOGGER_H_
#define Z3Y_INTERFACES_DEMO_I_ADVANCED_DEMO_LOGGER_H_

#include "interfaces_demo/i_demo_logger.h"  // 包含基类接口

namespace z3y {
    namespace demo {

        /**
         * @class IAdvancedDemoLogger
         * @brief 演示接口继承 (IAdvancedDemoLogger 派生自 IDemoLogger)。
         * @note 接口继承时，必须使用 `public virtual` 关键字。
         */
        class IAdvancedDemoLogger : public virtual IDemoLogger {
        public:
            //! [插件开发者核心]
            //! 定义接口的元数据 (IID, Name, Version)
            Z3Y_DEFINE_INTERFACE(IAdvancedDemoLogger, "z3y-demo-IAdvancedDemoLogger-IID-D0000010",
                1, 0);

            /**
            * @brief 记录一条带“警告”前缀的消息。
            * @param[in] message 要记录的警告消息。
            */
            virtual void LogWarning(const std::string& message) = 0;
        };

    }  // namespace demo
}  // namespace z3y

#endif  // Z3Y_INTERFACES_DEMO_I_ADVANCED_DEMO_LOGGER_H_