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
 * @file introspection_features.cpp
 * @brief z3y::demo::IntrospectionFeatures (IDemoModule 内省功能演示) 的源文件。
 * @author Yue Liu
 * @date 2025-08-17
 * @copyright Copyright (c) 2025 Yue Liu
 */

#include "introspection_features.h"
#include <iostream>
#include "framework/z3y_define_impl.h"
#include "interfaces_demo/i_advanced_demo_logger.h"  // 演示：接口继承
#include "interfaces_demo/i_demo_simple.h"          // 演示：版本不匹配

 // [插件开发者核心]
 // **自动注册**
 // 将 IntrospectionFeatures 注册为一个 *瞬态组件* (Component)。
 //
 // [参数说明]
 // 1. ClassName: `z3y::demo::IntrospectionFeatures`
 // 2. Alias: `"Demo.Introspection"`
 // 3. IsDefault: `false`
Z3Y_AUTO_REGISTER_COMPONENT(z3y::demo::IntrospectionFeatures,
    "Demo.Introspection", false);

// [受众：插件开发者 (高级示例)]
//
// **[内部演示] 接口版本不匹配**
//
// 我们在 .cpp 内部 *故意*
// 定义一个 `IDemoSimple` 的 V2 版本。
//
// [!! 关键 !!]
// 它的 IID (`"z3y-demo-ISimple-IID-A4736128"`)
// *故意* 与 `interfaces_demo/i_demo_simple.h`
// 中定义的 `IDemoSimple` (V1.0) 保持 *一致*。
//
// 但是，它的主版本号 (Major Version) 是 `2`。
//
// 在 `RunTest()` 中，我们将演示
// 尝试使用 `PluginCast<IDemoSimpleV2>`
// 去获取一个 `IDemoSimple` (V1.0) 的实现时，
// 框架会如何正确地抛出 `kErrorVersionMajorMismatch` 异常。
//
class IDemoSimpleV2 : public virtual z3y::IComponent {
public:
    Z3Y_DEFINE_INTERFACE(IDemoSimpleV2, "z3y-demo-IDemoSimple-IID-A4736128", 2,
        0);
    virtual std::string GetSimpleStringV2() = 0;
};

namespace z3y {
    namespace demo {

        IntrospectionFeatures::IntrospectionFeatures() {}
        IntrospectionFeatures::~IntrospectionFeatures() {}

        /**
         * @brief [生命周期钩子]
         *
         * [受众：插件开发者 (最佳实践)]
         * [禁忌]
         * **不要** 在此函数中获取 *其他* 插件的服务
         * (有死锁风险)。
         */
        void IntrospectionFeatures::Initialize() {
            std::cout << "  [IntrospectionFeatures] Instance Initialized "
                "(Initialize() called)."
                << std::endl;
        }

        std::string IntrospectionFeatures::GetDemoName() {
            return "Introspection (IPluginQuery) & Advanced Features";
        }

        /**
         * @brief [核心] 执行此模块的测试。
         *
         * [受众：插件开发者 (高级示例)]
         */
        void IntrospectionFeatures::RunTest() {
            // [1. 懒加载依赖]
            // 在测试开始时获取所需的 IDemoLogger 和 IPluginQuery 服务。
            try {
                if (!logger_) {
                    logger_ = z3y::GetDefaultService<IDemoLogger>();
                }
                if (!query_) {
                    // [演示]
                    // 获取核心服务 IPluginQuery。
                    // `clsid::kPluginQuery` 在 `framework/i_plugin_query.h` 中定义。
                    query_ = z3y::GetService<IPluginQuery>(clsid::kPluginQuery);
                }
            }
            catch (const z3y::PluginException& e) {
                // [健壮性]
                // 如果连 ILogger 或 IPluginQuery 都拿不到，测试无法运行。
                std::cerr
                    << "IntrospectionFeatures failed to get services in RunTest: "
                    << e.what() << std::endl;
                return;
            }

            logger_->Log("======= [IntrospectionDemo] Starting =======");

            // 1. [演示] GetLoadedPluginFiles
            logger_->Log("[IntroDemo] 1. Testing GetLoadedPluginFiles()...");
            auto files = query_->GetLoadedPluginFiles();
            for (const auto& file : files) {
                logger_->Log("   ... Loaded: " + file);
            }

            // 2. [演示] GetComponentDetailsByAlias
            logger_->Log(
                "[IntroDemo] 2. Testing GetComponentDetailsByAlias('Demo.Logger.Default')...");
            ComponentDetails details;
            if (query_->GetComponentDetailsByAlias("Demo.Logger.Default", details)) {
                logger_->Log("   ... Found: " + details.alias);
                logger_->Log("   ... IsSingleton: " +
                    std::to_string(details.is_singleton));
                logger_->Log("   ... IsDefault: " +
                    std::to_string(details.is_registered_as_default));
            }
            else {
                logger_->Log("   ... [FAIL] Could not find 'Demo.Logger.Default'!");
            }

            // 3. [演示] GetComponentsFromPlugin
            logger_->Log(
                "[IntroDemo] 3. Testing GetComponentsFromPlugin (for core_services)...");
            //
            // [设计]
            // 我们从上一步 (details)中获取到了 "Demo.Logger.Default" 的来源路径，
            // 现在我们用这个路径反向查询该 DLL 注册了哪些组件。
            std::string core_plugin_path = details.source_plugin_path;
            if (!core_plugin_path.empty()) {
                auto core_components = query_->GetComponentsFromPlugin(core_plugin_path);
                logger_->Log("   ... Found " + std::to_string(core_components.size()) +
                    " components in that plugin:");
                for (const auto& c : core_components) {
                    logger_->Log("       - " + c.alias);
                }
            }

            // 4. [演示] GetAllComponents (仅计数)
            logger_->Log("[IntroDemo] 4. Testing GetAllComponents()...");
            auto all_components = query_->GetAllComponents();
            logger_->Log("   ... Total components registered: " +
                std::to_string(all_components.size()));

            // --- 高级特性演示 ---

            logger_->Log("\n======= [Advanced Features Demo] Starting =======");

            // 5. [演示] 接口继承
            // (IAdvancedDemoLogger 继承自 IDemoLogger)
            logger_->Log(
                "[AdvancedDemo] 5. Testing Interface Inheritance (IAdvancedDemoLogger)...");
            try {
                // 5a.
                // 获取 "Demo.Logger.Advanced" 实例
                // (它实现了 IAdvancedDemoLogger 和 IDemoLogger)
                auto advanced_logger =
                    z3y::GetService<IAdvancedDemoLogger>("Demo.Logger.Advanced");
                // 调用派生接口的方法
                advanced_logger->LogWarning("Test LogWarning");
                // 调用基类接口的方法
                advanced_logger->Log("Test Log via Advanced");

                // 5b. [关键]
                // 再次获取 *同名* 实例 ("Demo.Logger.Advanced")，
                // (由于是单例 Service， `GetServiceImpl` 会返回同一个 `SingletonHolder` 实例)
                // 但这次 *只* 请求基类接口 `IDemoLogger` (这将触发一次新的 `PluginCast<IDemoLogger>`)
                auto base_logger = z3y::GetService<IDemoLogger>("Demo.Logger.Advanced");
                // 调用基类接口的方法
                base_logger->Log("Test Log via Base");

                logger_->Log("   ... Success: Interface inheritance and multiple "
                    "QueryInterface casts work.");
            }
            catch (const z3y::PluginException& e) {
                logger_->Log("   ... [FAIL] " + std::string(e.what()));
            }

            // 6. [演示] 版本不匹配
            logger_->Log(
                "[AdvancedDemo] 6. Testing Version Mismatch (IDemoSimpleV2 vs V1)...");
            try {
                // "Demo.Simple.A"
                // 实现了 `IDemoSimple`(V1.0)。
                // 我们故意使用 `IDemoSimpleV2`(V2.0)
                // (它们共享同一个 IID) 来 *创建* 它。
                //
                // [受众：框架维护者]
                // `CreateInstance` -> `CreateInstanceImpl` (成功)
                // -> `PluginCast<IDemoSimpleV2>` (失败)
                // -> `QueryInterfaceRaw(IID_Simple, 2, 0)`
                // -> `PluginImpl::QueryRecursive`
                // -> `if (my_major != host_major)` (1 != 2)
                // -> `return kErrorVersionMajorMismatch`
                auto simple_v2 = z3y::CreateInstance<IDemoSimpleV2>("Demo.Simple.A");
                logger_->Log(
                    "   ... [FAIL] Did not throw exception for version mismatch!");
            }
            catch (const z3y::PluginException& e) {
                // [核心]
                // 捕获 `PluginException` 并检查 *具体* 的错误码。
                if (e.GetError() == InstanceError::kErrorVersionMajorMismatch) {
                    logger_->Log(
                        "   ... Success: Caught expected "
                        "kErrorVersionMajorMismatch.");
                }
                else {
                    logger_->Log("   ... [FAIL] Caught wrong exception: " +
                        std::string(e.what()));
                }
            }

            logger_->Log("======= [IntrospectionDemo] Finished =======");
        }

    }  // namespace demo
}  // namespace z3y