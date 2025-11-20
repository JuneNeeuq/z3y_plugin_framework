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
 * @file main.cpp
 * @brief 生产级日志插件全功能测试与压测工具
 * @details
 * [测试覆盖]
 * 1. 基础功能: 路径解析、配置加载、文件创建。
 * 2. 高级路由: 验证不同模块 (System, Business, Algo) 的日志是否按配置分流到了不同文件。
 * 3. 动态运维: 验证 SetLevel 是否能精确控制指定命名空间的日志等级。
 * 4. 性能压测: 模拟多业务线程并发写入，计算 QPS。
 */

#include <atomic>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <thread>
#include <vector>
#include <iomanip>

 // 引入框架和日志接口
#include "framework/z3y_framework.h"
#include "interfaces_core/i_log_service.h"
#include "interfaces_core/z3y_log_macros.h"

#ifdef _WIN32
#include <Windows.h>
#endif

using z3y::interfaces::core::ILogger;
using z3y::interfaces::core::ILogManagerService;
using z3y::interfaces::core::LogLevel;
using z3y::PluginPtr;

// --- 压测参数配置 ---
const int kThreadCount = 8;           // 总并发线程数
const int kLogsPerThread = 50000;     // 每个线程写入条数
// 模拟三种不同的业务模块
const std::string kLogNameSys = "System.Network";    // 应流向 system.log
const std::string kLogNameBiz = "Business.Order";    // 应流向 business.log
const std::string kLogNameAlgo = "Algorithm.Vision";  // 应流向 console (默认)

// --- 路径辅助函数 (Windows UTF-8 兼容) ---
std::filesystem::path GetExePath() {
#ifdef _WIN32
    wchar_t buffer[MAX_PATH];
    if (GetModuleFileNameW(NULL, buffer, MAX_PATH) > 0) {
        return std::filesystem::path(buffer);
    }
    return std::filesystem::current_path();
#else
    char buffer[PATH_MAX];
    ssize_t count = readlink("/proc/self/exe", buffer, PATH_MAX);
    if (count > 0) return std::filesystem::path(std::string(buffer, count));
    return std::filesystem::current_path();
#endif
}

std::string PathToUtf8(const std::filesystem::path& path) {
#ifdef _WIN32
    std::wstring wstr = path.wstring();
    if (wstr.empty()) return "";
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
    std::string strTo(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &strTo[0], size_needed, NULL, NULL);
    return strTo;
#else
    return path.string();
#endif
}
// ----------------------------------------------

void PrintSeparator(const std::string& title) {
    std::cout << "\n=============================================================\n"
        << " " << title << "\n"
        << "=============================================================" << std::endl;
}

/**
 * @brief [详细功能测试] 覆盖命名空间、路由、动态调级
 */
void RunDetailedFunctionalTest(PluginPtr<ILogManagerService> log_mgr) {
    PrintSeparator("1. 详细功能验证 (Functional Verification)");

    // 1. 获取不同模块的 Logger
    auto logger_sys = log_mgr->GetLogger(kLogNameSys);
    auto logger_biz = log_mgr->GetLogger(kLogNameBiz);
    auto logger_algo = log_mgr->GetLogger(kLogNameAlgo);

    std::cout << "[Test 1] 验证多命名空间日志路由 (请检查 bench_logs 下是否生成了不同文件)..." << std::endl;
    // 这些日志应该去往不同的 Sink
    Z3Y_LOG_INFO(logger_sys, "System initialized. (Expect: bench_logs/system.log)");
    Z3Y_LOG_INFO(logger_biz, "Order created.      (Expect: bench_logs/business.log)");
    Z3Y_LOG_INFO(logger_algo, "Face detected.      (Expect: Console Only)");

    // 2. 动态调级测试 (精确控制)
    std::cout << "\n[Test 2] 验证精确动态调级 (System.* -> Trace)..." << std::endl;

    // 初始状态检查
    if (logger_sys->IsEnabled(LogLevel::Trace))
        std::cerr << "[Fail] System logger should NOT be Trace by default." << std::endl;
    else
        std::cout << "[Pass] System logger is NOT Trace by default." << std::endl;

    // 调整 System.* 为 Trace
    log_mgr->SetLevel("System", LogLevel::Trace);

    // 验证 System 变了
    if (logger_sys->IsEnabled(LogLevel::Trace)) {
        std::cout << "[Pass] System logger IS now Trace." << std::endl;
        Z3Y_LOG_TRACE(logger_sys, "System Trace Log (Visible).");
    } else {
        std::cerr << "[Fail] System logger SetLevel failed!" << std::endl;
    }

    // 验证 Business 没变 (隔离性)
    if (logger_biz->IsEnabled(LogLevel::Trace)) {
        std::cerr << "[Fail] Business logger was incorrectly affected!" << std::endl;
    } else {
        std::cout << "[Pass] Business logger remains unaffected (Isolation OK)." << std::endl;
    }

    // 恢复默认
    log_mgr->SetLevel("System", LogLevel::Info);
    std::cout << "[Info] Tests completed. Check logs." << std::endl;
}

/**
 * @brief [混合性能压测] 模拟真实场景下的多模块并发写入
 */
void RunMixedPerformanceTest(PluginPtr<ILogManagerService> log_mgr) {
    PrintSeparator("2. 混合负载压测 (Mixed Workload Benchmark)");

    // 提前获取 Logger，避免计入压测耗时
    auto logger_sys = log_mgr->GetLogger(kLogNameSys);
    auto logger_biz = log_mgr->GetLogger(kLogNameBiz);

    std::cout << "配置: " << kThreadCount << " 线程 x " << kLogsPerThread << " 条/线程\n"
        << "场景: System模块(写文件A) + Business模块(写文件B) 混合写入\n"
        << "总量: " << (kThreadCount * kLogsPerThread) << " 条日志" << std::endl;

    std::vector<std::thread> threads;
    std::atomic<int> finished_threads{ 0 };

    Z3Y_LOG_INFO(logger_sys, "Benchmark warming up...");
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    auto start_time = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < kThreadCount; ++i) {
        threads.emplace_back([=, &finished_threads]() {
            // 模拟业务：一半线程写 System，一半写 Business
            auto target_logger = (i % 2 == 0) ? logger_sys : logger_biz;

            for (int j = 0; j < kLogsPerThread; ++j) {
                // 带参数格式化，模拟真实开销
                Z3Y_LOG_INFO(target_logger, "Bench thread {}, seq {}, payload data 1234567890", i, j);
            }
            finished_threads++;
            });
    }

    for (auto& t : threads) {
        if (t.joinable()) t.join();
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = end_time - start_time;

    long long total_logs = kThreadCount * kLogsPerThread;
    long long qps = static_cast<long long>(total_logs / elapsed.count());
    double avg_latency_ns = (elapsed.count() * 1e9) / total_logs;

    std::cout << "\n--- 压测结果 ---" << std::endl;
    std::cout << "耗时:       " << std::fixed << std::setprecision(3) << elapsed.count() << " 秒" << std::endl;
    std::cout << "吞吐量:     " << qps << " logs/sec (QPS)" << std::endl;
    std::cout << "平均提交:   " << avg_latency_ns << " ns/log" << std::endl;
    std::cout << "-------------------------------------------------------------" << std::endl;
}

int main(int argc, char* argv[]) {
#ifdef _WIN32
    system("chcp 65001 > nul");
#endif

    try {
        auto manager = z3y::PluginManager::Create();

        std::filesystem::path exe_path = GetExePath();
        std::filesystem::path exe_dir = exe_path.parent_path();

        std::cout << "[Init] 插件目录: " << PathToUtf8(exe_dir) << std::endl;
        manager->LoadPluginsFromDirectory(exe_dir, true);

        auto log_mgr = z3y::GetDefaultService<ILogManagerService>();

        std::string config_path = PathToUtf8(exe_dir / "benchmark_config.json");
        std::string log_root = PathToUtf8(exe_dir / "bench_logs"); // 日志单独放一个文件夹

        if (!log_mgr->InitializeService(config_path, log_root)) {
            std::cerr << "[Fatal] 初始化失败！请检查 benchmark_config.json" << std::endl;
            return 1;
        }
        std::cout << "[Init] 日志服务就绪。" << std::endl;

        // 1. 全面功能测试
        RunDetailedFunctionalTest(log_mgr);

        std::this_thread::sleep_for(std::chrono::seconds(1));

        // 2. 性能压测
        RunMixedPerformanceTest(log_mgr);

        PrintSeparator("Shutting Down");
        // 清理资源
        log_mgr.reset();
        manager->UnloadAllPlugins();
        std::cout << "[Exit] 测试完成。" << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "[Fatal Exception] " << e.what() << std::endl;
        return 1;
    }

    return 0;
}