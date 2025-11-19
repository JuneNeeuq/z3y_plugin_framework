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
 * @file demo_logger_service.h
 * @brief z3y::demo::DemoLoggerService (IDemoLogger 接口实现) 的头文件。
 * @author Yue Liu
 * @date 2025-08-02
 * @copyright Copyright (c) 2025 Yue Liu
 *
 * @details
 * [受众：插件开发者 (作为示例)]
 *
 * 演示了如何实现一个基础的单例服务。
 *
 * [实现清单]
 * 1. 继承 `PluginImpl`，
 * 模板参数为 <[实现类], [实现的接口1], [实现的接口2], ...>
 * 2. 在 `public` 区域使用 `Z3Y_DEFINE_COMPONENT_ID` 定义一个唯一的实现 CLSID。
 * 3. 实现所有基接口的纯虚函数 (例如 `IDemoLogger::Log`)。
 * 4. 在 .cpp 文件中，使用 `Z3Y_AUTO_REGISTER_SERVICE` 注册此类。
 *
 * @see IDemoLogger (它实现的接口)
 * @see Z3Y_AUTO_REGISTER_SERVICE (在 .cpp 文件中用于注册)
 */

#pragma once

#ifndef Z3Y_PLUGIN_DEMO_CORE_DEMO_LOGGER_SERVICE_H_
#define Z3Y_PLUGIN_DEMO_CORE_DEMO_LOGGER_SERVICE_H_

#include <mutex>  // 用于 std::mutex
#include "framework/z3y_define_impl.h"    // 包含 PluginImpl, Z3Y_DEFINE_COMPONENT_ID
#include "interfaces_demo/i_demo_logger.h"  // 包含 IDemoLogger 接口

namespace z3y {
    namespace demo {

        /**
         * @class DemoLoggerService
         * @brief IDemoLogger 接口的默认实现。
         * @details
         * 这是一个单例服务 (Service)，
         * 演示了如何实现一个被多方依赖的基础服务。
         */
        class DemoLoggerService : public PluginImpl<DemoLoggerService, IDemoLogger> {
        public:
            //! [插件开发者核心]
            //! 定义组件的唯一 ClassId
            //! 这个字符串在所有插件实现中必须是唯一的。
            Z3Y_DEFINE_COMPONENT_ID("z3y-demo-DemoLoggerService-UUID-C50A10B4");

            DemoLoggerService();
            virtual ~DemoLoggerService();

            /**
             * @brief [实现] IDemoLogger::Log 接口。
             * @param[in] message 要打印的消息。
             */
            void Log(const std::string& message) override;

        private:
            //! [受众：插件开发者 (最佳实践)]
            //! **线程安全**:
            //! 日志服务是一个单例，
            //! 可能会被多个线程同时调用 `Log` 方法。
            //! 必须使用互斥锁来保护 `std::cout` (或任何其他共享资源)。
            std::mutex mutex_;
        };

    }  // namespace demo
}  // namespace z3y

#endif  // Z3Y_PLUGIN_DEMO_CORE_DEMO_LOGGER_SERVICE_H_