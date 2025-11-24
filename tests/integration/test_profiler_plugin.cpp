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
  * @file test_profiler_plugin.cpp
  * @brief 性能分析插件 (Profiler) 的全功能集成测试套件。
  * @details
  * 本测试套件旨在从使用者的角度验证 Profiler 插件的核心功能、稳定性和配置灵活性。
  * * [本次更新重点]
  * 1. **中文路径规避**: 在 `GlobalDisableSwitch` 中强制使用相对路径，解决 Windows 中文路径乱码导致的 IO 失败。
  * 2. **稳定性**: 增加了文件创建和写入的轮询等待机制。
  */

#undef Z3Y_PROFILE_MODULE_NAME
#define Z3Y_PROFILE_MODULE_NAME "Test.Integration"

#include "common/plugin_test_base.h"
#include "interfaces_core/i_config_service.h"
#include "interfaces_core/i_log_service.h"
#include "interfaces_profiler/i_profiler_service.h"
#include "interfaces_profiler/profiler_macros.h"

#include <fstream>
#include <thread>
#include <nlohmann/json.hpp>

#ifdef _WIN32
#include <Windows.h>
#endif

using namespace z3y;
using namespace z3y::interfaces::core;
using namespace z3y::interfaces::profiler;

/**
 * @class ProfilerPluginTest
 * @brief Profiler 集成测试夹具 (Test Fixture)。
 */
class ProfilerPluginTest : public PluginTestBase {
protected:
    void SetUp() override {
        PluginTestBase::SetUp();
        ASSERT_TRUE(LoadPlugin("plugin_config_manager"));
        ASSERT_TRUE(LoadPlugin("plugin_spdlog_logger"));
        ASSERT_TRUE(LoadPlugin("plugin_profiler"));

        test_root_ = bin_dir_ / std::filesystem::u8path(u8"TestEnv_Profiler");
        log_root_ = test_root_ / "logs";
        config_root_ = test_root_ / "config";

        if (std::filesystem::exists(test_root_)) std::filesystem::remove_all(test_root_);
        std::filesystem::create_directories(log_root_);
        std::filesystem::create_directories(config_root_);

        auto config_svc = z3y::GetDefaultService<IConfigManagerService>();
        ASSERT_TRUE(config_svc->InitializeService(PathToUtf8(config_root_)));

        PrepareLoggerConfig();
        auto log_svc = z3y::GetDefaultService<ILogManagerService>();
        ASSERT_TRUE(log_svc->InitializeService(PathToUtf8(config_root_ / "logger_config.json"), PathToUtf8(log_root_)));
    }

    void TearDown() override {
        z3y::profiler::ResetProfilerCache();
        if (auto [log_svc, err] = z3y::TryGetDefaultService<ILogManagerService>(); err == InstanceError::kSuccess) {
            log_svc->Flush();
        }
        PluginTestBase::TearDown();
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

    void PrepareLoggerConfig() {
        nlohmann::json j;
        j["global_settings"] = { {"flush_interval_seconds", 0}, {"flush_on_level", "trace"} };
        j["sinks"]["profiler_file"] = { {"type", "rotating_file_sink"}, {"base_name", "profiler_test.log"}, {"max_size", 1048576}, {"max_files", 1}, {"level", "trace"} };
        j["sinks"]["biz_file"] = { {"type", "rotating_file_sink"}, {"base_name", "business.log"}, {"level", "trace"} };
        j["rules"] = {
            { {"matcher", "System.Profiler"}, {"sinks", {"profiler_file"}} },
            { {"matcher", "Test"},            {"sinks", {"profiler_file"}} },
            { {"matcher", "MyBizLogger"},     {"sinks", {"biz_file"}} }
        };
        WriteFile(config_root_ / "logger_config.json", j.dump(4));
    }

    void UpdateProfilerConfig(double threshold_ms, bool enable_tracing = false, size_t max_queue = 2048) {
        nlohmann::json j;
        j["settings"] = {
            {"global_enable", true},
            {"alert_threshold_ms", threshold_ms},
            {"enable_console_log", false},
            {"enable_tracing", enable_tracing},
            {"trace_file", PathToUtf8(log_root_ / "trace.json")},
            {"max_trace_file_mb", 500},
            {"max_queue_size", max_queue},
            {"rules", {{ {"matcher", "Test.HighSpeed"}, {"threshold_ms", 1.0} }}}
        };
        WriteFile(config_root_ / "profiler_config.json", j.dump(4));

        if (auto [config_svc, err] = z3y::TryGetDefaultService<IConfigManagerService>(); err == InstanceError::kSuccess) {
            config_svc->Reload("profiler_config");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    void WriteFile(const std::filesystem::path& path, const std::string& content) {
        std::ofstream f(path, std::ios::binary);
        f << content;
    }

    std::string ReadLogContent(const std::string& filename = "profiler_test.log") {
        if (auto [log_svc, err] = z3y::TryGetDefaultService<ILogManagerService>(); err == InstanceError::kSuccess) {
            log_svc->Flush();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        std::ifstream f(log_root_ / filename, std::ios::binary);
        if (!f.is_open()) return "";
        return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    }

    nlohmann::json ReadTraceJson() {
        std::ifstream f(log_root_ / "trace.json");
        if (!f.is_open()) return nlohmann::json();
        try { return nlohmann::json::parse(f); } catch (...) { return nlohmann::json(); }
    }

    std::filesystem::path test_root_, log_root_, config_root_;
};

/**
 * @test 基础阈值过滤测试。
 */
TEST_F(ProfilerPluginTest, BasicThresholdFiltering) {
    UpdateProfilerConfig(20.0);
    auto profiler = z3y::GetDefaultService<IProfilerService>();
    { Z3Y_PROFILE_SCOPE("FastStep"); std::this_thread::sleep_for(std::chrono::milliseconds(5)); }
    { Z3Y_PROFILE_SCOPE("SlowStep"); std::this_thread::sleep_for(std::chrono::milliseconds(50)); }
    std::string log = ReadLogContent();
    EXPECT_EQ(log.find("FastStep"), std::string::npos);
    EXPECT_NE(log.find("SlowStep"), std::string::npos);
}

/**
 * @test 规则匹配策略测试。
 */
TEST_F(ProfilerPluginTest, RuleMatchingStrategy) {
    UpdateProfilerConfig(50.0);
    auto profiler = z3y::GetDefaultService<IProfilerService>();
    { Z3Y_PROFILE_SCOPE("NormalStep"); std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
    SourceLocation loc = { __FILE__, __FUNCTION__, __LINE__ };
    profiler->RecordTime(nullptr, "Test.HighSpeed", "RuleStep", nullptr, PayloadType::Text, loc, 5.0);
    std::string log = ReadLogContent();
    EXPECT_EQ(log.find("NormalStep"), std::string::npos);
    EXPECT_NE(log.find("RuleStep"), std::string::npos);
}

/**
 * @test 上下文数据 (Payload) 测试。
 */
TEST_F(ProfilerPluginTest, ContextPayloadSupport) {
    UpdateProfilerConfig(0.0);
    auto profiler = z3y::GetDefaultService<IProfilerService>();
    { Z3Y_PROFILE_SCOPE_MSG("ProcessMsg", "ID:{}, Mode:{}", 1001, "Fast"); }
    std::string log = ReadLogContent();
    EXPECT_NE(log.find("Data: ID:1001, Mode:Fast"), std::string::npos);
}

/**
 * @test 日志路由测试。
 */
TEST_F(ProfilerPluginTest, ExplicitLoggerRouting) {
    UpdateProfilerConfig(0.0);
    auto log_mgr = z3y::GetDefaultService<ILogManagerService>();
    auto biz_logger = log_mgr->GetLogger("MyBizLogger");
    { Z3Y_PROFILE_SCOPE_LOG(biz_logger, "BizOnlyStep"); }
    EXPECT_EQ(ReadLogContent("profiler_test.log").find("BizOnlyStep"), std::string::npos);
    EXPECT_NE(ReadLogContent("business.log").find("BizOnlyStep"), std::string::npos);
}

/**
 * @test 线性 Profiler 工作流测试。
 */
TEST_F(ProfilerPluginTest, LinearProfilerWorkflow) {
    UpdateProfilerConfig(0.0);
    auto profiler = z3y::GetDefaultService<IProfilerService>();
    {
        Z3Y_PROFILE_LINEAR_BEGIN("InitPhase");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        Z3Y_PROFILE_NEXT_MSG("ComputePhase", "Iter:{}", 1);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    std::string log = ReadLogContent();
    EXPECT_NE(log.find("'InitPhase'"), std::string::npos);
    EXPECT_NE(log.find("'ComputePhase'"), std::string::npos);
    EXPECT_NE(log.find("Data: Iter:1"), std::string::npos);
}

/**
 * @test 宏接口与日志路由测试 (MacroAPI_LoggerVariants)。
 */
TEST_F(ProfilerPluginTest, MacroAPI_LoggerVariants) {
    UpdateProfilerConfig(0.0);
    auto log_mgr = z3y::GetDefaultService<ILogManagerService>();
    auto biz_logger = log_mgr->GetLogger("MyBizLogger");
    { Z3Y_PROFILE_SCOPE_MSG_LOG(biz_logger, "BizMsgScope", "User:{}", "Admin"); }
    {
        Z3Y_PROFILE_LINEAR_BEGIN_LOG(biz_logger, "BizLinearStart");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        Z3Y_PROFILE_NEXT("BizLinearEnd");
    }
    auto test_func_macro = [&]() { Z3Y_PROFILE_FUNCTION_LOG(biz_logger); std::this_thread::sleep_for(std::chrono::milliseconds(1)); };
    test_func_macro();

    std::string profiler_log = ReadLogContent("profiler_test.log");
    std::string biz_log = ReadLogContent("business.log");
    EXPECT_EQ(profiler_log.find("BizMsgScope"), std::string::npos);
    EXPECT_EQ(profiler_log.find("BizLinearStart"), std::string::npos);
    EXPECT_NE(biz_log.find("BizMsgScope"), std::string::npos);
    EXPECT_NE(biz_log.find("Data: User:Admin"), std::string::npos);
    EXPECT_NE(biz_log.find("BizLinearStart"), std::string::npos);
    EXPECT_NE(biz_log.find("BizLinearEnd"), std::string::npos);
    EXPECT_NE(biz_log.find("operator"), std::string::npos);
}

/**
 * @test 高级 Tracing 功能测试。
 */
TEST_F(ProfilerPluginTest, AdvancedTracingFeatures) {
    UpdateProfilerConfig(0.0, true);
    auto profiler = z3y::GetDefaultService<IProfilerService>();
    uint64_t flow_id = 0x1234;
    {
        Z3Y_PROFILE_SCOPE_ARGS("StructArgs", "\"w\":{}, \"h\":{}", 100, 200);
        Z3Y_PROFILE_MARK_FRAME("CameraVsync");
        Z3Y_PROFILE_FLOW_START("DataFlow", flow_id);
    }
    Z3Y_PROFILE_FLUSH();
    profiler.reset();
    z3y::profiler::ResetProfilerCache();
    manager_->UnloadAllPlugins();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    nlohmann::json j = ReadTraceJson();
    ASSERT_TRUE(j.contains("traceEvents"));
    bool found_args = false, found_flow = false;
    for (const auto& evt : j["traceEvents"]) {
        if (!evt.is_object() || !evt.contains("name")) continue;
        if (evt["name"] == "StructArgs" && evt.contains("args") && evt["args"].value("w", 0) == 100) found_args = true;
        if (evt.contains("cat") && evt["cat"] == "flow" && evt.contains("id") && evt["id"] == flow_id) found_flow = true;
    }
    EXPECT_TRUE(found_args);
    EXPECT_TRUE(found_flow);
}

/**
 * @test 队列溢出保护测试。
 */
TEST_F(ProfilerPluginTest, QueueOverflowProtection) {
    UpdateProfilerConfig(0.0, true, 0); // 队列容量设为 0，强制丢弃
    auto profiler = z3y::GetDefaultService<IProfilerService>();

    const int kThreadCount = 4;
    const int kIterations = 100;
    std::vector<std::thread> threads;

    for (int t = 0; t < kThreadCount; ++t) {
        threads.emplace_back([kIterations]() {
            for (int i = 0; i < kIterations; ++i) {
                Z3Y_PROFILE_COUNTER("OverflowGen", i);
            }
            Z3Y_PROFILE_FLUSH();
            });
    }

    for (auto& t : threads) t.join();

    profiler.reset();
    z3y::profiler::ResetProfilerCache();
    manager_->UnloadAllPlugins();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    nlohmann::json j = ReadTraceJson();
    bool found_drop = false;
    if (j.contains("traceEvents")) {
        for (const auto& evt : j["traceEvents"]) {
            if (evt.is_object() && evt.contains("name") && evt["name"] == "DROPPED_CHUNKS") {
                found_drop = true;
                break;
            }
        }
    }
    EXPECT_TRUE(found_drop);
}

/**
 * @test 全局禁用开关测试。
 * @brief 验证 global_enable = false 时，系统实现零开销。
 * @note [稳定性修复] 使用相对路径 + 显式 UTF-8 转换，解决中文路径问题。
 */
TEST_F(ProfilerPluginTest, GlobalDisableSwitch) {
    auto profiler_instance = z3y::GetDefaultService<IProfilerService>();
    ASSERT_NE(profiler_instance, nullptr);

    // 1. 构造相对路径配置字符串 (纯 ASCII，规避中文编码问题)
    // 插件会在当前工作目录 (bin_dir_) 下创建此路径
    std::string relative_trace_file = "TestEnv_Profiler/logs/trace_disabled.json";

    // 测试代码用来检查文件的绝对路径
    std::filesystem::path abs_trace_path = bin_dir_ / "TestEnv_Profiler/logs/trace_disabled.json";

    // 2. 显式开启 Tracing (初始化阶段)
    nlohmann::json j_on;
    j_on["settings"] = {
        {"global_enable", true},
        {"alert_threshold_ms", 0.0},
        {"enable_tracing", true},
        {"trace_file", relative_trace_file} // [关键] 使用相对路径
    };
    WriteFile(config_root_ / "profiler_config.json", j_on.dump(4));

    auto config_svc = z3y::GetDefaultService<IConfigManagerService>();
    config_svc->Reload("profiler_config");

    // 3. 轮询等待文件创建 (确保初始化成功)
    bool file_created = false;
    for (int i = 0; i < 30; ++i) {
        if (std::filesystem::exists(abs_trace_path) && std::filesystem::file_size(abs_trace_path) > 0) {
            file_created = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    ASSERT_TRUE(file_created) << "Trace file creation timed out. Path: " << abs_trace_path.string();

    // 4. 切换配置：global_enable = false
    nlohmann::json j_off = j_on;
    j_off["settings"]["global_enable"] = false;
    WriteFile(config_root_ / "profiler_config.json", j_off.dump(4));
    config_svc->Reload("profiler_config");
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // 5. 执行操作 (预期不记录)
    { Z3Y_PROFILE_SCOPE("ShouldNotBeRecorded"); }
    Z3Y_PROFILE_COUNTER("TestCounter", 123);

    // 6. 关闭 Tracing 以触发 Flush 和文件关闭
    j_off["settings"]["enable_tracing"] = false;
    WriteFile(config_root_ / "profiler_config.json", j_off.dump(4));
    config_svc->Reload("profiler_config");

    // 等待关闭
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // 7. 验证内容 (轮询等待 Footer)
    std::string content;
    bool footer_found = false;
    for (int i = 0; i < 50; ++i) {
        std::ifstream f(abs_trace_path, std::ios::binary | std::ios::ate);
        if (f.is_open()) {
            auto size = f.tellg();
            if (size > 0) {
                f.seekg(0, std::ios::beg);
                content.resize(size);
                f.read(&content[0], size);
                if (content.find("]}") != std::string::npos) {
                    footer_found = true;
                    break;
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    if (!footer_found) {
        FAIL() << "Trace file incomplete (missing footer).\nPath: " << abs_trace_path.string();
    }

    // 解析验证
    try {
        auto trace = nlohmann::json::parse(content);
        if (trace.contains("traceEvents")) {
            for (const auto& evt : trace["traceEvents"]) {
                if (evt.contains("name")) {
                    EXPECT_NE(evt["name"], "ShouldNotBeRecorded");
                    EXPECT_NE(evt["name"], "TestCounter");
                }
            }
        }
    } catch (const std::exception& e) {
        FAIL() << "JSON Parse Error: " << e.what();
    }
}

/**
 * @test 线程元数据测试。
 */
TEST_F(ProfilerPluginTest, ThreadNaming) {
    UpdateProfilerConfig(0.0, true);
    std::thread t([&]() {
        Z3Y_PROFILE_THREAD_NAME("MyRenderThread");
        Z3Y_PROFILE_FLUSH();
        });
    t.join();

    z3y::profiler::ResetProfilerCache();
    manager_->UnloadAllPlugins();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    nlohmann::json j = ReadTraceJson();
    bool found_name = false;
    for (const auto& evt : j["traceEvents"]) {
        if (evt.value("ph", "") == "M" && evt.value("name", "") == "thread_name" &&
            evt.contains("args") && evt["args"].value("name", "") == "MyRenderThread") {
            found_name = true;
            break;
        }
    }
    EXPECT_TRUE(found_name);
}