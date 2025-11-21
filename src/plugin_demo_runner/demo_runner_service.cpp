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
 * @file demo_runner_service.cpp
 * @brief z3y::demo::DemoRunnerService (IDemoRunner 接口实现) 的源文件。
 * @author Yue Liu
 * @date 2025-08-09
 * @copyright Copyright (c) 2025 Yue Liu
 */

#include "demo_runner_service.h"
#include <iostream>
#include "framework/z3y_define_impl.h"  // 包含所需的一切
#include "interfaces_demo/i_demo_module.h"  // [核心依赖] 依赖 IDemoModule 接口

 // [插件开发者核心]
 // **自动注册**
 // 将 DemoRunnerService 注册为 *单例服务* (Service)。
 //
 // [参数说明]
 // 1. ClassName: `z3y::demo::DemoRunnerService`
 // 2. Alias: `"Demo.Runner"`
 // 3. IsDefault: `true`
 //    - `true` 表示：
 //      `z3y::GetDefaultService<IDemoRunner>()`
 //      (宿主 `main.cpp` 调用的) 将会找到 *这个* 实现。
Z3Y_AUTO_REGISTER_SERVICE(z3y::demo::DemoRunnerService, "Demo.Runner", true);

namespace z3y {
    namespace demo {

        /**
         * @brief 构造函数。
         *
         * [受众：插件开发者 (最佳实践)]
         *
         * [禁忌]
         * 构造函数中 **不应** 获取任何服务 (例如 `z3y::Get...`)，
         * 以避免循环依赖和启动死锁风险。
         * 依赖项应在首次使用时（`RunAllDemos`）进行“懒加载”。
         */
        DemoRunnerService::DemoRunnerService() {
            // 不在此处获取服务
        }

        DemoRunnerService::~DemoRunnerService() {
            // [受众：插件开发者 (最佳实践)]
            //
            // [禁忌]
            // 析构函数中 **不应** 调用依赖于其他插件的服务
            // (例如 `logger_->Log`)。
            //
            // [原因]
            // 此时 `logger_` (如果它来自另一个插件) 可能已经被销毁，
            // 导致悬空指针和崩溃。
            //
            // [替代方案]
            // 应使用 `Shutdown()` 钩子执行安全的清理日志记录。
            std::cout << "  [DemoRunnerService] Destructor called." << std::endl;
        }

        /**
         * @brief [生命周期钩子]
         *
         * [受众：插件开发者 (最佳实践)]
         *
         * [设计思想：安全关闭 (Safe Shutdown)]
         * 这是在插件卸载时安全执行清理代码的地方。
         * `PluginManager` 保证在调用 `Shutdown()` 时，
         * 所有服务 *仍然存在* (尽管它们可能也即将被关闭)。
         *
         * [关键]
         * 我们使用 `TryGet...` (noexcept API) 来获取 `IDemoLogger`。
         * 这是在 `Shutdown` 和析构函数等
         * “不能抛出异常” 的上下文中 *必须* 遵循的最佳实践。
         */
        void DemoRunnerService::Shutdown() {
            if (auto [logger, err] = z3y::TryGetDefaultService<IDemoLogger>();
                err == InstanceError::kSuccess) {
                logger->Log("DemoRunnerService Shutting Down (safe).");
            }
        }

        /**
         * @brief [核心实现] 查找并执行所有演示模块。
         *
         * [受众：插件开发者 (高级示例)]
         *
         * [设计思想：服务发现 (Service Discovery)]
         * 这演示了框架的内省能力。
         * 此函数不“硬编码”任何模块，
         * 而是向框架查询实现了特定接口（`IDemoModule`） 的所有组件。
         */
        void DemoRunnerService::RunAllDemos() {
            // [1. 懒加载依赖]
            // 在执行前，尝试获取我们依赖的 `IDemoLogger` 服务。
            if (auto [logger, err] = z3y::TryGetDefaultService<IDemoLogger>();
                err == InstanceError::kSuccess) {
                logger_ = logger; // 缓存指针
            } else {
                // [健壮性]
                // 这是一个关键失败，我们没有 `logger_` 就无法报告结果。
                // (由于 logger_ 获取失败，只能打印到 cerr)
                std::cerr << "DemoRunnerService failed to get IDemoLogger: "
                    << ResultToString(err) << std::endl;
                return;
            }

            logger_->Log("======= [DemoRunner] Starting All Demo Modules =======");

            try {
                // [2. 获取内省服务]
                // 使用服务定位器获取 `IPluginQuery` 服务。
                // 这是一个关键依赖，如果获取失败，
                // 我们通过 `try-catch` 捕获异常。
                auto query = z3y::GetService<IPluginQuery>(clsid::kPluginQuery);

                // [3. 查找所有模块]
                // [核心设计]
                // 我们向框架查询：
                // “请给我所有实现了 `IDemoModule` 接口的组件的元数据。”
                auto modules = query->FindComponentsImplementing(IDemoModule::kIid);

                logger_->Log("Found " + std::to_string(modules.size()) + " demo modules.");

                // [4. 遍历并执行]
                for (const auto& module_details : modules) {
                    logger_->Log("---== Executing Demo: " + module_details.alias + " ==---");

                    // [4a. 创建实例]
                    // `IDemoModule` 是一个瞬态组件 (Component)，
                    // 我们必须使用 `CreateInstance`
                    // (或 `TryCreateInstance`) 来创建新实例。
                    //
                    // (使用 `TryCreateInstance`
                    // 是一种更健壮的方式，即使某个模块创建失败，也不会中止循环)
                    auto [demo_module, err] =
                        z3y::TryCreateInstance<IDemoModule>(module_details.clsid);

                    if (err != InstanceError::kSuccess) {
                        logger_->Log("[ERROR] Failed to create demo module '" +
                            module_details.alias + "': " + ResultToString(err));
                        continue; // 继续下一个模块
                    }

                    // [4b. 执行测试]
                    demo_module->RunTest();

                    logger_->Log("---== Finished Demo: " + module_details.alias + " ==---");
                }
            } catch (const z3y::PluginException& e) {
                // [健壮性]
                // 如果 `GetService<IPluginQuery>` 失败， 我们会捕获到异常。
                logger_->Log("[FATAL] DemoRunnerService failed to get IPluginQuery: " +
                    std::string(e.what()));
            }

            logger_->Log("======= [DemoRunner] All Demo Modules Finished =======");
        }

    }  // namespace demo
}  // namespace z3y