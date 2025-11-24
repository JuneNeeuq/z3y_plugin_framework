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
  * @file test_loader_robustness.cpp
  * @brief [集成测试] 插件加载器 (Loader) 的健壮性与边界条件测试
  * * @details
  * 验证 PluginManager 在面对异常输入时的行为：
  * 1. **非法文件**: 加载不存在的文件或非 DLL 文件。
  * 2. **重复加载**: 多次加载同一个插件的幂等性。
  * 3. **卸载清理**: 验证 UnloadAllPlugins 是否彻底清理了状态。
  */

#include "common/plugin_test_base.h"
#include "framework/plugin_manager.h"
#include "interfaces_demo/i_demo_logger.h"
#include <fstream>

using namespace z3y;

class LoaderRobustnessTest : public PluginTestBase {
    // 使用默认 SetUp/TearDown
};

/**
 * @test 加载不存在的文件
 * @brief 验证 LoadPlugin 在文件缺失时返回 false 并给出错误信息。
 */
TEST_F(LoaderRobustnessTest, LoadNonExistentFile) {
    std::string err;
    bool ret = manager_->LoadPlugin("path/to/ghost_plugin.dll", err);

    EXPECT_FALSE(ret);
    EXPECT_FALSE(err.empty()); // 必须有错误信息
}

/**
 * @test 加载非法格式文件
 * @brief 创建一个伪造的 DLL (文本文件)，验证加载器能否优雅失败。
 */
TEST_F(LoaderRobustnessTest, LoadInvalidFormat) {
    // 1. 创建一个假的 .dll 文件
    std::filesystem::path fake_dll = bin_dir_ / "fake.dll";
    {
        std::ofstream f(fake_dll);
        f << "This is not a PE/ELF file";
        f.close();
    }

    // 2. 尝试加载
    std::string err;
    // 注意：在 Windows 上 LoadLibrary 可能会失败，Linux 上 dlopen 也会失败
    // 但框架首先会检查扩展名，如果是 .dll/.so 才会尝试系统调用
    bool ret = manager_->LoadPlugin(fake_dll, err);

    EXPECT_FALSE(ret);
    // 错误信息应包含系统级的加载错误 (如 "Invalid image format")
    EXPECT_FALSE(err.empty());

    // 清理
    std::filesystem::remove(fake_dll);
}

/**
 * @test 重复加载幂等性
 * @brief 验证多次加载同一个有效插件，不会导致错误或重复注册。
 */
TEST_F(LoaderRobustnessTest, Idempotency) {
    // 1. 第一次加载
    ASSERT_TRUE(LoadPlugin("plugin_demo_core_services"));

    // 2. 第二次加载 (应该返回 true，因为已加载视为成功)
    std::string err;
    // 构造完整路径以模拟真实调用
    // LoadPlugin 内部应该有检查 "is already loaded"
    // 这里我们复用 PluginTestBase::LoadPlugin 的逻辑手动调用
    // (注意：PluginTestBase::LoadPlugin 封装了路径拼接)

    ASSERT_TRUE(LoadPlugin("plugin_demo_core_services")) << "第二次加载应返回成功 (幂等)";

    // 3. 验证没有重复注册 (通过查询组件数量或尝试获取服务)
    auto logger = z3y::GetDefaultService<z3y::demo::IDemoLogger>();
    ASSERT_NE(logger, nullptr);
    // 如果重复注册，可能会抛出 "Alias already exists" 异常，或者 Log 中有警告
    // 只要上面没有 crash 或 throw，就算通过
}

/**
 * @test 卸载后状态清理
 * @brief 验证 UnloadAllPlugins 后，无法再获取服务。
 */
TEST_F(LoaderRobustnessTest, StateCleanupAfterUnload) {
    ASSERT_TRUE(LoadPlugin("plugin_demo_core_services"));

    // 确保服务可用
    EXPECT_NO_THROW(z3y::GetDefaultService<z3y::demo::IDemoLogger>());

    // 卸载
    manager_->UnloadAllPlugins();

    // 验证：再次获取服务应失败 (抛出 AliasNotFound 或 InternalError)
    EXPECT_THROW({
        z3y::GetDefaultService<z3y::demo::IDemoLogger>();
        }, z3y::PluginException);
}