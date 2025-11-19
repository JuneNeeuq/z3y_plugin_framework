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
 * @file advanced_demo_logger.cpp
 * @brief z3y::demo::AdvancedDemoLogger (IAdvancedDemoLogger 接口实现) 的源文件。
 * @author Yue Liu
 * @date 2025-08-03
 * @copyright Copyright (c) 2025 Yue Liu
 */

#include "advanced_demo_logger.h"
#include <iostream>
#include "framework/z3y_define_impl.h"  // 包含 Z3Y_AUTO_REGISTER_SERVICE

 // [插件开发者核心]
 // **自动注册**
 // 将 AdvancedDemoLogger 注册为 *单例服务*。
 //
 // [参数说明]
 // 1. ClassName: `z3y::demo::AdvancedDemoLogger`
 // 2. Alias: `"Demo.Logger.Advanced"`
 // 3. IsDefault: `false`
 //    -
 //    `false`：`z3y::GetDefaultService<IAdvancedDemoLogger>()`
 //    *不会* 找到它。
 //    (因为 `DemoLoggerService` 已经被注册为 `IDemoLogger` 的默认实现，
 //    而此实现类也实现了 `IDemoLogger`，
 //    `is_default=true` 会导致冲突)。
Z3Y_AUTO_REGISTER_SERVICE(z3y::demo::AdvancedDemoLogger, "Demo.Logger.Advanced", false);

namespace z3y {
    namespace demo {

        AdvancedDemoLogger::AdvancedDemoLogger() {
            std::lock_guard<std::mutex> lock(mutex_);
            std::cout << "  [AdvancedDemoLogger] Service Created." << std::endl;
        }

        AdvancedDemoLogger::~AdvancedDemoLogger() {
            std::lock_guard<std::mutex> lock(mutex_);
            std::cout << "  [AdvancedDemoLogger] Service Destroyed." << std::endl;
        }

        /**
         * @brief [实现] IDemoLogger::Log
         *
         * [受众：插件开发者]
         * 即使这是基类接口的方法，也必须被实现。
         */
        void AdvancedDemoLogger::Log(const std::string& message) {
            std::lock_guard<std::mutex> lock(mutex_);
            std::cout << "  [AdvancedDemoLogger] (Log): " << message << std::endl;
        }

        /**
         * @brief [实现] IAdvancedDemoLogger::LogWarning
         *
         * [受众：插件开发者]
         * 派生接口的方法。
         */
        void AdvancedDemoLogger::LogWarning(const std::string& message) {
            std::lock_guard<std::mutex> lock(mutex_);
            std::cout << "  [AdvancedDemoLogger] (WARNING): " << message << std::endl;
        }

    }  // namespace demo
}  // namespace z3y