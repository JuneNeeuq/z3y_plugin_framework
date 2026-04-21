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

#include <atomic>
#include <thread>
#include <chrono>

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
    std::filesystem::path config_path = z3y::utils::PathToUtf8(bin_dir_ / config_file);
    std::filesystem::path log_root = z3y::utils::PathToUtf8(bin_dir_ / "test_logs");

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
    ASSERT_TRUE(log_mgr->InitializeService(z3y::utils::PathToUtf8((bin_dir_ / config_file).string()), z3y::utils::PathToUtf8((bin_dir_ / "logs").string())));

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

/**
 * @test 验证强制存盘功能的有效性 (Bug Regression Test)
 * @brief 验证调用 Flush() 后，数据是否立即被写入磁盘，而不是停留在内存中。
 * @details
 * 此测试是为了回归验证 "Unregistered Logger" Bug。
 * 如果 Logger 创建后未注册到 spdlog 全局表，log_mgr->Flush() (即 spdlog::apply_all(flush))
 * 将无法找到该 Logger，导致数据不落盘。
 */
TEST_F(SpdlogPluginTest, ExplicitFlushPersistsDataImmediately) {
    auto log_mgr = z3y::GetDefaultService<ILogManagerService>();

    // 1. [关键配置] 构造一个 "懒惰" 的配置
    // - flush_interval_seconds: 60秒 (禁止时间自动刷盘)
    // - flush_on_level: off (禁止根据等级自动刷盘)
    // 这样只有显式调用 Flush() 才能让数据落盘
    std::string filename = "flush_test.log";
    std::filesystem::path log_file_abs_path = bin_dir_ / "logs" / filename;

    // 清理旧文件，确保测试环境纯净
    if (std::filesystem::exists(log_file_abs_path)) {
        std::filesystem::remove(log_file_abs_path);
    }

    std::string config_content = R"({
        "global_settings": {
            "format_pattern": "%v",
            "async_queue_size": 4096,
            "flush_interval_seconds": 60, 
            "flush_on_level": "off"
        },
        "sinks": {
            "test_file": {
                "type": "rotating_file_sink",
                "base_name": "flush_test.log", 
                "max_size": 1048576,
                "max_files": 1,
                "level": "info"
            }
        },
        "default_rule": {
            "sinks": ["test_file"]
        }
    })";

    std::filesystem::path config_path = bin_dir_ / "flush_config.json";
    std::ofstream out(config_path);
    out << config_content;
    out.close();

    // 2. 初始化服务
    ASSERT_TRUE(log_mgr->InitializeService(
        z3y::utils::PathToUtf8(config_path),
        z3y::utils::PathToUtf8(bin_dir_ / "logs")
    ));

    // 3. 获取 Logger 并写入唯一数据
    auto logger = log_mgr->GetLogger("FlushTester");

    // 生成一个随机标记字符串
    std::string unique_token = "TOKEN_" + std::to_string(std::rand());
    Z3Y_LOG_INFO(logger, "This message must be flushed immediately: {}", unique_token);

    // [验证点 A] 在 Flush 之前，理论上数据还在内存队列或缓冲区中
    // (注意：这取决于操作系统 IO 缓存，不一定总是空，但我们主要验证 Flush 后的结果)

    // 4. [核心动作] 强制刷盘
    // 如果插件有 Bug (未注册 Logger)，这一步将不起作用
    log_mgr->Flush();

    // 5. [验证点 B] 读取文件检查 (引入轮询重试机制)
    // 解释：因为 spdlog 依然持有文件写入句柄，Windows NTFS
    // 的文件大小元数据可能不会瞬间更新。 此时 ifstream 可能会读到 0
    // 字节。我们需要给操作系统几十毫秒的时间同步底层数据。
    bool found = false;
    std::string content;

    // 最多重试 20 次，每次间隔 50 毫秒（总计最多等待 1 秒）
    for (int i = 0; i < 20; ++i) {
      std::ifstream f(log_file_abs_path);
      if (f.is_open()) {
        // 一次性读取整个文件内容
        content.assign((std::istreambuf_iterator<char>(f)),
                       std::istreambuf_iterator<char>());

        // 如果找到了我们刚才写入的 Token，说明系统刷盘和元数据更新已完成
        if (content.find(unique_token) != std::string::npos) {
          found = true;
          break;
        }
      }
      // 如果没找到（文件可能读到空），给操作系统一点时间同步，再试
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    // 断言：最终必须能找到该数据
    EXPECT_TRUE(found)
        << "强制 Flush() 失败！即使等待后，日志文件内容仍未包含最新数据。\n"
        << "最后一次读取的长度: " << content.length() << " bytes\n"
        << "最后一次读取的内容: " << content;
}

// =======================================================================
// [新增功能测试] UI 观察者机制 (Observer)
// =======================================================================

/**
 * @test 验证 UI 观察者能否正确接收到完整且合法的日志记录
 */
TEST_F(SpdlogPluginTest, UIObserver_ReceivesCompleteLogRecord) {
  auto log_mgr = z3y::GetDefaultService<ILogManagerService>();
  std::string config_file = GenerateTestConfig();
  ASSERT_TRUE(log_mgr->InitializeService(
      z3y::utils::PathToUtf8((bin_dir_ / config_file).string()),
      z3y::utils::PathToUtf8((bin_dir_ / "logs").string())));

  auto logger = log_mgr->GetLogger("Algorithm.Matcher");

  // 用于将异步回调中的数据提取到主线程进行断言
  struct ExtractedData {
    uint32_t struct_size = 0;
    std::string logger_name;
    std::string message;
    std::string file_name;
    std::string func_name;
    LogLevel level;
    uint32_t thread_id = 0;
    int32_t line_number = 0;
  };
  ExtractedData captured;
  std::atomic<int> call_count{0};

  // 1. 注册观察者
  log_mgr->AddLogObserver("TestUI", [&](const LogRecord& record) {
    // [极其重要的演示] UI 拿到指针后，必须立刻深拷贝为 std::string!
    captured.struct_size = record.struct_size;
    captured.logger_name = record.logger_name;
    captured.message = record.message;
    captured.file_name = record.file_name;
    captured.func_name = record.func_name;
    captured.level = record.level;
    captured.thread_id = record.thread_id;
    captured.line_number = record.line_number;
    call_count++;
  });

  // 2. 触发日志
  int test_value = 999;
  Z3Y_LOG_ERROR(logger, "Template matching failed, score: {}", test_value);

  // 3. [异步屏障] 强制等待后台 spdlog 线程处理完回调！
  // 因为是 async_logger，如果我们不 Flush，测试用例可能直接跑完导致验证失败。
  log_mgr->Flush();

  // [修复核心] 轮询等待后台线程完成回调，最多等待 1 秒 (100 * 10ms)
  int wait_loops = 0;
  while (call_count.load() == 0 && wait_loops < 100) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    wait_loops++;
  }

  // 4. 断言验证
  EXPECT_EQ(call_count.load(), 1)
      << "Observer callback should be triggered exactly once";

  // 验证 ABI 防御机制 (struct_size 必须严格等于 sizeof(LogRecord))
  EXPECT_EQ(captured.struct_size, sizeof(LogRecord));

  // 验证内容透传完整性
  EXPECT_EQ(captured.level, LogLevel::Error);
  EXPECT_EQ(captured.logger_name, "Algorithm.Matcher");
  EXPECT_EQ(captured.message, "Template matching failed, score: 999");

  // 验证宏是否正确捕获了代码位置 (确保没有变成空指针或乱码)
  EXPECT_NE(captured.file_name.find("test_spdlog_plugin.cpp"),
            std::string::npos)
      << "Filename must be captured properly";
  EXPECT_NE(captured.func_name, "Unknown") << "Function name must be captured";
  EXPECT_GT(captured.line_number, 0);
  EXPECT_GT(captured.thread_id, 0);
}

/**
 * @test 验证观察者注销机制
 * @brief 移除后，不应再收到任何日志回调
 */
TEST_F(SpdlogPluginTest, UIObserver_RemoveStopsCallback) {
  auto log_mgr = z3y::GetDefaultService<ILogManagerService>();
  std::string config_file = GenerateTestConfig();
  log_mgr->InitializeService(
      z3y::utils::PathToUtf8((bin_dir_ / config_file).string()),
      z3y::utils::PathToUtf8((bin_dir_ / "logs").string()));

  auto logger = log_mgr->GetLogger("Hardware.PLC");
  std::atomic<int> call_count{0};

  // 1. 注册并测试一次
  log_mgr->AddLogObserver("TempObserver",
                          [&](const LogRecord&) { call_count++; });

  Z3Y_LOG_INFO(logger, "First message");
  log_mgr->Flush();

  // 等待第一次回调完成
  int wait_loops = 0;
  while (call_count.load() == 0 && wait_loops < 100) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    wait_loops++;
  }

  EXPECT_EQ(call_count.load(), 1);

  // 2. 核心操作：注销观察者
  log_mgr->RemoveLogObserver("TempObserver");

  // 3. 再次记录日志，验证回调不再触发
  Z3Y_LOG_INFO(logger, "Second message - should be ignored by observer");
  log_mgr->Flush();

  // 给后台 50 毫秒的充足时间，确保即便处理了队列，回调也不会增加
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  EXPECT_EQ(call_count.load(), 1)
      << "Callback count should not increase after removal";
}