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
  * @file test_introspection.cpp
  * @brief [集成测试] 框架内省 (Introspection) 能力测试
  * * @details
  * 验证 IPluginQuery 服务能否正确反映框架当前的运行时状态：
  * 1. **全量查询**: 获取所有已注册组件。
  * 2. **条件查询**: 根据接口 IID 查找组件。
  * 3. **元数据验证**: 检查别名、单例标志、来源插件路径等信息是否准确。
  */

#include "common/plugin_test_base.h"
#include "framework/i_plugin_query.h"
#include "interfaces_demo/i_demo_logger.h"

using namespace z3y;

class IntrospectionTest : public PluginTestBase {
protected:
    void SetUp() override {
        PluginTestBase::SetUp();
        ASSERT_TRUE(LoadPlugin("plugin_demo_core_services"));

        // 获取内省服务
        query_ = z3y::GetService<IPluginQuery>(clsid::kPluginQuery);
        ASSERT_NE(query_, nullptr);
    }

    PluginPtr<IPluginQuery> query_;
};

/**
 * @test 查找实现特定接口的组件
 * @brief 验证 FindComponentsImplementing 能找到所有 Logger 实现。
 */
TEST_F(IntrospectionTest, FindComponentsByInterface) {
    using namespace z3y::demo;

    // 查找所有实现了 IDemoLogger 的组件
    auto loggers = query_->FindComponentsImplementing(IDemoLogger::kIid);

    // 预期至少有两个：DemoLoggerService (默认) 和 AdvancedDemoLogger
    EXPECT_GE(loggers.size(), 2);

    bool found_default = false;
    bool found_advanced = false;

    for (const auto& detail : loggers) {
        if (detail.alias == "Demo.Logger.Default") found_default = true;
        if (detail.alias == "Demo.Logger.Advanced") found_advanced = true;

        // 验证元数据完整性
        EXPECT_FALSE(detail.source_plugin_path.empty()) << "组件必须包含来源插件路径";
        EXPECT_EQ(detail.is_singleton, true) << "Logger 应该是单例服务";
    }

    EXPECT_TRUE(found_default);
    EXPECT_TRUE(found_advanced);
}

/**
 * @test 获取已加载插件列表
 * @brief 验证 GetLoadedPluginFiles 能列出我们加载的插件。
 */
TEST_F(IntrospectionTest, VerifyLoadedFiles) {
    auto files = query_->GetLoadedPluginFiles();
    ASSERT_FALSE(files.empty());

    bool found_core_plugin = false;
    for (const auto& path : files) {
        // 检查路径中是否包含插件名 (跨平台简单检查)
        if (path.find("plugin_demo_core_services") != std::string::npos) {
            found_core_plugin = true;

            // 进一步验证：反查该文件注册的组件
            auto components = query_->GetComponentsFromPlugin(path);
            EXPECT_GT(components.size(), 0) << "该插件应该注册了组件";
        }
    }
    EXPECT_TRUE(found_core_plugin) << "GetLoadedPluginFiles 应该包含已加载的插件";
}

/**
 * @test 通过别名精确查询
 * @brief 验证 GetComponentDetailsByAlias 的准确性。
 */
TEST_F(IntrospectionTest, GetDetailsByAlias) {
    ComponentDetails details;
    bool found = query_->GetComponentDetailsByAlias("Demo.Logger.Default", details);

    ASSERT_TRUE(found);
    EXPECT_EQ(details.alias, "Demo.Logger.Default");
    EXPECT_TRUE(details.is_registered_as_default); // 它是默认实现
    EXPECT_TRUE(details.is_singleton);
}