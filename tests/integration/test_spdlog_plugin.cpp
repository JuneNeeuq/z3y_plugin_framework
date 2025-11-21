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
 * @file test_spdlog_plugin.cpp
 * @brief 日志插件的全功能集成测试
 * @details
 * 覆盖范围：
 * 1. 服务注册与获取
 * 2. 配置文件加载 (正常 vs 异常)
 * 3. 宏调用安全性
 * 4. 动态调级 (SetLevel) 及其持久化特性
 * 5. 强制刷盘 (Flush)
 */

#include "common/plugin_test_base.h"
#include "interfaces_core/i_log_service.h"
#include "interfaces_core/z3y_log_macros.h" // 用于测试宏调用

using namespace z3y;
using namespace z3y::interfaces::core;

/**
 * @brief 定义日志测试套件
 * @details 继承自 PluginTestBase，复用其 SetUp/TearDown 能力。
 */
class SpdlogPluginTest : public PluginTestBase {
protected:
    // [重写 SetUp]
    // 我们希望在测试日志具体功能前，插件已经被加载好了。
    void SetUp() override {
        PluginTestBase::SetUp(); // [重要] 必须先调用基类的 SetUp 初始化 Manager

        // [GTest 断言] ASSERT_TRUE
        // 含义：如果加载失败，这是一个致命错误，当前测试函数立即终止。
        // 原理：没加载成功，后面的测试代码可能会崩溃 (空指针)，所以必须终止。
        ASSERT_TRUE(LoadPlugin("plugin_spdlog_logger"));
    }
};

/**
 * @test 验证服务是否成功注册到框架
 */
TEST_F(SpdlogPluginTest, ServiceIsRegistered) {
    // 尝试获取服务接口
    auto result = z3y::TryGetDefaultService<ILogManagerService>();

    // [GTest 断言] EXPECT_EQ
    // 含义：期待两个值相等。如果不等，测试标记为失败，但继续执行。
    EXPECT_EQ(result.second, InstanceError::kSuccess) << "Service should be registered upon plugin load";
    EXPECT_NE(result.first, nullptr);
}

/**
 * @test 验证初始化：配置文件丢失时的容错性 (Fallback机制)
 */
TEST_F(SpdlogPluginTest, InitFailsWithMissingConfig_ShouldFallback) {
    auto log_mgr = z3y::GetDefaultService<ILogManagerService>();

    // 1. 传入一个不存在的路径
    bool success = log_mgr->InitializeService("non_existent_config.json", "logs");

    // 期待返回 false，表示文件读取失败
    EXPECT_FALSE(success) << "Should return false for missing config file";

    // 2. 验证 Fallback Logger 是否可用
    // 即使初始化失败，GetLogger 也应该返回一个能用的对象 (输出到控制台)，而不是 nullptr
    auto logger = log_mgr->GetLogger("Test.Fallback");
    ASSERT_NE(logger, nullptr) << "Should return a fallback logger even if init failed";

    // 尝试打印日志，确保不崩溃
    Z3Y_LOG_INFO(logger, "This is a fallback log message");
}

/**
 * @test 验证初始化：正常配置文件加载
 */
TEST_F(SpdlogPluginTest, InitSuccessWithValidConfig) {
    auto log_mgr = z3y::GetDefaultService<ILogManagerService>();

    // 我们假设 CMake 已经把 logger_config.json 复制到了 bin 目录 (在 CMakeLists.txt 中配置)
    std::filesystem::path config_path = bin_dir_ / "logger_config.json";
    std::filesystem::path log_root = bin_dir_ / "test_logs";

    // 1. 初始化
    bool success = log_mgr->InitializeService(config_path.string(), log_root.string());

    // 如果文件没复制过来，这个断言会失败。这能帮我们检查 CMake 配置对不对。
    ASSERT_TRUE(success) << "Failed to init with config at: " << config_path;

    // 2. 获取 Logger
    auto logger = log_mgr->GetLogger("System.Test");
    ASSERT_NE(logger, nullptr);

    // 3. 验证宏调用 (编译期和运行期检查)
    // 确保宏展开后的代码能正常运行，并且 LogSourceLocation 能正确捕获
    Z3Y_LOG_INFO(logger, "Integration test log: {}", 12345);
}

/**
 * @test 验证核心功能：动态日志等级调整 (SetLevel)
 * @details 这是一个非常关键的功能测试，验证了 SetLevel 是否能影响 现有 和 未来 的 Logger。
 */
TEST_F(SpdlogPluginTest, DynamicSetLevel_Persistence) {
    auto log_mgr = z3y::GetDefaultService<ILogManagerService>();

    // 先初始化，确保非 Fallback 模式
    log_mgr->InitializeService((bin_dir_ / "logger_config.json").string(), (bin_dir_ / "logs").string());

    // 场景 1: 获取一个现有的 Logger "Network.Tcp"
    auto logger_net = log_mgr->GetLogger("Network.Tcp");

    // 假设配置默认是 Info，所以 Trace 应该是禁用的
    // (注意：这依赖于配置文件默认不是 Trace，如果不确定，可以先 SetLevel 强制重置)
    log_mgr->SetLevel("", LogLevel::Info); // 全局设为 Info
    EXPECT_FALSE(logger_net->IsEnabled(LogLevel::Trace));

    // 场景 2: 动态调整 "Network" 前缀为 Trace
    log_mgr->SetLevel("Network", LogLevel::Trace);

    // 验证：现有的 logger_net 应该被更新为 Trace
    EXPECT_TRUE(logger_net->IsEnabled(LogLevel::Trace)) << "Existing logger should be updated";

    // 场景 3: 创建一个新的 Logger "Network.Http"
    // 验证：新创建的 Logger 应该自动继承 "Network" 前缀的规则 (持久化验证)
    auto logger_new = log_mgr->GetLogger("Network.Http");
    EXPECT_TRUE(logger_new->IsEnabled(LogLevel::Trace)) << "New logger should inherit dynamic rule";

    // 场景 4: 验证隔离性，"Business" 模块不应受影响
    auto logger_biz = log_mgr->GetLogger("Business.Order");
    EXPECT_FALSE(logger_biz->IsEnabled(LogLevel::Trace)) << "Other modules should not be affected";
}

/**
 * @test 验证强制刷盘 (Smoke Test)
 */
TEST_F(SpdlogPluginTest, FlushDoesNotCrash) {
    auto log_mgr = z3y::GetDefaultService<ILogManagerService>();
    // 即使未初始化，Flush 也不应崩溃
    log_mgr->Flush();

    // 初始化后再 Flush
    log_mgr->InitializeService((bin_dir_ / "logger_config.json").string(), "logs");
    log_mgr->Flush();
}