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
 * @file test_event_system.cpp
 * @brief 事件总线 (IEventBus) 全功能覆盖测试
 *
 * @details
 * 本测试覆盖以下维度：
 * 1. **通信模式**:
 * - 全局广播 (Global Broadcast)
 * - 特定发送者单播 (Sender-Specific)
 * 2. **时序模型**:
 * - 同步直接调用 (Direct/Blocking)
 * - 异步队列调用 (Queued/Async)
 * 3. **生命周期管理**:
 * - 手动断开 (Disconnect)
 * - RAII 自动断开 (ScopedConnection)
 * 4. **调试与健壮性 (核心)**:
 * - 异常拦截 (Exception Handler): 验证回调抛出异常时能否被框架捕获。
 * - 事件追踪 (Event Trace Hook): 验证能否监控事件的流转生命周期。
 * 5. **重入安全性 (Re-entrancy)**: [新增]
 * - 验证在回调函数内部调用 Unsubscribe (取消订阅) 是否会导致迭代器失效或死锁。
 * - 验证在回调函数内部递归触发新事件是否会导致死锁。
 */

#include "common/plugin_test_base.h"
#include "framework/z3y_define_impl.h" // 包含 PluginImpl, 宏定义等
#include <thread>
#include <vector>
#include <atomic>

using namespace z3y;

// =============================================================================
// [Mock Objects] 定义测试用的事件和组件
// =============================================================================

/**
 * @brief 测试用事件 payload
 */
struct TestPayloadEvent : public z3y::Event {
    Z3Y_DEFINE_EVENT(TestPayloadEvent, "z3y-test-evt-payload-001");
    int id;
    std::string msg;
    TestPayloadEvent(int i, std::string m) : id(i), msg(std::move(m)) {}
};

/**
 * @brief 另一个测试事件 (用于区分类型)
 */
struct TestSignalEvent : public z3y::Event {
    Z3Y_DEFINE_EVENT(TestSignalEvent, "z3y-test-evt-signal-002");
};

/**
 * @brief 递归测试事件 (用于测试嵌套触发)
 */
struct TestRecursiveEvent : public z3y::Event {
    Z3Y_DEFINE_EVENT(TestRecursiveEvent, "z3y-test-evt-recursive-003");
    int depth; // 递归深度
    explicit TestRecursiveEvent(int d) : depth(d) {}
};

/**
 * @brief 模拟组件 (Sender)
 * @details 用作 SubscribeToSender 测试中的“发布者”身份。
 */
class MockSender : public z3y::PluginImpl<MockSender> {
public:
    Z3Y_DEFINE_COMPONENT_ID("z3y-test-mock-sender-UUID");
};

/**
 * @brief 模拟接收者 (Subscriber)
 * @details 继承 PluginImpl (隐含 enable_shared_from_this) 以便订阅事件。
 */
class MockReceiver : public z3y::PluginImpl<MockReceiver> {
public:
    Z3Y_DEFINE_COMPONENT_ID("z3y-test-mock-receiver-UUID");

    // 接收计数器
    std::atomic<int> received_count{ 0 };
    int last_received_id = -1;

    void OnEvent(const TestPayloadEvent& e) {
        received_count++;
        last_received_id = e.id;
    }

    void OnSignal(const TestSignalEvent& e) {
        received_count++;
    }

    // 会抛出异常的回调
    void OnThrowingEvent(const TestPayloadEvent& e) {
        throw std::runtime_error("Intentional Test Exception");
    }
};

/**
 * @brief [新增] 重入测试辅助类
 * @details
 * 框架的 SubscribeGlobal 要求回调必须是成员函数。
 * 为了测试"回调中取消订阅"和"回调中递归触发"，我们需要一个拥有这些成员函数的类。
 */
class ReentrancyHelper : public z3y::PluginImpl<ReentrancyHelper> {
public:
    Z3Y_DEFINE_COMPONENT_ID("z3y-test-reentrancy-helper-UUID");

    z3y::Connection conn_to_break; // 保存需要断开的连接
    PluginPtr<IEventBus> bus_ref;  // 保存 EventBus 引用以进行递归触发
    int call_count = 0;
    int current_depth = 0;
    int max_depth = 0;

    // 回调：在内部断开连接
    void OnSignalDisconnectSelf(const TestSignalEvent& e) {
        call_count++;
        // [关键操作] 在回调执行期间断开自身连接
        // 如果框架没有处理好迭代器失效问题，这里会 Crash
        if (conn_to_break.IsConnected()) {
            conn_to_break.Disconnect();
        }
    }

    // 回调：递归触发
    void OnRecursiveFire(const TestRecursiveEvent& e) {
        current_depth = e.depth;
        if (e.depth < max_depth && bus_ref) {
            // [关键操作] 在回调内部再次触发事件
            // 如果框架死锁（非递归锁），这里会卡死
            bus_ref->FireGlobal<TestRecursiveEvent>(e.depth + 1);
        }
    }
};

// =============================================================================
// [Test Fixture] 测试夹具
// =============================================================================

class EventSystemTest : public PluginTestBase {
protected:
    void SetUp() override {
        PluginTestBase::SetUp(); // 初始化 PluginManager
        bus_ = z3y::GetDefaultService<IEventBus>(); // 获取 EventBus 单例

        // 创建 Mock 对象
        sender1_ = std::make_shared<MockSender>();
        sender2_ = std::make_shared<MockSender>();
        receiver_ = std::make_shared<MockReceiver>();
        reentrancy_helper_ = std::make_shared<ReentrancyHelper>();

        // 初始化 Mock 对象 (PluginImpl 规范)
        sender1_->Initialize();
        sender2_->Initialize();
        receiver_->Initialize();
        reentrancy_helper_->Initialize();
    }

    // 辅助: 等待异步队列处理完毕
    void WaitForAsync(int ms = 50) {
        std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    }

    PluginPtr<IEventBus> bus_;
    std::shared_ptr<MockSender> sender1_;
    std::shared_ptr<MockSender> sender2_;
    std::shared_ptr<MockReceiver> receiver_;
    std::shared_ptr<ReentrancyHelper> reentrancy_helper_;
};

// =============================================================================
// 1. 基础功能测试 (Global / Direct / Queued)
// =============================================================================

/**
 * @test 全局同步广播 (Direct)
 * @brief 验证 FireGlobal 后，订阅者能立即在当前线程收到回调。
 */
TEST_F(EventSystemTest, GlobalBroadcast_Direct) {
    // 订阅
    z3y::ScopedConnection conn = bus_->SubscribeGlobal<TestPayloadEvent>(
        receiver_, &MockReceiver::OnEvent, ConnectionType::kDirect
    );

    // 触发
    bus_->FireGlobal<TestPayloadEvent>(100, "Hello");

    // 验证: 立即生效
    EXPECT_EQ(receiver_->received_count, 1);
    EXPECT_EQ(receiver_->last_received_id, 100);
}

/**
 * @test 全局异步广播 (Queued)
 * @brief 验证 FireGlobal 后，回调被放入队列，不会阻塞当前线程，稍后执行。
 */
TEST_F(EventSystemTest, GlobalBroadcast_Queued) {
    // 订阅 (kQueued)
    z3y::ScopedConnection conn = bus_->SubscribeGlobal<TestPayloadEvent>(
        receiver_, &MockReceiver::OnEvent, ConnectionType::kQueued
    );

    // 触发
    bus_->FireGlobal<TestPayloadEvent>(200, "Async");

    // 验证: 此时不应立即收到 (因为是异步)
    EXPECT_EQ(receiver_->received_count, 0);

    // 等待处理
    WaitForAsync();

    // 验证: 稍后收到
    EXPECT_EQ(receiver_->received_count, 1);
    EXPECT_EQ(receiver_->last_received_id, 200);
}

// =============================================================================
// 2. 路由功能测试 (Sender Specific)
// =============================================================================

/**
 * @test 特定发送者订阅 (Sender Specific)
 * @brief 验证 SubscribeToSender 能够正确过滤不同实例发出的事件。
 */
TEST_F(EventSystemTest, SenderSpecific_Isolation) {
    // 只订阅 sender1_ 发出的事件
    z3y::ScopedConnection conn = bus_->SubscribeToSender<TestPayloadEvent>(
        sender1_, receiver_, &MockReceiver::OnEvent
    );

    // 场景 1: sender2 触发 -> 预期忽略
    bus_->FireToSender<TestPayloadEvent>(sender2_, 999, "Ignore me");
    EXPECT_EQ(receiver_->received_count, 0);

    // 场景 2: sender1 触发 -> 预期接收
    bus_->FireToSender<TestPayloadEvent>(sender1_, 100, "Accept me");
    EXPECT_EQ(receiver_->received_count, 1);
    EXPECT_EQ(receiver_->last_received_id, 100);

    // 场景 3: 全局触发 -> 预期忽略 (因为是特定订阅)
    bus_->FireGlobal<TestPayloadEvent>(888, "Global");
    EXPECT_EQ(receiver_->received_count, 1); // 计数不变
}

// =============================================================================
// 3. 生命周期管理测试 (RAII & Disconnect)
// =============================================================================

/**
 * @test 连接断开与 RAII
 * @brief 验证手动 Disconnect 和 ScopedConnection 析构后不再接收事件。
 */
TEST_F(EventSystemTest, Connection_Lifecycle) {
    // 1. 测试手动 Disconnect
    {
        z3y::Connection raw_conn = bus_->SubscribeGlobal<TestSignalEvent>(
            receiver_, &MockReceiver::OnSignal
        );

        bus_->FireGlobal<TestSignalEvent>();
        EXPECT_EQ(receiver_->received_count, 1);

        raw_conn.Disconnect(); // 手动断开

        bus_->FireGlobal<TestSignalEvent>();
        EXPECT_EQ(receiver_->received_count, 1); // 计数不应增加
    }

    // 2. 测试 RAII (ScopedConnection)
    receiver_->received_count = 0;
    {
        z3y::ScopedConnection scoped_conn = bus_->SubscribeGlobal<TestSignalEvent>(
            receiver_, &MockReceiver::OnSignal
        );
        bus_->FireGlobal<TestSignalEvent>();
        EXPECT_EQ(receiver_->received_count, 1);
    } // 离开作用域，自动断开

    bus_->FireGlobal<TestSignalEvent>();
    EXPECT_EQ(receiver_->received_count, 1); // 计数不应增加
}

// =============================================================================
// 4. 调试功能测试：异常处理 (Exception Handling)
// =============================================================================

/**
 * @test 异常拦截机制
 * @brief 验证插件回调中抛出的异常能被框架的 SetExceptionHandler 捕获，而不是导致 crash。
 */
TEST_F(EventSystemTest, Debug_ExceptionHandling) {
    std::string caught_error_msg;
    bool exception_caught = false;

    // 1. 注册异常处理器 (Hook)
    manager_->SetExceptionHandler([&](const std::exception& e) {
        caught_error_msg = e.what();
        exception_caught = true;
        });

    // Case A: 同步调用异常 (Direct)
    {
        z3y::ScopedConnection conn = bus_->SubscribeGlobal<TestPayloadEvent>(
            receiver_, &MockReceiver::OnThrowingEvent, ConnectionType::kDirect
        );

        // 触发 -> 回调抛出 -> 框架捕获 -> 调用 Handler
        bus_->FireGlobal<TestPayloadEvent>(1, "Direct Throw");

        EXPECT_TRUE(exception_caught) << "ExceptionHandler was not called for Direct event";
        EXPECT_EQ(caught_error_msg, "Intentional Test Exception");
    }

    // 重置状态
    exception_caught = false;
    caught_error_msg.clear();

    // Case B: 异步调用异常 (Queued)
    {
        z3y::ScopedConnection conn = bus_->SubscribeGlobal<TestPayloadEvent>(
            receiver_, &MockReceiver::OnThrowingEvent, ConnectionType::kQueued
        );

        bus_->FireGlobal<TestPayloadEvent>(2, "Async Throw");
        WaitForAsync();

        EXPECT_TRUE(exception_caught) << "ExceptionHandler was not called for Async event";
        EXPECT_EQ(caught_error_msg, "Intentional Test Exception");
    }
}

// =============================================================================
// 5. 调试功能测试：事件追踪 (Event Tracing)
// =============================================================================

/**
 * @test 事件全链路追踪
 * @brief 验证 SetEventTraceHook 能正确捕获事件流转的各个关键节点。
 */
TEST_F(EventSystemTest, Debug_EventTracing) {
    struct TraceRecord {
        EventTracePoint point;
        EventId evt_id;
    };
    std::vector<TraceRecord> traces;

    // 1. 注册追踪钩子
    manager_->SetEventTraceHook([&](EventTracePoint pt, EventId id, void*, const char*) {
        traces.push_back({ pt, id });
        });

    // Case A: 追踪同步事件
    {
        z3y::ScopedConnection conn = bus_->SubscribeGlobal<TestPayloadEvent>(
            receiver_, &MockReceiver::OnEvent, ConnectionType::kDirect
        );

        bus_->FireGlobal<TestPayloadEvent>(1, "TraceMe");

        bool fired_found = false;
        bool call_found = false;

        for (const auto& t : traces) {
            if (t.evt_id == TestPayloadEvent::kEventId) {
                if (t.point == EventTracePoint::kEventFired) fired_found = true;
                if (t.point == EventTracePoint::kDirectCallStart) call_found = true;
            }
        }
        EXPECT_TRUE(fired_found) << "Missing kEventFired trace";
        EXPECT_TRUE(call_found) << "Missing kDirectCallStart trace";
    }

    traces.clear();

    // Case B: 追踪异步事件
    {
        z3y::ScopedConnection conn = bus_->SubscribeGlobal<TestSignalEvent>(
            receiver_, &MockReceiver::OnSignal, ConnectionType::kQueued
        );

        bus_->FireGlobal<TestSignalEvent>();
        WaitForAsync();

        bool entry_found = false;
        bool exec_found = false;
        for (const auto& t : traces) {
            // 1. 入队 (QueuedEntry) 时：发生在 Fire 调用线程，此时 EventId 是已知的
            if (t.point == EventTracePoint::kQueuedEntry && t.evt_id == TestSignalEvent::kEventId) {
                entry_found = true;
            }

            // 2. 执行 (QueuedExecuteStart) 时：发生在工作线程 EventLoop
            // EventLoop 并不知晓任务内部包裹的 EventId，因此它传出的是 0。
            // 我们只要验证收到了"任意异步任务开始执行"的信号即可。
            if (t.point == EventTracePoint::kQueuedExecuteStart && t.evt_id == 0) {
                exec_found = true;
            }
        }
        EXPECT_TRUE(entry_found) << "Missing kQueuedEntry trace";
        EXPECT_TRUE(exec_found) << "Missing kQueuedExecuteStart trace";
    }
}

// =============================================================================
// 6. 重入安全性测试 (Re-entrancy Safety)
// =============================================================================

/**
 * @test 回调中取消订阅 (Unsubscribe inside Callback)
 * @brief 验证在事件回调函数执行期间，调用 Disconnect() 取消自身订阅是否安全。
 */
TEST_F(EventSystemTest, Reentrancy_UnsubscribeSelf) {
    // 准备辅助对象
    reentrancy_helper_->call_count = 0;

    // 订阅事件，绑定到 helper 的成员函数
    // [核心修复] 使用 ReentrancyHelper 的成员函数，而非 Lambda
    reentrancy_helper_->conn_to_break = bus_->SubscribeGlobal<TestSignalEvent>(
        reentrancy_helper_,
        &ReentrancyHelper::OnSignalDisconnectSelf,
        ConnectionType::kDirect
    );

    // 第一次触发：应收到回调，Helper 内部会调用 Disconnect
    EXPECT_NO_THROW(bus_->FireGlobal<TestSignalEvent>());
    EXPECT_EQ(reentrancy_helper_->call_count, 1);
    EXPECT_FALSE(reentrancy_helper_->conn_to_break.IsConnected()) << "Connection should be disconnected after callback";

    // 第二次触发：不应再收到回调
    bus_->FireGlobal<TestSignalEvent>();
    EXPECT_EQ(reentrancy_helper_->call_count, 1) << "Callback should not be called after disconnection";
}

/**
 * @test 嵌套事件触发 (Recursive Firing)
 * @brief 验证在事件 A 的回调中触发事件 B (或递归触发 A) 是否会导致死锁。
 */
TEST_F(EventSystemTest, Reentrancy_RecursiveFire) {
    reentrancy_helper_->max_depth = 5;
    reentrancy_helper_->current_depth = 0;
    reentrancy_helper_->bus_ref = bus_; // 注入 bus 引用以便 helper 内部触发

    // 订阅递归事件
    z3y::ScopedConnection conn = bus_->SubscribeGlobal<TestRecursiveEvent>(
        reentrancy_helper_,
        &ReentrancyHelper::OnRecursiveFire,
        ConnectionType::kDirect
    );

    // 初始触发 (深度 1)
    // 如果测试没有卡死 (Deadlock) 并成功返回，说明锁机制是重入安全的
    bus_->FireGlobal<TestRecursiveEvent>(1);

    EXPECT_EQ(reentrancy_helper_->current_depth, reentrancy_helper_->max_depth)
        << "Recursive firing failed to reach target depth";
}