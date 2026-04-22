/**
 * @file test_profiler_plugin.cpp
 * @brief Profiler 性能分析插件的集成测试与稳定性测试集。
 * * @details
 * 【面向测试与维护人员】
 * 本文件负责进行严格的 GoogleTest 验证，覆盖了如下功能：
 * - 基础作用域分析器 (ScopedTimer)
 * - 父子级联拓扑的建立 (Tree Hierarchy)
 * - 高频数据 Tag 的挂载深拷贝
 * - 完全跨线程的 Async 流式绑定和闭环生成
 * - 超越 SLA 触发器的报表打印检测
 * 任何对此插件核心实现（尤其是自旋锁或内存池）修改后，必须保证所有的用例完全绿灯通过，
 * 如果产生死锁或一直 Pending，极大可能是修改破坏了 LCRS 树的并发控制状态机。
 */

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

#include "common/plugin_test_base.h"
#include "framework/z3y_service_locator.h"
#include "interfaces_core/i_config_service.h"
#include "interfaces_core/i_log_service.h"
#include "interfaces_profiler/profiler_macros.h"
#include "interfaces_profiler/profiler_types.h"

using namespace z3y;

/**
 * @brief 性能插件专用的测试固件，提供安全沙盒式的文件 IO 管理与上下文加载。
 */
class ProfilerPluginTest : public PluginTestBase {
 protected:
  std::string current_log_file_;

  /**
   * @brief 在每个测试开启前建立独立的日志文件路径，清空历史遗留污染。
   */
  void SetUp() override {
    std::string test_name =
        ::testing::UnitTest::GetInstance()->current_test_info()->name();
    current_log_file_ = "app_" + test_name + ".log";

    if (!std::filesystem::exists("logs")) {
      std::filesystem::create_directory("logs");
    }

    std::filesystem::path log_path =
        std::filesystem::path("logs") / current_log_file_;
    if (std::filesystem::exists(log_path)) {
      try {
        std::filesystem::remove(log_path);
      } catch (...) {
      }
    }

    // 清理可能污染测试环境的遗留 config.json
    if (std::filesystem::exists("config.json")) {
      try {
        std::filesystem::remove("config.json");
      } catch (...) {
      }
    }

    std::string config_content = R"({
            "global_settings": {
                "format_pattern": "[%Y-%m-%d %H:%M:%S.%e] [%n] %v",
                "async_queue_size": 1024,
                "flush_interval_seconds": 1,
                "flush_on_level": "trace"
            },
            "sinks": {
                "file_test": {
                    "type": "rotating_file_sink",
                    "base_name": ")" +
                                 current_log_file_ + R"(",
                    "max_size": 1048576,
                    "max_files": 1,
                    "level": "trace"
                }
            },
            "default_rule": { "sinks": [ "file_test" ] }
        })";
    std::ofstream out("test_logger_config.json");
    out << config_content;
    out.close();

    PluginTestBase::SetUp();

    ASSERT_TRUE(LoadPlugin("plugin_spdlog_logger"));
    ASSERT_TRUE(LoadPlugin("plugin_config_manager"));
    ASSERT_TRUE(LoadPlugin("plugin_profiler"));

    auto [log_svc, err] =
        TryGetDefaultService<z3y::interfaces::core::ILogManagerService>();
    ASSERT_EQ(err, z3y::InstanceError::kSuccess);
    ASSERT_TRUE(log_svc->InitializeService("test_logger_config.json", "logs"));

    auto [profiler_svc, profiler_err] =
        TryGetDefaultService<z3y::interfaces::profiler::IProfilerService>();
    ASSERT_EQ(profiler_err, z3y::InstanceError::kSuccess);

    // 强行将配置设置为 true，打破本地配置文件的“永久封印”
    if (auto [cfg_svc, cfg_err] =
            TryGetDefaultService<z3y::interfaces::core::IConfigService>();
        cfg_err == z3y::InstanceError::kSuccess) {
      cfg_svc->SetValue("System.Profiler.Enable", true);
      // 让事件总线飞一会儿，确保 Profiler 同步开启
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
  }

  void TearDown() override {
    z3y::interfaces::profiler::ResetProfilerCache();
    // 【关键修复】：在底层框架强杀 DLL 之前，主动通知 Profiler
    // 断开与其他组件的连接。 防止因为 PluginManager 乱序卸载导致调用到已卸载
    // DLL 的虚函数。
    if (auto [svc, err] =
            TryGetDefaultService<z3y::interfaces::profiler::IProfilerService>();
        err == z3y::InstanceError::kSuccess) {
      svc->Shutdown();
    }

    PluginTestBase::TearDown();
    try {
      std::filesystem::remove("test_logger_config.json");
    } catch (...) {
    }
  }

  /**
   * @brief 测试完成后，用于验证断言读取日志结果。
   */
  std::string ReadAllLogs() {
    if (auto [log_svc, err] =
            TryGetDefaultService<z3y::interfaces::core::ILogManagerService>();
        err == z3y::InstanceError::kSuccess) {
      log_svc->Flush();
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    std::string all_content;
    std::filesystem::path log_path =
        std::filesystem::path("logs") / current_log_file_;

    // 智能轮询 IO，最多等待 2 秒
    for (int i = 0; i < 40; ++i) {
      all_content.clear();
      if (std::filesystem::exists(log_path)) {
        std::ifstream file(log_path);
        if (file.is_open()) {
          std::string line;
          while (std::getline(file, line)) all_content += line + "\n";
        }
      }
      // 如果读到了 Profiler 特征码，说明日志已经落盘，提前退出轮询
      if (all_content.find("[Z3Y Profiler]") != std::string::npos) {
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return all_content;
  }

  /**
   * @brief 模拟一个耗时处理流水线。
   */
  void SimulateAlgorithmWork() {
    Z3Y_PROFILE();
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    {
      Z3Y_PROFILE_NAMED("Algorithm_SubStep_A");
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
  }
};

/**
 * @brief 验证基础周期内多级拓扑树能否被正确的构建并打印 SLA 警告报告。
 */
TEST_F(ProfilerPluginTest, Verify_SlaTimeout_And_Tags) {
  {
    Z3Y_PROFILE_ROOT("Inspect_Workflow", 1, 0.0);
    Z3Y_PROFILE_TAG("BatchNumber", "NV-2026");
    {
      Z3Y_PROFILE_NAMED("DeepLearning_Infer");
      Z3Y_PROFILE_TAG("Model", "YOLO_v26");
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
  }
  std::string logs = ReadAllLogs();
  EXPECT_TRUE(logs.find("Inspect_Workflow") != std::string::npos);
  EXPECT_TRUE(logs.find("BatchNumber: NV-2026") != std::string::npos);
  EXPECT_TRUE(logs.find("DeepLearning_Infer") != std::string::npos);
  EXPECT_TRUE(logs.find("Model: YOLO_v26") != std::string::npos);
  EXPECT_TRUE(logs.find("%") != std::string::npos);
}

/**
 * @brief 验证周期性 Tick 的汇报逻辑。
 */
TEST_F(ProfilerPluginTest, Verify_Periodic_Reporting) {
  for (int i = 0; i < 3; ++i) {
    Z3Y_PROFILE_ROOT("Periodic_Task", 3, 9999.0);
    Z3Y_PROFILE_NAMED("Fast_Process");
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  std::string logs = ReadAllLogs();
  EXPECT_TRUE(logs.find("Periodic") != std::string::npos);
  EXPECT_TRUE(logs.find("Periodic_Task") != std::string::npos);
  EXPECT_TRUE(logs.find("3 ") != std::string::npos);
}

/**
 * @brief 验证 Z3Y_PROFILE_LINEAR 顺序执行流分析的准确性。
 */
TEST_F(ProfilerPluginTest, Verify_Linear_Profile) {
  {
    Z3Y_PROFILE_ROOT("Linear_Root", 1, 0.0);
    Z3Y_PROFILE_LINEAR("Total_Spaghetti_Code");
    Z3Y_PROFILE_NEXT("Step1_Filter");
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    Z3Y_PROFILE_NEXT("Step2_Match");
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  std::string logs = ReadAllLogs();
  EXPECT_TRUE(logs.find("Total_Spaghetti_Code") != std::string::npos);
  EXPECT_TRUE(logs.find("Step1_Filter") != std::string::npos);
  EXPECT_TRUE(logs.find("Step2_Match") != std::string::npos);
}

/**
 * @brief 验证自定义数值统计和事件触发。
 */
TEST_F(ProfilerPluginTest, Verify_Metrics_Value_And_Event) {
  {
    Z3Y_PROFILE_ROOT("Metrics_Root", 1, 0.0);

    double vals[] = {10.0, 20.0};
    for (int i = 0; i < 2; ++i) {
      Z3Y_PROFILE_VALUE("Blob_Count", vals[i]);
      Z3Y_PROFILE_EVENT("Camera_Triggered");
    }
  }
  std::string logs = ReadAllLogs();

  EXPECT_TRUE(logs.find("[Metrics]") != std::string::npos);
  EXPECT_TRUE(logs.find("Blob_Count") != std::string::npos);
  EXPECT_TRUE(logs.find("(Value)") != std::string::npos);
  EXPECT_TRUE(logs.find("Max: 20.00") != std::string::npos);

  EXPECT_TRUE(logs.find("Camera_Triggered") != std::string::npos);
  EXPECT_TRUE(logs.find("(Event)") != std::string::npos);
  EXPECT_TRUE(logs.find("Total Occurrences: 2") != std::string::npos);
}

/**
 * @brief 验证复杂异步多线程分析场景的数据追踪防线。
 */
TEST_F(ProfilerPluginTest, Verify_Async_Pipeline) {
  uint64_t mock_frame_id = 8848;
  std::thread t1([mock_frame_id]() {
    Z3Y_PROFILE_ASYNC_BEGIN("Async_Workflow", mock_frame_id, 1, 0.0);
  });
  t1.join();

  std::thread t2([mock_frame_id]() {
    Z3Y_PROFILE_ASYNC_ATTACH(mock_frame_id);
    {
      Z3Y_PROFILE_NAMED("Cross_Thread_Worker");
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    Z3Y_PROFILE_ASYNC_COMMIT(mock_frame_id);
  });
  t2.join();

  std::string logs = ReadAllLogs();
  EXPECT_TRUE(logs.find("Async_Workflow") != std::string::npos) << "Logs:\n"
                                                                << logs;
  EXPECT_TRUE(logs.find("Cross_Thread_Worker") != std::string::npos);
}

/**
 * @brief 验证 Profiler 配置选项能否实时动态介入（热阻断）。
 */
TEST_F(ProfilerPluginTest, Verify_Config_Dynamic_Disable) {
  auto [cfg_svc, err] =
      TryGetDefaultService<z3y::interfaces::core::IConfigService>();
  ASSERT_EQ(err, z3y::InstanceError::kSuccess);

  // 将控制变量动态设为 false，测试引擎是否能够立即屏蔽探针注入
  cfg_svc->SetValue("System.Profiler.Enable", false);

  int retry = 50;
  bool is_enabled = true;
  while (is_enabled && retry-- > 0) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    if (auto [svc, err2] =
            TryGetDefaultService<z3y::interfaces::profiler::IProfilerService>();
        err2 == z3y::InstanceError::kSuccess) {
      is_enabled = svc->IsEnabled();
    }
  }

  {
    Z3Y_PROFILE_ROOT("Disabled_Task", 1, 0.0);
    Z3Y_PROFILE_NAMED("Should_Not_Log");
  }

  std::string logs = ReadAllLogs();

  EXPECT_TRUE(logs.find("Disabled_Task") == std::string::npos);
  EXPECT_TRUE(logs.find("Should_Not_Log") == std::string::npos);
}

/**
 * @brief 极限并发防丢失测试：验证无锁双精度浮点数 CAS 累加的绝对安全性
 * @note  不依赖 ProfilerPluginTest 的文件 IO，单纯测试内存数据结构的坚固性
 */
TEST(ProfilerConcurrencyTest, LockFreeDoubleAccumulation) {
  z3y::interfaces::profiler::AggregatorNode node;
  node.Reset();

  const int num_threads = 20;
  const int iterations = 10000;
  const double val_to_add = 3.14;

  std::vector<std::thread> workers;

  for (int t = 0; t < num_threads; ++t) {
    workers.emplace_back([&node, iterations, val_to_add]() {
      for (int i = 0; i < iterations; ++i) {
        // 并发写入调用次数
        node.call_count.fetch_add(1, std::memory_order_relaxed);
        // 并发累加 Double
        z3y::interfaces::profiler::AtomicAddDouble(node.sum_value_bits,
                                                   val_to_add);
        // 并发抢占最大值
        z3y::interfaces::profiler::AtomicUpdateMax(node.max_value_bits,
                                                   val_to_add * (i % 100));
      }
    });
  }

  for (auto& w : workers) {
    w.join();
  }

  // 核心断言：验证在极其恶劣的并发抢占下，无锁 CAS 没有吞掉任何一次数据
  EXPECT_EQ(node.call_count.load(),
            static_cast<uint64_t>(num_threads * iterations));

  // 浮点数比较允许微小的精度误差
  double expected_sum = num_threads * iterations * val_to_add;
  EXPECT_NEAR(node.GetSumValue(), expected_sum, 1e-4);

  // 验证最大值没有出现“大数吃小数”或越界
  EXPECT_NEAR(node.GetMaxValue(), val_to_add * 99, 1e-4);
}

/**
 * @brief 满载熔断降级测试：验证 1024 个槽位被僵尸耗尽时，系统触发 Circuit
 * Breaker 且不崩溃
 */
TEST_F(ProfilerPluginTest, Verify_CircuitBreaker_On_Exhaustion) {
  // 故意占据全部 1024 个物理槽位，且不调用 COMMIT (模拟全部卡死)
  for (uint64_t i = 1; i <= 1024; ++i) {
    Z3Y_PROFILE_ASYNC_BEGIN("Zombie_Slot", i, 1, 0.0);
  }

  // 第 1025 个任务，此时没有空闲槽位，应该触发熔断静默返回
  Z3Y_PROFILE_ASYNC_BEGIN("Overflow_Task", 1025, 1, 0.0);

  // Worker 尝试 Attach
  // 并写入数据，验证宏底层的空安全防御是否生效（若不生效将抛出空指针异常崩溃）
  Z3Y_PROFILE_ASYNC_ATTACH(1025);
  {
    Z3Y_PROFILE_NAMED("Should_Be_Ignored_Safely");
    Z3Y_PROFILE_VALUE("LostMetric", 100.0);
  }
  Z3Y_PROFILE_ASYNC_COMMIT(1025);

  std::string logs = ReadAllLogs();

  // 验证输出了熔断警告日志 (由 profiler_logger_ 打印)
  bool has_warning = (logs.find("Circuit breaker") != std::string::npos) ||
                     (logs.find("Exhausted") != std::string::npos);
  EXPECT_TRUE(has_warning) << "未能检测到熔断警告日志！";

  // 验证被拒绝服务的节点没有产生脏日志
  EXPECT_TRUE(logs.find("Overflow_Task") == std::string::npos);

  // 清理 1024 个槽位，归还给内存池
  for (uint64_t i = 1; i <= 1024; ++i) {
    Z3Y_PROFILE_ASYNC_COMMIT(i);
  }
}

/**
 * @brief 僵尸线程 UAF 防御测试：验证侵入式引用计数与就地超度法则
 */
TEST_F(ProfilerPluginTest, Verify_Zombie_UAF_Protection) {
  uint64_t mock_zombie_id = 9999;

  // 主线程开启槽位
  Z3Y_PROFILE_ASYNC_BEGIN("Zombie_Workflow", mock_zombie_id, 1, 0.0);

  std::atomic<bool> worker_ready{false};
  std::atomic<bool> worker_done{false};

  std::thread zombie_worker([mock_zombie_id, &worker_ready, &worker_done]() {
    // 工作线程挂靠成功，槽位引用计数 +1
    Z3Y_PROFILE_ASYNC_ATTACH(mock_zombie_id);
    worker_ready = true;

    // 模拟工业现场算子卡死 50 毫秒
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // 醒来后继续写入（此时主线程早已执行了 Commit）
    // 验证点：此时槽位不能被物理回收，否则这里将发生 Use-After-Free
    // (野指针踩踏)
    {
      Z3Y_PROFILE_NAMED("Zombie_Step");
      Z3Y_PROFILE_VALUE("Zombie_Value", 42.0);
    }

    // 僵尸线程最后退出，执行减持，由于是最后一人，它将负责“就地超度”
    Z3Y_PROFILE_ASYNC_COMMIT(mock_zombie_id);
    worker_done = true;
  });

  // 主控制线程等待 worker 完成挂靠
  while (!worker_ready) {
    std::this_thread::yield();
  }

  // 主控线程认为超时，或者任务周期已到，不等 Worker 做完，直接 Commit
  // 抛弃监控！ (引用计数 -1，但不为 0，所以不会物理销毁节点树)
  Z3Y_PROFILE_ASYNC_COMMIT(mock_zombie_id);

  // 等待僵尸线程执行完毕
  zombie_worker.join();

  std::string logs = ReadAllLogs();

  // 验证僵尸线程写入的数据依然被完美闭环收集，并没有丢失，也没有导致进程崩溃
  EXPECT_TRUE(logs.find("Zombie_Workflow") != std::string::npos);
  EXPECT_TRUE(logs.find("Zombie_Step") != std::string::npos);
  EXPECT_TRUE(logs.find("Zombie_Value") != std::string::npos);
}