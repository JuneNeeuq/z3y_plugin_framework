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
 * @file advanced_demo_logger.h
 * @brief z3y::demo::AdvancedDemoLogger (IAdvancedDemoLogger 接口实现) 的头文件。
 * @author Yue Liu
 * @date 2025-08-03
 * @copyright Copyright (c) 2025 Yue Liu
 *
 * @details
 * [受众：插件开发者 (作为高级示例)]
 *
 * 演示了如何实现一个 *继承* 自其他接口 (`IDemoLogger`)
 * 的接口 (`IAdvancedDemoLogger`)。
 *
 * @see IAdvancedDemoLogger (它实现的接口)
 * @see IDemoLogger (它实现的基接口)
 */

#pragma once

#ifndef Z3Y_PLUGIN_DEMO_CORE_ADVANCED_DEMO_LOGGER_H_
#define Z3Y_PLUGIN_DEMO_CORE_ADVANCED_DEMO_LOGGER_H_

#include <mutex>
#include "framework/z3y_define_impl.h"  // 包含 PluginImpl
#include "interfaces_demo/i_advanced_demo_logger.h"  // 包含 IAdvancedDemoLogger

namespace z3y {
    namespace demo {

        /**
         * @class AdvancedDemoLogger
         * @brief IAdvancedDemoLogger 接口的实现。
         *
         * [受众：插件开发者 (高级示例)]
         *
         * [设计思想：多接口实现]
         *
         * `PluginImpl` 的模板参数列表可以包含 *多个* 接口。
         *
         * [!! 关键 !!]
         * 因为 `IAdvancedDemoLogger` 继承自 `IDemoLogger`，
         * 所以 `AdvancedDemoLogger` 类必须同时实现
         * `IAdvancedDemoLogger` 和 `IDemoLogger` 的所有纯虚函数。
         *
         * 同时，`PluginImpl` 的模板参数 *必须*
         * 同时列出 `IAdvancedDemoLogger` 和 `IDemoLogger`。
         *
         * `PluginImpl` 基类需要这个完整的列表 来自动生成 `QueryInterfaceRaw`，
         * 以便它知道此类可以被查询 `IAdvancedDemoLogger::kIid`
         * *和* `IDemoLogger::kIid`。
         */
        class AdvancedDemoLogger : public PluginImpl<AdvancedDemoLogger,
            IAdvancedDemoLogger,  // [1] 实现派生接口
            IDemoLogger> {        // [2] 同时声明基接口
        public:
            //! [插件开发者核心]
            //! 定义组件的唯一 ClassId
            Z3Y_DEFINE_COMPONENT_ID("z3y-demo-AdvancedDemoLogger-UUID-D0000011");

            AdvancedDemoLogger();
            virtual ~AdvancedDemoLogger();

            // --- IDemoLogger 接口实现 ---
            /**
             * @brief [实现] 基接口 `IDemoLogger::Log`
             */
            void Log(const std::string& message) override;

            // --- IAdvancedDemoLogger 接口实现 ---
            /**
             * @brief [实现] 派生接口 `IAdvancedDemoLogger::LogWarning`
             */
            void LogWarning(const std::string& message) override;

        private:
            std::mutex mutex_; // 同样需要线程安全
        };

    }  // namespace demo
}  // namespace z3y

#endif  // Z3Y_PLUGIN_DEMO_CORE_ADVANCED_DEMO_LOGGER_H_