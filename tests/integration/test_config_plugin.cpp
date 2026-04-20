/**
 * @file test_config_plugin.cpp
 * @brief ConfigProviderService (配置管理插件) 全功能单元测试套件
 * @details
 * 本测试套件基于 gtest 编写，覆盖了配置插件的以下核心能力：
 * 1. 基础类型支持与 Builder 语法糖测试
 * 2. 强类型安全与非法强转拦截测试
 * 3. 边界校验 (Min/Max) 与权限拦截测试 (Permission/ReadOnly)
 * 4. 占位节点机制 (先订阅、后注册的时序解耦) 测试
 * 5. RAII 生命周期安全 (防止野指针崩溃) 测试
 * 6. ACID 事务批量修改 (全成功或全回滚) 测试
 * 7. UI 快照拉取 (隐藏参数过滤) 测试
 * 8. IO 文件加载与默认值回填测试
 */

#include "common/plugin_test_base.h"       // 引入 PluginTestBase
#include "framework/z3y_service_locator.h" // 引入 ServiceLocator

#include "framework/i_event_bus.h"
#include "framework/connection_type.h"

#include <gtest/gtest.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <memory>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

// 包含暴露给业务的纯虚接口和类型定义
#include "interfaces_core/i_config_service.h"

using namespace z3y;
using namespace z3y::interfaces::core;

/**
 * @brief 测试固件 (Test Fixture)，为每个测试用例提供干净的上下文环境。
 */
class ConfigProviderTest : public PluginTestBase {
 protected:
  void SetUp() override { 
    PluginTestBase::SetUp();

    ASSERT_TRUE(LoadPlugin("plugin_config_manager"));

    // 4. 从服务定位器中通过【纯虚接口】和【UUID】解析服务！
    // 绝对不出现 ConfigProviderService 的任何字眼
    config_ = z3y::GetDefaultService<z3y::interfaces::core::IConfigService>();
    ASSERT_NE(config_, nullptr)
        << "Fatal: IConfigService not found in ServiceLocator!";

    test_db_path_ = "test_config_db.json";
    config_->SetStoragePath(test_db_path_);
  }

  void TearDown() override { 
    // 2. 清理产生的临时测试文件
    std::error_code ec;
    std::filesystem::remove(test_db_path_, ec);
    std::filesystem::remove(test_db_path_ + ".tmp", ec);

    config_.reset();

    PluginTestBase::TearDown();
  }

  // 必须使用框架专属的智能指针，保障跨 DLL 的内存 ABI 安全！
  z3y::PluginPtr<z3y::interfaces::core::IConfigService> config_;
  std::string test_db_path_;
};

// ============================================================================
// 测试组 1：Builder 链式构建与基础类型支持
// ============================================================================

TEST_F(ConfigProviderTest, BuilderFluentAPIAndBasicTypes) {
  // 【场景】业务插件注册并订阅整型、浮点型、布尔型和字符串四种基本类型
  int captured_int = 0;
  double captured_double = 0.0;
  bool captured_bool = false;
  std::string captured_str;

  // 1. 注册整数型参数
  auto conn1 = config_->Builder<int>("Camera.Exposure")
                   .NameKey("曝光时间")
                   .Default(1000)
                   .Min(100)
                   .Max(5000)
                   .Bind([&captured_int](int val) { captured_int = val; });

  // 2. 注册浮点型参数
  auto conn2 =
      config_->Builder<double>("Robot.Speed")
          .NameKey("运动速度")
          .Default(1.5)
          .Bind([&captured_double](double val) { captured_double = val; });

  // 3. 注册布尔型参数
  auto conn3 = config_->Builder<bool>("System.Debug")
                   .Default(false)
                   .Bind([&captured_bool](bool val) { captured_bool = val; });

  // 4. 注册字符串型参数
  auto conn4 = config_->Builder<std::string>("Network.IP")
                   .Default("192.168.1.100")
                   .Bind([&captured_str](const std::string& val) {
                     captured_str = val;
                   });

  // 【断言】Bind() 调用的瞬间，应该立刻触发一次回调，将默认值回填给业务变量
  EXPECT_EQ(captured_int, 1000);
  EXPECT_EQ(captured_double, 1.5);
  EXPECT_FALSE(captured_bool);
  EXPECT_EQ(captured_str, "192.168.1.100");

  // 【断言】使用安全接口写入新值，并确认回调被正确触发
  EXPECT_TRUE(config_->SetValueSafe<int>("Camera.Exposure", 2000));
  EXPECT_EQ(captured_int, 2000);

  EXPECT_TRUE(config_->SetValueSafe<std::string>("Network.IP", "127.0.0.1"));
  EXPECT_EQ(captured_str, "127.0.0.1");
}

TEST_F(ConfigProviderTest, VectorAndEnumTypes) {
  // 【场景】测试对枚举类型和一维数组的支持
  enum class TriggerMode { kSoftware = 0, kHardware = 1 };
  TriggerMode captured_enum = TriggerMode::kSoftware;
  std::vector<int64_t> captured_vec;

  // 注册枚举类型 (底层会自动转换为 int64_t 存储)
  auto conn_enum =
      config_->Builder<TriggerMode>("Camera.TriggerMode")
          .Default(TriggerMode::kSoftware)
          .Enum({"0", "1"}, {"软触发", "硬触发"})
          .Bind([&captured_enum](TriggerMode val) { captured_enum = val; });

  // 注册数组类型
  std::vector<int64_t> def_vec = {10, 20, 30};
  auto conn_vec = config_->Builder<std::vector<int64_t>>("System.ROI")
                      .Default(def_vec)
                      .Min(0)    // 对数组的每个元素进行最小值约束
                      .Max(100)  // 对数组的每个元素进行最大值约束
                      .Bind([&captured_vec](const std::vector<int64_t>& val) {
                        captured_vec = val;
                      });

  // 【断言】验证枚举赋值
  EXPECT_TRUE(config_->SetValueSafe<TriggerMode>("Camera.TriggerMode",
                                                 TriggerMode::kHardware));
  EXPECT_EQ(captured_enum, TriggerMode::kHardware);

  // 【断言】验证数组赋值及数组元素的边界校验
  EXPECT_TRUE(
      config_->SetValueSafe<std::vector<int64_t>>("System.ROI", {50, 60}));
  EXPECT_EQ(captured_vec.size(), 2);
  EXPECT_EQ(captured_vec[0], 50);

  // 尝试赋越界数组 (其中一个元素超出了 100)，应该被整体拒绝
  EXPECT_FALSE(
      config_->SetValueSafe<std::vector<int64_t>>("System.ROI", {50, 150}));
  EXPECT_EQ(captured_vec[0], 50);  // 原值未被破坏
}

// ============================================================================
// 测试组 2：数据合法性校验防线 (Validation & Security)
// ============================================================================

TEST_F(ConfigProviderTest, BoundaryValidation) {
  // 【场景】验证当数据超出 Min/Max 设定时，系统是否会铁面无私地拦截
  int trigger_count = 0;
  auto conn = config_->Builder<int>("Motor.Speed")
                  .Default(50)
                  .Min(0)
                  .Max(100)
                  .Bind([&trigger_count](int) { trigger_count++; });

  EXPECT_EQ(trigger_count, 1);  // 初始回调触发 1 次

  // 1. 合法修改
  EXPECT_TRUE(config_->SetValueSafe<int>("Motor.Speed", 60));
  EXPECT_EQ(trigger_count, 2);  // 回调触发，计数 +1
  EXPECT_EQ(config_->GetValueSafe<int>("Motor.Speed"), 60);

  // 2. 越界修改 (超过 Max) -> 必须被拦截
  EXPECT_FALSE(config_->SetValueSafe<int>("Motor.Speed", 101));
  EXPECT_EQ(trigger_count, 2);  // 拦截成功，回调不触发
  EXPECT_EQ(config_->GetValueSafe<int>("Motor.Speed"), 60);  // 值未被篡改

  // 3. 越界修改 (低于 Min) -> 必须被拦截
  EXPECT_FALSE(config_->SetValueSafe<int>("Motor.Speed", -1));
  EXPECT_EQ(trigger_count, 2);
}

TEST_F(ConfigProviderTest, ReadOnlyAndPermission) {
  // 【场景】验证只读属性和权限令牌功能

  // 1. 注册只读参数
  config_->Builder<std::string>("Status.Version")
      .Default("v1.0.0")
      .ReadOnly(true)
      .RegisterOnly();  // 仅注册，不绑回调

  // 企图修改只读参数将被拦截
  EXPECT_FALSE(config_->SetValueSafe<std::string>("Status.Version", "v2.0.0"));
  EXPECT_EQ(config_->GetValueSafe<std::string>("Status.Version"), "v1.0.0");

  // 2. 注册带权限拦截的参数
  config_->Builder<int>("System.SecretParam")
      .Default(0)
      .Permission("admin_only")  // 指定必须拥有 admin_only 角色才能改
      .RegisterOnly();

  // 空角色或错误角色修改被拒绝
  EXPECT_FALSE(config_->SetValueSafe<int>("System.SecretParam", 99));
  EXPECT_FALSE(config_->SetValueSafe<int>("System.SecretParam", 99, "guest"));

  // 正确角色修改成功
  EXPECT_TRUE(
      config_->SetValueSafe<int>("System.SecretParam", 99, "admin_only"));
  EXPECT_EQ(config_->GetValueSafe<int>("System.SecretParam"), 99);
}

TEST_F(ConfigProviderTest, TypeSafetyMismatch) {
  // 【场景】验证底层的 std::variant 强类型保护，防止因为类型乱转导致内存崩溃

  auto conn = config_->Builder<int>("Param.Count").Default(10).Bind([](int) {});

  // 1. 尝试以 double 强行写入 int 节点
  EXPECT_FALSE(config_->SetValueSafe<double>("Param.Count", 10.5));

  // 2. 尝试使用 std::string 强行写入 int 节点
  EXPECT_FALSE(config_->SetValueSafe<std::string>("Param.Count", "10"));

  // 3. GetValueSafe 类型不匹配时，自动安全回退为默认值 (100)
  auto wrong_val =
      config_->GetValueSafe<std::string>("Param.Count", "FallbackVal");
  EXPECT_EQ(wrong_val, "FallbackVal");

  // 4. 极端测试：真实的强转拦截验证
  // 先合法注册一个 double 类型的参数，牢牢占据拓扑树
  config_->Builder<double>("Param.DoubleNode").Default(1.5).RegisterOnly();

  // 然后企图使用 int 强行去重新 Builder/Bind 这个已经定型的 double 节点！
  // 底层派发回调初始值时，将触发 bad_variant_access，被 TypeSafeWrapper
  // 拦截并抛出 invalid_argument
  EXPECT_THROW(
      {
        auto err_conn =
            config_->Builder<int>("Param.DoubleNode").Default(1).Bind([](int) {
            });
      },
      std::invalid_argument);
}

// ============================================================================
// 测试组 3：高级交互机制 (Phantom Node, RAII, Transactions)
// ============================================================================

TEST_F(ConfigProviderTest, CallbackTriggerLogic) {
  // 【场景】测试值相同时，防抖机制应阻止触发回调
  int trigger_count = 0;
  auto conn = config_->Builder<int>("Param.Test")
                  .Default(1)
                  .Bind([&trigger_count](int) { trigger_count++; });

  EXPECT_EQ(trigger_count, 1);

  // 赋相同的值，返回 true (操作成功)，但由于值无实质变化，不应触发业务回调
  EXPECT_TRUE(config_->SetValueSafe<int>("Param.Test", 1));
  EXPECT_EQ(trigger_count, 1);

  // 赋不同的值，触发回调
  EXPECT_TRUE(config_->SetValueSafe<int>("Param.Test", 2));
  EXPECT_EQ(trigger_count, 2);
}

TEST_F(ConfigProviderTest, PhantomNodeUpgrade) {
  // 【场景】极其硬核的“占位节点机制”测试。
  // 假设 UI 插件先启动，尝试监听一个还没被硬件插件注册的参数。

  int ui_captured_val = 0;

  // 此时 "Camera.Hardware.Exposure" 根本不存在！但系统会生成占位节点。
  auto ui_conn = config_->Subscribe<int>(
      "Camera.Hardware.Exposure",
      [&ui_captured_val](int val) { ui_captured_val = val; });

  // 此时值为空，不会触发
  EXPECT_EQ(ui_captured_val, 0);

  // 随后，硬件插件终于加载了，开始真正注册这个节点！
  auto hw_conn = config_->Builder<int>("Camera.Hardware.Exposure")
                     .Default(888)
                     .Min(100)
                     .Max(1000)
                     .Bind([](int) {});  // 硬件自身的绑定

  // 【断言】占位节点转正的瞬间，系统必须立刻把硬件插件设定的默认值(888)，
  // 穿越时空补发给早早等候的 UI 插件回调！
  EXPECT_EQ(ui_captured_val, 888);
}

TEST_F(ConfigProviderTest, RAIIConnectionRelease) {
  // 【场景】验证当 ScopedConnection
  // 析构时，回调是否真的被安全解除了，防止野指针。
  int capture = 0;

  {
    // 在局部作用域内创建一个连接
    auto temp_conn = config_->Builder<int>("Temp.Param")
                         .Default(10)
                         .Bind([&capture](int val) { capture = val; });

    EXPECT_EQ(capture, 10);
    config_->SetValueSafe<int>("Temp.Param", 20);
    EXPECT_EQ(capture, 20);

    // 离开作用域，temp_conn 析构，自动解除绑定
  }

  // 此时连接已断开，再修改该值，业务捕获变量不应该发生变化！
  config_->SetValueSafe<int>("Temp.Param", 30);
  EXPECT_EQ(capture, 20);  // 依然是 20，拦截野指针调用成功！
}

TEST_F(ConfigProviderTest, BatchTransactionRollback) {
  // 【场景】测试 ACID 事务特性：多个参数同时提交，一损俱损一荣俱荣。

  config_->Builder<int>("ParamX").Default(0).Max(10).RegisterOnly();
  config_->Builder<int>("ParamY").Default(0).Max(10).RegisterOnly();

  // 创建事务批处理，把 X 设为合法值 5，但把 Y 设为非法值 99
  auto batch = config_->CreateBatch();
  batch.Set("ParamX", 5).Set("ParamY", 99);

  auto errors = batch.Commit();

  // 【断言】提交必须返回错误信息
  EXPECT_FALSE(errors.empty());

  // 【核心断言】因为 Y 非法，整个事务必须回滚。X 绝对不能被修改为 5！
  EXPECT_EQ(config_->GetValueSafe<int>("ParamX"), 0);
  EXPECT_EQ(config_->GetValueSafe<int>("ParamY"), 0);

  // 重新创建合法事务
  auto batch2 = config_->CreateBatch();
  batch2.Set("ParamX", 5).Set("ParamY", 5);
  auto errors2 = batch2.Commit();

  EXPECT_TRUE(errors2.empty());                        // 0 错误
  EXPECT_EQ(config_->GetValueSafe<int>("ParamX"), 5);  // 成功应用
}

// ============================================================================
// 测试组 4：UI 渲染接口与持久化机制
// ============================================================================

TEST_F(ConfigProviderTest, UIQueryGetAllConfigs) {
  // 【场景】测试提供给前端 UI 生成界面的全量拉取接口，确认是否过滤了 Hidden
  // 参数。

  // 1. 普通参数（应暴露给 UI）
  config_->Builder<int>("UI.VisibleParam").Default(1).RegisterOnly();

  // 2. 隐藏参数（纯后台使用，应向 UI 隐藏）
  config_->Builder<int>("Core.HiddenParam")
      .Default(2)
      .Hidden(true)
      .RegisterOnly();

  auto ui_snapshot = config_->GetAllConfigs();

  // 【断言】快照中只能找到 VisibleParam，找不到 HiddenParam
  EXPECT_TRUE(ui_snapshot.find("UI.VisibleParam") != ui_snapshot.end());
  EXPECT_TRUE(ui_snapshot.find("Core.HiddenParam") == ui_snapshot.end());
}

TEST_F(ConfigProviderTest, FileIOAndInitialLoad) {
  // 【场景】测试配置文件持久化。
  // 模拟程序第二次启动，从硬盘读取历史数据并自动覆盖代码中的 Default() 默认值。

  // 1. 手工伪造一个旧的配置文件 (模拟硬盘里已有数据)
  std::ofstream ofs(test_db_path_);
  ofs << R"({
      "Saved.Param": 999
  })";
  ofs.close();

  // 2. 强制触发重新读取 (把刚伪造的 json 读进内存的 initial_load_cache_ 池中)
  config_->SetStoragePath(test_db_path_);

  // 3. 插件启动了，开始注册这个参数，代码里填的 Default 是 1
  int capture = 0;
  auto conn =
      config_->Builder<int>("Saved.Param").Default(1).Bind([&capture](int val) {
        capture = val;
      });

  // 【核心断言】系统发现硬盘里有缓存数据 (999)，必须抛弃代码里的 Default(1)，
  // 使用硬盘数据进行初始化和绑定回调！
  EXPECT_EQ(capture, 999);
  EXPECT_EQ(config_->GetValueSafe<int>("Saved.Param"), 999);
}

// ============================================================================
// 测试组 5：高阶机制补充测试 (循环保护、高并发、异步 IO、生命周期)
// ============================================================================

TEST_F(ConfigProviderTest, CyclicUpdateProtection) {
  // 【场景】测试“防栈溢出”保护机制。
  // 插件A监听"Param.A"并修改"Param.B"；插件B监听"Param.B"并修改"Param.A"。

  int trigger_A = 0;
  int trigger_B = 0;

  // 使用 ConnectionGroup 管理多个连接
  ConnectionGroup group;

  group += config_->Builder<int>("Param.A").Default(0).Bind(
      [this, &trigger_A](int val) {
        trigger_A++;
        // A 的回调中去设置 B
        this->config_->SetValueSafe<int>("Param.B", val + 1);
      });

  group += config_->Builder<int>("Param.B").Default(0).Bind(
      [this, &trigger_B](int val) {
        trigger_B++;
        // B 的回调中又去设置 A，形成死亡闭环！
        this->config_->SetValueSafe<int>("Param.A", val + 1);
      });

  // 【触发死亡闭环】
  // 如果底层的 recursion_depth 保护失效，这里会直接导致测试进程 Stack Overflow
  // 崩溃退出！
  bool success = config_->SetValueSafe<int>("Param.A", 100);

  // 【断言】
  // 1. SetValueSafe 应该在递归深度 > 3 时主动返回 false 进行拦截
  EXPECT_FALSE(success);
  // 2. 触发次数不应该太大（说明被成功掐断在摇篮里）
  EXPECT_LT(trigger_A, 10);
  EXPECT_LT(trigger_B, 10);
}

TEST_F(ConfigProviderTest, ConcurrencyStressAndLockFree) {
  // 【场景】多线程狂暴读写测试，验证双重读写锁与防死锁排序算法的健壮性。

  std::atomic<int> callback_hits{0};

  auto conn1 = config_->Builder<int>("Thread.ValA").Default(0).Bind([&](int) {
    callback_hits++;
  });
  auto conn2 = config_->Builder<int>("Thread.ValB").Default(0).Bind([&](int) {
    callback_hits++;
  });

  auto write_task = [&]() {
    for (int i = 0; i < 1000; i++) {
      // 奇数次修改 A，偶数次修改 B，制造交叉锁竞争
      if (i % 2 == 0) {
        config_->SetValueSafe("Thread.ValA", i);
      } else {
        config_->SetValueSafe("Thread.ValB", i);
      }
      // 随机发起包含两个参数的并发事务，考验 ApplyBatch 的地址排序防死锁机制
      if (i % 100 == 0) {
        auto batch = config_->CreateBatch();
        batch.Set("Thread.ValA", i).Set("Thread.ValB", i);
        batch.Commit();
      }

      // 【补充】：让出 CPU 时间片，打破死锁僵局
      std::this_thread::yield();
    }
  };

  auto read_task = [&]() {
    for (int i = 0; i < 1000; i++) {
      // 疯狂读取，考验 Shared Lock (读锁) 并发性能
      int a = config_->GetValueSafe<int>("Thread.ValA");
      int b = config_->GetValueSafe<int>("Thread.ValB");

      // 模拟真实业务处理的微小停顿，避免共享锁被永不间断地霸占
      std::this_thread::yield();
    }
  };

  std::vector<std::thread> threads;
  // 启动 10 个写线程和 10 个读线程
  for (int i = 0; i < 10; i++) threads.emplace_back(write_task);
  for (int i = 0; i < 10; i++) threads.emplace_back(read_task);

  for (auto& t : threads) {
    if (t.joinable()) t.join();
  }

  // 【断言】只要程序能活着走到这里没有死锁卡住，就说明并发设计无懈可击！
  EXPECT_GT(callback_hits.load(), 0);
}

TEST_F(ConfigProviderTest, AsyncWorkerIOAndDebounce) {
  // 【场景】验证后台 Worker 线程的 500ms 防抖落盘机制。

  config_->Builder<int>("Async.WriteTest").Default(0).RegisterOnly();

  // 1. 连续高频赋值 100 次（模拟用户拖拽滑动条）
  for (int i = 1; i <= 100; i++) {
    config_->SetValueSafe<int>("Async.WriteTest", i);
  }

  // 2. 此时立即去读硬盘里的文件，应该还读不到 100，因为 500ms 的防抖还没过
  // (注意：如果是刚建的文件可能还不存在，这里只验证逻辑)

  // 3. 阻塞等待 800ms，让 Worker 线程从 condition_variable
  // 中醒来并完成原子重命名落盘
  std::this_thread::sleep_for(std::chrono::milliseconds(800));

  // 4. 绕过内存，直接暴力读取底层 JSON 文件
  std::ifstream ifs(test_db_path_);
  ASSERT_TRUE(ifs.is_open())
      << "Configuration file was not created by Worker Thread!";

  nlohmann::json root;
  ifs >> root;
  ifs.close();

  // 【断言】硬盘文件里确实被正确写入了最后一次拖拽的值 (100)
  EXPECT_TRUE(root.contains("Async.WriteTest"));
  EXPECT_EQ(root["Async.WriteTest"].get<int>(), 100);
}

TEST_F(ConfigProviderTest, ConnectionGroupClear) {
  // 【场景】测试 ConnectionGroup 的统一清理能力
  int capture1 = 0, capture2 = 0;

  ConnectionGroup group;
  group += config_->Builder<int>("Group.A").Default(10).Bind(
      [&](int v) { capture1 = v; });
  group += config_->Builder<int>("Group.B").Default(20).Bind(
      [&](int v) { capture2 = v; });

  EXPECT_EQ(capture1, 10);
  EXPECT_EQ(capture2, 20);

  // 手动调用 Clear 清空所有连接
  group.Clear();

  // 再修改参数，回调不应再触发
  config_->SetValueSafe("Group.A", 99);
  config_->SetValueSafe("Group.B", 99);

  EXPECT_EQ(capture1, 10);  // 依然是旧值
  EXPECT_EQ(capture2, 20);  // 依然是旧值
}

// ============================================================================
// 测试组 6：企业级高级特性 (校验、重置、配方管理、分组查询)
// ============================================================================

TEST_F(ConfigProviderTest, CustomValidatorLogic) {
  // 【场景】测试图灵完备的自定义校验闭包是否能正确拦截非法业务逻辑
  int trigger_count = 0;

  auto conn = config_->Builder<int>("Camera.FrameRate")
                  .Default(30)
                  .Validator([](int val) -> std::string {
                    if (val % 2 != 0) return "帧率必须是偶数";
                    if (val > 100) return "帧率超出了硬件物理极限";
                    return "";  // 返回空字符串表示通过
                  })
                  .Bind([&](int) { trigger_count++; });

  EXPECT_EQ(trigger_count, 1);

  // 1. 合法修改 (偶数且小于 100)
  EXPECT_TRUE(config_->SetValueSafe<int>("Camera.FrameRate", 60));
  EXPECT_EQ(config_->GetValueSafe<int>("Camera.FrameRate"), 60);
  EXPECT_EQ(trigger_count, 2);

  // 2. 非法修改：奇数拦截
  EXPECT_FALSE(config_->SetValueSafe<int>("Camera.FrameRate", 61));
  EXPECT_EQ(config_->GetValueSafe<int>("Camera.FrameRate"), 60);  // 维持原状

  // 3. 非法修改：超出物理极限拦截
  EXPECT_FALSE(config_->SetValueSafe<int>("Camera.FrameRate", 120));
  EXPECT_EQ(config_->GetValueSafe<int>("Camera.FrameRate"), 60);  // 维持原状
}

TEST_F(ConfigProviderTest, ResetToDefaultAndGroupReset) {
  // 【场景】测试原子级单参数重置，以及模块级的 Group 批量重置
  config_->Builder<int>("GroupA.Param1")
      .GroupKey("GroupA")
      .Default(10)
      .RegisterOnly();
  config_->Builder<int>("GroupA.Param2")
      .GroupKey("GroupA")
      .Default(20)
      .RegisterOnly();
  config_->Builder<int>("GroupB.Param1")
      .GroupKey("GroupB")
      .Default(30)
      .RegisterOnly();

  // 把所有参数改乱
  config_->SetValueSafe("GroupA.Param1", 99);
  config_->SetValueSafe("GroupA.Param2", 99);
  config_->SetValueSafe("GroupB.Param1", 99);

  // 1. 单独重置 GroupA.Param1
  EXPECT_TRUE(config_->ResetToDefault("GroupA.Param1"));
  EXPECT_EQ(config_->GetValueSafe<int>("GroupA.Param1"), 10);  // 恢复为 10
  EXPECT_EQ(config_->GetValueSafe<int>("GroupA.Param2"),
            99);  // Param2 不受影响

  // 2. 批量重置整个 GroupA 页签
  config_->ResetGroupToDefault("GroupA");
  EXPECT_EQ(config_->GetValueSafe<int>("GroupA.Param2"), 20);  // 恢复为 20
  EXPECT_EQ(config_->GetValueSafe<int>("GroupB.Param1"),
            99);  // GroupB 绝不能受误伤
}

TEST_F(ConfigProviderTest, ExportImportProfileRecipe) {
  // 【场景】测试配方配置文件的导出备份，与强行覆盖导入
  config_->Builder<int>("Recipe.Speed").Default(100).RegisterOnly();
  config_->SetValueSafe("Recipe.Speed", 300);  // 当前配方速度调到了 300

  std::string recipe_file = "test_recipe_product_X.json";

  // 1. 导出当前配方
  EXPECT_TRUE(config_->ExportToFile(recipe_file));

  // 2. 随意篡改当前内存
  config_->SetValueSafe("Recipe.Speed", 500);

  // 3. 一键导入配方 (apply_immediately = true 将立刻触发 Reload)
  EXPECT_TRUE(config_->ImportFromFile(recipe_file, true));

  // 【断言】必须瞬间恢复到导出时的快照 300
  EXPECT_EQ(config_->GetValueSafe<int>("Recipe.Speed"), 300);

  // 清理临时配方文件
  std::filesystem::remove(recipe_file);
}

TEST_F(ConfigProviderTest, GetConfigsByGroupQuery) {
  // 【场景】测试 UI 懒加载所需的过滤查询 API
  config_->Builder<int>("UI.Visible1")
      .GroupKey("Tab_Motion")
      .Default(1)
      .RegisterOnly();
  config_->Builder<int>("UI.Visible2")
      .GroupKey("Tab_Motion")
      .Default(2)
      .RegisterOnly();
  config_->Builder<int>("UI.Other")
      .GroupKey("Tab_Vision")
      .Default(3)
      .RegisterOnly();
  config_->Builder<int>("UI.Hidden")
      .GroupKey("Tab_Motion")
      .Default(4)
      .Hidden(true)
      .RegisterOnly();

  // 前端仅拉取 "Tab_Motion" 这个页签的数据
  auto snapshot_map = config_->GetConfigsByGroup("Tab_Motion");

  // 【断言】
  EXPECT_EQ(snapshot_map.size(), 2);  // 必须精准拿到 2 个
  EXPECT_TRUE(snapshot_map.find("UI.Visible1") != snapshot_map.end());
  EXPECT_TRUE(snapshot_map.find("UI.Visible2") != snapshot_map.end());
  // 绝对不能拿到别的 Tab 的参数
  EXPECT_TRUE(snapshot_map.find("UI.Other") == snapshot_map.end());
  // 绝对不能拿到 is_hidden = true 的后端参数
  EXPECT_TRUE(snapshot_map.find("UI.Hidden") == snapshot_map.end());
}

TEST_F(ConfigProviderTest, PhantomNodeReloadBugFixRegression) {
  // 【场景】极其刁钻的边界情况验证：占位节点在遇到 Reload 时是否会吞噬数据！

  int ui_captured = 0;
  // 1. UI 提前订阅，生成了一个 std::monostate 的占位节点
  auto conn_ui =
      config_->Subscribe<int>("Device.ID", [&](int v) { ui_captured = v; });

  // 2. 外部程序强行篡改了物理文件并触发了 Reload
  std::ofstream ofs(test_db_path_);
  ofs << R"({"Device.ID": 888})";
  ofs.close();

  // 如果没有补上上面的 Bug Fix 代码，这里就会把 888 吞噬掉！
  config_->ReloadFromFile();

  // 3. 后端硬件插件终于启动并正式注册节点
  auto conn_hw = config_->Builder<int>("Device.ID")
                     .Default(1)  // 代码里默认是 1
                     .Bind([](int) {});

  // 【断言】占位节点必须成功通过 initial_load_cache_ 存活下来，并认领 888
  EXPECT_EQ(config_->GetValueSafe<int>("Device.ID"), 888);
  EXPECT_EQ(ui_captured, 888);  // 并且成功通知给了等待已久的 UI
}

/**
 * @brief 测试配置变更审计事件的发布与跨插件接收
 */
// ============================================================================
// 辅助 Mock 组件：用于接收和验证审计事件
// ============================================================================
class AuditEventReceiver : public z3y::PluginImpl<AuditEventReceiver> {
 public:
  Z3Y_DEFINE_COMPONENT_ID("z3y-test-audit-receiver-UUID");

  // 用于收集总线发来的事件
  std::vector<z3y::interfaces::core::ConfigChangedEvent> received_events;

  // 符合框架规范的事件处理回调函数
  void OnConfigChanged(const z3y::interfaces::core::ConfigChangedEvent& e) {
    received_events.push_back(e);
  }
};

// ============================================================================
// 测试用例：验证配置变更审计事件的发布与跨插件接收
// ============================================================================
TEST_F(ConfigProviderTest, AuditTrailEventPublication) {
  // 1. 获取全局事件总线 (使用 z3y 原生接口)
  auto event_bus = z3y::GetDefaultService<z3y::IEventBus>();
  ASSERT_NE(event_bus, nullptr);

  // 2. 创建并初始化 Mock 接收者
  auto receiver = std::make_shared<AuditEventReceiver>();
  receiver->Initialize();

  // 3. 订阅 ConfigChangedEvent 事件 (使用与 test_event_system.cpp
  // 完全一致的语法)
  z3y::ScopedConnection conn =
      event_bus->SubscribeGlobal<z3y::interfaces::core::ConfigChangedEvent>(
          receiver, &AuditEventReceiver::OnConfigChanged,
          z3y::ConnectionType::kDirect);

  // 4. 触发配置修改
  config_->Builder<int>("Audit.Param").Default(100).RegisterOnly();

  // 动作 A: 单项修改
  config_->SetValueSafe("Audit.Param", 200, "Maintainer");

  // 动作 B: 批量事务修改
  auto batch = config_->CreateBatch();
  batch.Set("Audit.Param", 300);
  batch.Commit("Operator");

  // 5. 严格断言验证
  ASSERT_EQ(receiver->received_events.size(), 2);

  // 验证动作 A 的审计事件
  EXPECT_EQ(receiver->received_events[0].path, "Audit.Param");
  EXPECT_EQ(receiver->received_events[0].old_value, "100");
  EXPECT_EQ(receiver->received_events[0].new_value, "200");
  EXPECT_EQ(receiver->received_events[0].operator_role, "Maintainer");
  EXPECT_GT(receiver->received_events[0].timestamp_ms, 0);  // 时间戳必须有效

  // 验证动作 B 的审计事件
  EXPECT_EQ(receiver->received_events[1].path, "Audit.Param");
  EXPECT_EQ(receiver->received_events[1].old_value, "200");
  EXPECT_EQ(receiver->received_events[1].new_value, "300");
  EXPECT_EQ(receiver->received_events[1].operator_role, "Operator");
}

TEST_F(ConfigProviderTest, SchemaMetadataRetention) {
  // 【场景】验证 UI 渲染强相关的元数据（NameKey, GroupKey,
  // SubGroupKey）是否被精准存储与提取
  // 这个测试防止未来有人修改底层逻辑时，不小心把前端 UI
  // 极其依赖的布局字段给弄丢了。

  // 1. 模拟后端：注册一个带有完整 UI 多语言/布局元数据的参数
  config_->Builder<int>("Hardware.Camera.Gain")
      .NameKey("UI_PARAM_GAIN")   // 多语言翻译键：模拟增益
      .GroupKey("UI_TAB_VISION")  // 一级分组：视觉选项卡
      .SubGroupKey(
          "UI_GROUP_SENSOR")  // 二级分组：传感器参数框 (本次新增的测试点)
      .Default(100)
      .RegisterOnly();

  // 2. 模拟前端 UI：在用户点击 "UI_TAB_VISION"
  // 选项卡时，懒加载获取该页签下的所有快照
  auto snapshot_map = config_->GetConfigsByGroup("UI_TAB_VISION");

  // 3. 严格断言验证
  // 确保参数真的被拉取到了
  ASSERT_TRUE(snapshot_map.find("Hardware.Camera.Gain") != snapshot_map.end())
      << "未能通过 GroupKey 拉取到指定的参数快照！";

  // 提取该参数的元数据 (SchemaMetadata)
  auto meta = snapshot_map["Hardware.Camera.Gain"].meta;

  // 【核心断言】：检查我们新加的 SubGroupKey 以及其他布局字段是否原样保留
  EXPECT_EQ(meta.name_key, "UI_PARAM_GAIN");
  EXPECT_EQ(meta.group_key, "UI_TAB_VISION");
  EXPECT_EQ(meta.subgroup_key, "UI_GROUP_SENSOR")
      << "SubGroupKey 丢失或错误！请检查 ConfigBuilder::SubGroupKey 赋值逻辑。";
}

// ============================================================================
// GTest Main 引导入口
// ============================================================================
int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}