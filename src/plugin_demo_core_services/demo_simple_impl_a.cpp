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
 * @file demo_simple_impl_a.cpp
 * @brief z3y::demo::DemoSimpleImplA (IDemoSimple 接口实现 A) 的源文件。
 * @author Yue Liu
 * @date 2025-08-03
 * @copyright Copyright (c) 2025 Yue Liu
 */

#include "demo_simple_impl_a.h"
#include <iostream>
#include "framework/z3y_define_impl.h"  // 包含 Z3Y_AUTO_REGISTER_COMPONENT

 // [插件开发者核心]
 // **自动注册**
 // 将 DemoSimpleImplA 注册为一个 *瞬态组件* (Component)。
 //
 // [参数说明]
 // 1. ClassName: `z3y::demo::DemoSimpleImplA`
 // 2. Alias: `"Demo.Simple.A"`
 // 3. IsDefault: `true`
 //    - `true` 表示：
 //      `z3y::CreateDefaultInstance<IDemoSimple>()` 将会找到 *这个* 实现。
 //    - (与 `Z3Y_AUTO_REGISTER_SERVICE` 不同，
 //      此宏用于创建新实例，而不是获取单例)
Z3Y_AUTO_REGISTER_COMPONENT(z3y::demo::DemoSimpleImplA, "Demo.Simple.A", true);

namespace z3y {
    namespace demo {

        DemoSimpleImplA::DemoSimpleImplA() {
            std::cout << "  [DemoSimpleImplA] Instance Created." << std::endl;
        }

        DemoSimpleImplA::~DemoSimpleImplA() {
            std::cout << "  [DemoSimpleImplA] Instance Destroyed." << std::endl;
        }

        /**
         * @brief [实现] IDemoSimple::GetSimpleString
         *
         * [受众：插件开发者 (最佳实践)]
         *
         * [设计思想：懒加载 (Lazy Loading)]
         * 这是在插件中安全获取依赖服务的 **推荐模式**。
         *
         * 1. 检查成员变量 `logger_` 是否已经缓存 (`!logger_`)。
         * 2. 如果未缓存，使用 `z3y::TryGetDefaultService` (noexcept API) 来获取它。
         * 3. (健壮性) 这种方式可以确保即使在 `Shutdown`
         * 等上下文被调用时， 也不会因抛出异常而导致程序崩溃。
         */
        std::string DemoSimpleImplA::GetSimpleString() {
            // [懒加载]
            if (!logger_) {
                // [Fix] 使用 TryGet... (noexcept API) 来安全地获取可选依赖，
                // 即使在 Shutdown 期间被调用也能保证安全。
                if (auto [logger, err] = z3y::TryGetDefaultService<z3y::demo::IDemoLogger>();
                    err == InstanceError::kSuccess) {
                    // 首次使用时获取服务，并缓存到成员变量中
                    logger_ = logger;
                } else if (err != InstanceError::kSuccess) {
                    // 如果日志服务不存在，打印到 cerr
                    std::cerr << "  [DemoSimpleImplA] Failed to get logger: "
                        << ResultToString(err) << std::endl;
                }
            }

            if (logger_) {
                logger_->Log("DemoSimpleImplA::GetSimpleString() was called.");
            }
            return "Hello from DemoSimpleImplA (and I just logged a message!)";
        }

    }  // namespace demo
}  // namespace z3y