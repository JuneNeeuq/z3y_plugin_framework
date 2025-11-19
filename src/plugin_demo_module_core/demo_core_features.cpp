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
 * @file demo_core_features.cpp
 * @brief z3y::demo::DemoCoreFeatures (IDemoModule 核心功能演示) 的源文件。
 * @author Yue Liu
 * @date 2025-08-10
 * @copyright Copyright (c) 2025 Yue Liu
 */

#include "demo_core_features.h"
#include <iostream>
#include "framework/z3y_define_impl.h"  // 包含所需的一切
#include "interfaces_demo/i_demo_simple.h"  // 包含 IDemoSimple 接口 (用于测试)

 // [插件开发者核心]
 // **自动注册**
 // 将 DemoCoreFeatures 注册为一个 *瞬态组件* (Component)。
 //
 // [参数说明]
 // 1. ClassName: `z3y::demo::DemoCoreFeatures`
 // 2. Alias: `"Demo.CoreFeatures"`
 // 3. IsDefault: `false`
 //
 // [设计思想：组件 (Component) vs 服务 (Service)]
 //
 // - `Z3Y_AUTO_REGISTER_SERVICE` (例如 `DemoLoggerService`)
 //   用于注册 *单例*，
 //   通过 `z3y::GetService` 获取。
 //
 // - `Z3Y_AUTO_REGISTER_COMPONENT` (例如 `DemoCoreFeatures`)
 //   用于注册 *瞬态* 实例，
 //   通过 `z3y::CreateInstance` 获取。
 //
 // `DemoRunnerService` 将通过 `IPluginQuery` 找到它
 // (因为它实现了 `IDemoModule`)，
 // 并通过 `CreateInstance` 来创建它。
Z3Y_AUTO_REGISTER_COMPONENT(z3y::demo::DemoCoreFeatures, "Demo.CoreFeatures",
    false);

namespace z3y {
    namespace demo {

        /**
         * @brief 构造函数。
         *
         * [受众：插件开发者 (最佳实践)]
         *
         * [禁忌]
         * 1. 不应获取任何服务 (例如 `z3y::Get...`)。
         * 2. 不应调用 `shared_from_this()` (此时 `weak_ptr` 尚未初始化)。
         */
        DemoCoreFeatures::DemoCoreFeatures() {
            // 构造函数中 shared_from_this() 不可用
            // 不应在此处获取服务或订阅事件
        }

        DemoCoreFeatures::~DemoCoreFeatures() {}

        /**
         * @brief [生命周期钩子]
         *
         * [受众：插件开发者 (最佳实践) 和 框架维护者]
         *
         * [!! 核心禁忌 !!]
         * **绝对不要** 在 `Initialize()` 钩子中
         * 调用 `z3y::GetService` 或 `z3y::CreateInstance`
         * 来获取 *其他* 插件的服务或组件。
         *
         * [原因：死锁风险 (Deadlock Risk)]
         * `PluginManager` 在 `GetService` 中使用 `std::call_once`
         * 来线程安全地创建单例。
         * * 假设：
         * 1. 线程 A 调用 `GetService<IDemoLogger>()`。
         * 2. `PluginManager` 锁住 `IDemoLogger` 的 `SingletonHolder`， 开始构造 `DemoLoggerService`。
         * 3. `DemoLoggerService::Initialize()` 被调用 (在线程 A 上， 仍持有锁)。
         * 4. [问题] `DemoLoggerService::Initialize()` *内部*
         * 决定 `GetService<IDemoSimple>()` (或 `CreateInstance`)。
         *
         * 5. 线程 B *同时* 调用 `GetService<IDemoSimple>()`。
         * 6. `PluginManager` 锁住 `IDemoSimple` 的 `SingletonHolder`， 开始构造 `DemoSimpleImplA`。
         * 7. `DemoSimpleImplA::Initialize()` 被调用 (在线程 B 上， 仍持有锁)。
         * 8. [问题] `DemoSimpleImplA::Initialize()` *内部* 决定 `GetService<IDemoLogger>()`。
         *
         * 9. **死锁**。
         * 线程 A 持有 Logger 锁，等待 Simple 锁。
         * 线程 B 持有 Simple 锁，等待 Logger 锁。
         *
         * [结论]
         * `Initialize()` 应该只执行 *不依赖其他插件* 的初始化。
         * 依赖项应在 *首次使用时* (例如 `RunTest` 中)
         * 通过“懒加载”来获取。
         */
        void DemoCoreFeatures::Initialize() {
            std::cout << "  [DemoCoreFeatures] Instance Initialized (Initialize() "
                "called)."
                << std::endl;
        }

        std::string DemoCoreFeatures::GetDemoName() {
            return "Core Features (GetService, CreateInstance, Exceptions)";
        }

        /**
         * @brief [核心] 执行此模块的测试。
         *
         * [受众：插件开发者 (示例)]
         * 演示了服务定位器的各种 API。
         */
        void DemoCoreFeatures::RunTest() {
            // [1. 懒加载 IDemoLogger]
            // [最佳实践]
            // 这是安全获取依赖的推荐位置。
            // 在 *首次* 需要时获取 Logger 服务。
            if (!logger_) {
                // [健壮性]
                // 使用 `TryGet...` (noexcept API) 来获取日志服务。
                if (auto [logger, err] = z3y::TryGetDefaultService<IDemoLogger>();
                    err == InstanceError::kSuccess) {
                    logger_ = logger;
                }
                else {
                    // 如果连日志服务都没有，无法继续测试，
                    // 只能打印到 cerr。
                    std::cerr << "DemoCoreFeatures cannot run: IDemoLogger is missing ("
                        << ResultToString(err) << ")." << std::endl;
                    return;
                }
            }

            // --- 开始测试 ---

            // 1. 演示 GetDefaultService (预期成功)
            logger_->Log("[CoreDemo] 1. Testing GetDefaultService<IDemoLogger>...");
            try {
                // (这是抛出异常的 API)
                auto logger_default = z3y::GetDefaultService<IDemoLogger>();
                logger_default->Log("   ...Success: GetDefaultService<IDemoLogger> works.");
            }
            catch (const z3y::PluginException& e) {
                logger_->Log("   ...[FAIL] " + std::string(e.what()));
            }

            // 2. 演示 CreateDefaultInstance (预期成功)
            logger_->Log(
                "[CoreDemo] 2. Testing CreateDefaultInstance<IDemoSimple> (Demo.Simple.A)...");
            try {
                // "Demo.Simple.A" 被注册为 IDemoSimple 的默认实现
                auto simple_a = z3y::CreateDefaultInstance<IDemoSimple>();
                logger_->Log("   ...Success: " + simple_a->GetSimpleString());
            }
            catch (const z3y::PluginException& e) {
                logger_->Log("   ...[FAIL] " + std::string(e.what()));
            }

            // 3. 演示 CreateInstance (按别名) (预期成功)
            logger_->Log("[CoreDemo] 3. Testing CreateInstance<IDemoSimple>('Demo.Simple.B')...");
            try {
                // "Demo.Simple.B" 是 IDemoSimple 的非默认实现
                auto simple_b = z3y::CreateInstance<IDemoSimple>("Demo.Simple.B");
                logger_->Log("   ...Success: " + simple_b->GetSimpleString());
            }
            catch (const z3y::PluginException& e) {
                logger_->Log("   ...[FAIL] " + std::string(e.what()));
            }

            // 4. 演示 Try... API (预期 kErrorAliasNotFound)
            logger_->Log(
                "[CoreDemo] 4. Testing Try... API (kErrorAliasNotFound)...");
            // (这是 noexcept API，返回 std::pair)
            if (auto [simple_c, err] =
                z3y::TryCreateInstance<IDemoSimple>("Alias.That.Does.Not.Exist");
                err == InstanceError::kErrorAliasNotFound) {
                logger_->Log("   ...Success: Caught expected kErrorAliasNotFound.");
            }
            else if (err == InstanceError::kSuccess) {
                logger_->Log("   ...[FAIL] Did not fail!");
            }
            else {
                logger_->Log("   ...[FAIL] Caught wrong error: " + ResultToString(err));
            }

            // 5. 演示 Try... API (预期 kErrorNotAService)
            logger_->Log("[CoreDemo] 5. Testing Try... API (kErrorNotAService)...");
            // "Demo.Simple.A" 是一个瞬态组件 (Component)，
            // 但我们 *错误地* 使用 `TryGetService` (用于单例服务) 来获取它。
            if (auto [simple_a_service, err] = z3y::TryGetService<IDemoSimple>("Demo.Simple.A");
                err == InstanceError::kErrorNotAService) {
                logger_->Log("   ...Success: Caught expected kErrorNotAService.");
            }
            else if (err == InstanceError::kSuccess) {
                logger_->Log("   ...[FAIL] Did not fail!");
            }
            else {
                logger_->Log("   ...[FAIL] Caught wrong error: " + ResultToString(err));
            }

            // 6. 演示 Try... API (预期 kErrorNotAComponent)
            logger_->Log("[CoreDemo] 6. Testing Try... API (kErrorNotAComponent)...");
            // "Demo.Logger.Default" 是一个单例服务 (Service)，
            // 但我们 *错误地* 使用 `TryCreateInstance` (用于瞬态组件) 来获取它。
            if (auto [logger_instance, err] =
                z3y::TryCreateInstance<IDemoLogger>("Demo.Logger.Default");
                err == InstanceError::kErrorNotAComponent) {
                logger_->Log("   ...Success: Caught expected kErrorNotAComponent.");
            }
            else if (err == InstanceError::kSuccess) {
                logger_->Log("   ...[FAIL] Did not fail!");
            }
            else {
                logger_->Log("   ...[FAIL] Caught wrong error: " + ResultToString(err));
            }
        }

    }  // namespace demo
}  // namespace z3y