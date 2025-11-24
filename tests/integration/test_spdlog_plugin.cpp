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
 * @brief [集成测试] 日志插件全功能验证 (包含自动配置生成)
 * @details
 * 本文件包含对 spdlog 日志插件的完整测试。
 * 为了解决测试环境可能缺失配置文件的问题，本测试会在运行时自动生成
 * 临时的 `logger_config_test.json`，确保测试的独立性和稳定性。
 *
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
#include <fstream> // 用于写入配置文件

#ifdef _WIN32
#include <Windows.h>
#endif

using namespace z3y;
using namespace z3y::interfaces::core;

/**
 * @brief 定义日志测试套件
 * @details 继承自 PluginTestBase，复用其 PluginManager 管理能力。
 */
class SpdlogPluginTest : public PluginTestBase {
protected:
    // [重写 SetUp]
    // 在每个测试开始前，加载日志插件，确保环境就绪
    void SetUp() override {
        PluginTestBase::SetUp(); // [重要] 必须先调用基类的 SetUp 初始化 Manager

        // [GTest 断言] 加载插件 DLL
        // 如果加载失败，后续测试无法进行，直接终止当前用例
        ASSERT_TRUE(LoadPlugin("plugin_spdlog_logger"));
    }

    std::string PathToUtf8(const std::filesystem::path& path) {
#ifdef _WIN32
        std::wstring wstr = path.wstring();
        if (wstr.empty()) return "";
        int size = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
        std::string str(size, 0);
        WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &str[0], size, NULL, NULL);
        return str;
#else
        return path.string();
#endif
    }

    // [新增辅助函数] 自动生成测试用的配置文件
    // 解决 "运行目录下缺少 logger_config.json 导致测试失败" 的问题
    std::string GenerateTestConfig() {
        std::string filename = "logger_config_test.json";
        std::filesystem::path config_path = bin_dir_ / filename;

        // 构造一个最小化的有效 JSON 配置
        // 包含一个控制台输出 (console) 和一个文件输出 (file)
        // 默认等级设为 Info
        const char* json_content = R"({
            "global_settings": {
                "format_pattern": "[%H:%M:%S] [%n] [%l] %v",
                "async_queue_size": 4096,
                "flush_interval_seconds": 1
            },
            "sinks": {
                "console_out": {
                    "type": "stdout_color_sink",
                    "level": "trace" 
                },
                "file_out": {
                    "type": "rotating_file_sink",
                    "base_name": "logs/test_app.log",
                    "max_size": 1048576,
                    "max_files": 3,
                    "level": "trace"
                }
            },
            "default_rule": {
                "sinks": ["console_out", "file_out"]
            }
        })";

        // 写入文件
        std::ofstream out(config_path);
        if (out.is_open()) {
            out << json_content;
            out.close();
        } else {
            std::cerr << "[Test] Failed to create config file: " << config_path << std::endl;
        }

        // 返回文件名供测试使用
        return filename;
    }
};

/**
 * @test 验证服务是否成功注册到框架
 * @brief 插件加载后，应该能通过服务定位器找到 ILogManagerService。
 */
TEST_F(SpdlogPluginTest, ServiceIsRegistered) {
    // 尝试获取服务接口
    auto result = z3y::TryGetDefaultService<ILogManagerService>();

    // 验证结果
    EXPECT_EQ(result.second, InstanceError::kSuccess) << "Service should be registered upon plugin load";
    EXPECT_NE(result.first, nullptr);
}

/**
 * @test 验证初始化：配置文件丢失时的容错性 (Fallback机制)
 * @brief 故意传入不存在的文件名，预期初始化失败，但系统不应崩溃。
 */
TEST_F(SpdlogPluginTest, InitFailsWithMissingConfig_ShouldFallback) {
    auto log_mgr = z3y::GetDefaultService<ILogManagerService>();

    // 1. 传入一个不存在的路径
    // 预期：InitializeService 返回 false
    bool success = log_mgr->InitializeService("non_existent_config.json", "logs");
    EXPECT_FALSE(success) << "Should return false for missing config file";

    // 2. 验证 Fallback Logger 是否可用
    // 即使初始化失败，GetLogger 也应该返回一个能用的对象 (通常是输出到控制台的 Fallback Logger)
    // 而绝对不能返回 nullptr，否则业务代码会崩溃
    auto logger = log_mgr->GetLogger("Test.Fallback");
    ASSERT_NE(logger, nullptr) << "Should return a fallback logger even if init failed";

    // 尝试打印日志，确保不崩溃 (Smoke Test)
    Z3Y_LOG_INFO(logger, "This is a fallback log message");
}

/**
 * @test 验证初始化：正常配置文件加载
 * @brief 使用自动生成的配置文件进行初始化，预期成功。
 */
TEST_F(SpdlogPluginTest, InitSuccessWithValidConfig) {
    auto log_mgr = z3y::GetDefaultService<ILogManagerService>();

    // 1. [修复] 自动生成配置文件，不再依赖外部拷贝
    std::string config_file = GenerateTestConfig();
    std::filesystem::path config_path = PathToUtf8(bin_dir_ / config_file);
    std::filesystem::path log_root = PathToUtf8(bin_dir_ / "test_logs");

    // 2. 初始化
    // 预期：返回 true
    bool success = log_mgr->InitializeService(config_path.string(), log_root.string());
    ASSERT_TRUE(success) << "Failed to init with config at: " << config_path;

    // 3. 获取 Logger
    // 由于配置文件中 default_rule 指向 console 和 file，此 Logger 应该能正常工作
    auto logger = log_mgr->GetLogger("System.Test");
    ASSERT_NE(logger, nullptr);

    // 4. 验证宏调用
    // 打印一条日志，人工检查或通过文件检查内容 (此处仅做不崩溃检查)
    Z3Y_LOG_INFO(logger, "Integration test log: {}", 12345);
}

/**
 * @test 验证核心功能：动态日志等级调整 (SetLevel)
 * @brief 验证 SetLevel 能否动态修改现有和未来的 Logger 等级。
 * @note 此测试曾因配置文件缺失导致 InitializeService 失败，从而导致 SetLevel 无效。
 * 现在有了 GenerateTestConfig，此问题已解决。
 */
TEST_F(SpdlogPluginTest, DynamicSetLevel_Persistence) {
    auto log_mgr = z3y::GetDefaultService<ILogManagerService>();

    // 1. [修复] 先初始化系统 (必须步骤)
    // 如果不初始化，SetLevel 内部会直接 return，导致测试失败
    std::string config_file = GenerateTestConfig();
    ASSERT_TRUE(log_mgr->InitializeService(PathToUtf8((bin_dir_ / config_file).string()), PathToUtf8((bin_dir_ / "logs").string())));

    // 2. 场景准备：获取一个 Logger "Network.Tcp"
    // 根据 GenerateTestConfig 中的配置，默认级别可能是 Trace (取决于 sink 配置)
    // 为了测试准确性，我们先将全局级别强制设为 Info，屏蔽 Trace
    log_mgr->SetLevel("", LogLevel::Info);

    auto logger_net = log_mgr->GetLogger("Network.Tcp");
    // 验证：Info 级别下，Trace 应该被禁用
    EXPECT_FALSE(logger_net->IsEnabled(LogLevel::Trace));

    // 3. [测试点 A] 动态调整 "Network" 前缀为 Trace
    log_mgr->SetLevel("Network", LogLevel::Trace);

    // 验证：现有的 logger_net (Network.Tcp) 应该立即生效
    EXPECT_TRUE(logger_net->IsEnabled(LogLevel::Trace)) << "Existing logger should be updated immediately";

    // 4. [测试点 B] 创建一个新的 Logger "Network.Http"
    // 验证：新创建的 Logger 应该自动继承刚才设置的 "Network" 前缀规则 (持久化验证)
    auto logger_new = log_mgr->GetLogger("Network.Http");
    EXPECT_TRUE(logger_new->IsEnabled(LogLevel::Trace)) << "New logger should inherit dynamic rule";

    // 5. [测试点 C] 验证隔离性
    // "Business" 模块不匹配 "Network" 前缀，应该保持默认 (Info)，即 Trace 不可用
    auto logger_biz = log_mgr->GetLogger("Business.Order");
    EXPECT_FALSE(logger_biz->IsEnabled(LogLevel::Trace)) << "Other modules should not be affected";
}

/**
 * @test 验证强制刷盘 (Smoke Test)
 * @brief 确保 Flush 接口调用安全，不会崩溃。
 */
TEST_F(SpdlogPluginTest, FlushDoesNotCrash) {
    auto log_mgr = z3y::GetDefaultService<ILogManagerService>();

    // 场景 1: 未初始化时调用 Flush (应安全返回)
    log_mgr->Flush();

    // 场景 2: 初始化后调用 Flush
    std::string config_file = GenerateTestConfig();
    log_mgr->InitializeService((bin_dir_ / config_file).string(), "logs");

    log_mgr->Flush(); // 应该将缓冲区内容写入磁盘
}