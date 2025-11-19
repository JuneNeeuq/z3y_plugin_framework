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
 * @file demo_logger_service.cpp
 * @brief z3y::demo::DemoLoggerService (IDemoLogger 接口实现) 的源文件。
 * @author Yue Liu
 * @date 2025-08-02
 * @copyright Copyright (c) 2025 Yue Liu
 */

#include "demo_logger_service.h"
#include <iostream>  // 用于 std::cout
#include <mutex>     // 用于 std::lock_guard
#include "framework/z3y_define_impl.h"  // 包含 Z3Y_AUTO_REGISTER_SERVICE

 // [插件开发者核心]
 // **自动注册**
 // 将 DemoLoggerService 类注册为一个 *单例服务* (Service)。
 //
 // [参数说明]
 // 1. ClassName: `z3y::demo::DemoLoggerService` (实现类)
 // 2. Alias: `"Demo.Logger.Default"` (用于 `z3y::GetService("...")` 的别名)
 // 3. IsDefault: `true`
 //    - `true` 表示：
 //      `z3y::GetDefaultService<IDemoLogger>()` 将会找到 *这个* 实现。
 //    - `false` 表示：
 //      只能通过别名 `"Demo.Logger.Default"` 获取。
Z3Y_AUTO_REGISTER_SERVICE(z3y::demo::DemoLoggerService, "Demo.Logger.Default", true);

namespace z3y {
    namespace demo {

        DemoLoggerService::DemoLoggerService() {
            // [受众：框架维护者]
            // 构造函数和析构函数也应该受锁保护，
            // 这是一个好习惯，
            // 尽管 `std::call_once` 保证了构造函数只被调用一次。
            std::lock_guard lock(mutex_);
            std::cout << "  [DemoLoggerService] Service Created (Constructor)." << std::endl;
        }

        DemoLoggerService::~DemoLoggerService() {
            std::lock_guard lock(mutex_);
            std::cout << "  [DemoLoggerService] Service Destroyed (Destructor)."
                << std::endl;
        }

        /**
         * @brief [线程安全实现] IDemoLogger::Log
         *
         * [受众：插件开发者 (最佳实践)]
         *
         * 使用 `std::lock_guard` (RAII 锁)
         * 来确保对共享资源 (`std::cout`) 的访问是互斥的，
         * 防止来自不同线程的日志消息交错。
         */
        void DemoLoggerService::Log(const std::string& message) {
            std::lock_guard lock(mutex_);
            std::cout << "  [DemoLoggerService] " << message << std::endl;
        }

    }  // namespace demo
}  // namespace z3y