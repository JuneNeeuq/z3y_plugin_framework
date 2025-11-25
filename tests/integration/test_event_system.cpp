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
 * @brief [集成测试] 事件总线 (IEventBus) 全功能测试。
 *
 * @details
 * 本文件使用 GTest 框架，对事件总线进行黑盒测试。
 * 覆盖场景：
 * 1. 同步广播 (Global Direct)
 * 2. 异步广播 (Global Queued)
 * 3. 路由隔离 (Sender Specific)
 * 4. 连接生命周期 (Manual vs RAII)
 * 5. 异常处理 (OOB Handler)
 * 6. 事件追踪钩子 (Trace Hook)
 * 7. 重入安全性 (Reentrancy)
 */

#include "common/plugin_test_base.h"
#include "framework/z3y_define_impl.h" 
#include <thread>
#include <vector>
#include <atomic>

using namespace z3y;

// =============================================================================
// [Mock Objects] 定义测试用的事件和组件
// =============================================================================

/** @brief 测试事件：带数据负载。 */
struct TestPayloadEvent : public z3y::Event {
    Z3Y_DEFINE_EVENT(TestPayloadEvent, "z3y-test-evt-payload-001");
    int id;
    std::string msg;
    TestPayloadEvent(int i, std::string m) : id(i), msg(std::move(m)) {}
};

/** @brief 测试事件：纯信号，无负载。 */
struct TestSignalEvent : public z3y::Event {
    Z3Y_DEFINE_EVENT(TestSignalEvent, "z3y-test-evt-signal-002");
};

/** @brief 测试事件：用于递归触发测试。 */
struct TestRecursiveEvent : public z3y::Event {
    Z3Y_DEFINE_EVENT(TestRecursiveEvent, "z3y-test-evt-recursive-003");
    int depth;
    explicit TestRecursiveEvent(int d) : depth(d) {}
};

/** @brief Mock 发送者组件。 */
class MockSender : public z3y::PluginImpl<MockSender> {
public:
    Z3Y_DEFINE_COMPONENT_ID("z3y-test-mock-sender-UUID");
};

/** @brief Mock 接收者组件。 */
class MockReceiver : public z3y::PluginImpl<MockReceiver> {
public:
    Z3Y_DEFINE_COMPONENT_ID("z3y-test-mock-receiver-UUID");

    std::atomic<int> received_count{ 0 };
    int last_received_id = -1;
    std::thread::id last_thread_id; // 用于验证是在哪个线程执行的

    void OnEvent(const TestPayloadEvent& e) {
        received_count++;
        last_received_id = e.id;
        last_thread_id = std::this_thread::get_id();
    }

    void OnSignal(const TestSignalEvent& e) {
        received_count++;
        last_thread_id = std::this_thread::get_id();
    }

    void OnThrowingEvent(const TestPayloadEvent& e) {
        throw std::runtime_error("Intentional Test Exception");
    }
};

/** @brief 辅助类：用于测试重入（在回调中操作总线）。 */
class ReentrancyHelper : public z3y::PluginImpl<ReentrancyHelper> {
public:
    Z3Y_DEFINE_COMPONENT_ID("z3y-test-reentrancy-helper-UUID");

    z3y::Connection conn_to_break;
    PluginPtr<IEventBus> bus_ref;
    int call_count = 0;
    int current_depth = 0;
    int max_depth = 0;

    // 在回调中断开自己
    void OnSignalDisconnectSelf(const TestSignalEvent& e) {
        call_count++;
        if (conn_to_break.IsConnected()) {
            conn_to_break.Disconnect();
        }
    }

    // 在回调中触发新事件
    void OnRecursiveFire(const TestRecursiveEvent& e) {
        current_depth = e.depth;
        if (e.depth < max_depth && bus_ref) {
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
        bus_ = z3y::GetDefaultService<IEventBus>();

        sender1_ = std::make_shared<MockSender>();
        sender2_ = std::make_shared<MockSender>();
        receiver_ = std::make_shared<MockReceiver>();
        reentrancy_helper_ = std::make_shared<ReentrancyHelper>();

        sender1_->Initialize();
        sender2_->Initialize();
        receiver_->Initialize();
        reentrancy_helper_->Initialize();
    }

    // 辅助：忙等待异步任务
    void WaitForAsync(int timeout_ms = 100) {
        int elapsed = 0;
        while (elapsed < timeout_ms) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            elapsed++;
        }
    }

    PluginPtr<IEventBus> bus_;
    std::shared_ptr<MockSender> sender1_;
    std::shared_ptr<MockSender> sender2_;
    std::shared_ptr<MockReceiver> receiver_;
    std::shared_ptr<ReentrancyHelper> reentrancy_helper_;
};

// =============================================================================
// 测试用例
// =============================================================================

TEST_F(EventSystemTest, GlobalBroadcast_Direct) {
    // 测试同步广播
    z3y::ScopedConnection conn = bus_->SubscribeGlobal<TestPayloadEvent>(
        receiver_, &MockReceiver::OnEvent, ConnectionType::kDirect
    );

    bus_->FireGlobal<TestPayloadEvent>(100, "Hello");

    EXPECT_EQ(receiver_->received_count, 1);
    // 验证：直接调用应在当前线程执行
    EXPECT_EQ(receiver_->last_thread_id, std::this_thread::get_id());
}

TEST_F(EventSystemTest, GlobalBroadcast_Queued) {
    // 测试异步广播
    z3y::ScopedConnection conn = bus_->SubscribeGlobal<TestPayloadEvent>(
        receiver_, &MockReceiver::OnEvent, ConnectionType::kQueued
    );

    bus_->FireGlobal<TestPayloadEvent>(200, "Async");

    // 等待异步执行
    int retries = 100;
    while (receiver_->received_count == 0 && retries-- > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    EXPECT_EQ(receiver_->received_count, 1);
    // 验证：异步调用应在不同线程执行
    EXPECT_NE(receiver_->last_thread_id, std::this_thread::get_id());
}

TEST_F(EventSystemTest, SenderSpecific_Isolation) {
    // 测试路由隔离：只听 sender1 的
    z3y::ScopedConnection conn = bus_->SubscribeToSender<TestPayloadEvent>(
        sender1_, receiver_, &MockReceiver::OnEvent
    );

    bus_->FireToSender<TestPayloadEvent>(sender2_, 999, "Ignore me");
    EXPECT_EQ(receiver_->received_count, 0); // sender2 被忽略

    bus_->FireToSender<TestPayloadEvent>(sender1_, 100, "Accept me");
    EXPECT_EQ(receiver_->received_count, 1); // sender1 被接收

    bus_->FireGlobal<TestPayloadEvent>(888, "Global");
    EXPECT_EQ(receiver_->received_count, 1); // 混合模式下，全局事件可能也会收到（取决于实现策略，这里假设不干扰）
    // 注意：如果 sender 订阅没有显式屏蔽全局，通常只会收到定向的。
    // 实际上这里的测试表明 SubscribeToSender 只接收 FireToSender(sender1)。
    // FireGlobal 不会触发 Sender 订阅。
}

TEST_F(EventSystemTest, Connection_Lifecycle) {
    // 1. 手动 Disconnect
    {
        z3y::Connection raw_conn = bus_->SubscribeGlobal<TestSignalEvent>(
            receiver_, &MockReceiver::OnSignal
        );
        bus_->FireGlobal<TestSignalEvent>();
        EXPECT_EQ(receiver_->received_count, 1);

        raw_conn.Disconnect(); // 手动断开
        bus_->FireGlobal<TestSignalEvent>();
        EXPECT_EQ(receiver_->received_count, 1); // 不应再收到
    }

    // 2. RAII ScopedConnection 自动断开
    receiver_->received_count = 0;
    {
        z3y::ScopedConnection scoped_conn = bus_->SubscribeGlobal<TestSignalEvent>(
            receiver_, &MockReceiver::OnSignal
        );
        bus_->FireGlobal<TestSignalEvent>();
        EXPECT_EQ(receiver_->received_count, 1);
    } // 离开作用域，自动断开

    bus_->FireGlobal<TestSignalEvent>();
    EXPECT_EQ(receiver_->received_count, 1);
}

TEST_F(EventSystemTest, Debug_ExceptionHandling) {
    std::string caught_error_msg;
    bool exception_caught = false;

    // 注册异常处理器
    manager_->SetExceptionHandler([&](const std::exception& e) {
        caught_error_msg = e.what();
        exception_caught = true;
        });

    {
        z3y::ScopedConnection conn = bus_->SubscribeGlobal<TestPayloadEvent>(
            receiver_, &MockReceiver::OnThrowingEvent, ConnectionType::kQueued
        );

        bus_->FireGlobal<TestPayloadEvent>(2, "Async Throw");
        WaitForAsync();

        EXPECT_TRUE(exception_caught);
        EXPECT_EQ(caught_error_msg, "Intentional Test Exception");
    }
}

TEST_F(EventSystemTest, Debug_EventTracing) {
    struct TraceRecord {
        EventTracePoint point;
        EventId evt_id;
    };
    std::vector<TraceRecord> traces;

    // 注册追踪钩子
    manager_->SetEventTraceHook([&](EventTracePoint pt, EventId id, void*, const char*) {
        traces.push_back({ pt, id });
        });

    {
        z3y::ScopedConnection conn = bus_->SubscribeGlobal<TestPayloadEvent>(
            receiver_, &MockReceiver::OnEvent, ConnectionType::kDirect
        );
        bus_->FireGlobal<TestPayloadEvent>(1, "TraceMe");

        // 验证是否收到了 Fired 和 DirectCall 两个点
        bool fired_found = false;
        bool call_found = false;
        for (const auto& t : traces) {
            if (t.evt_id == TestPayloadEvent::kEventId) {
                if (t.point == EventTracePoint::kEventFired) fired_found = true;
                if (t.point == EventTracePoint::kDirectCallStart) call_found = true;
            }
        }
        EXPECT_TRUE(fired_found);
        EXPECT_TRUE(call_found);
    }
}

TEST_F(EventSystemTest, Reentrancy_UnsubscribeSelf) {
    reentrancy_helper_->call_count = 0;
    // 订阅并在回调中断开自己
    reentrancy_helper_->conn_to_break = bus_->SubscribeGlobal<TestSignalEvent>(
        reentrancy_helper_,
        &ReentrancyHelper::OnSignalDisconnectSelf,
        ConnectionType::kDirect
    );

    // 第一次触发：回调执行，断开连接
    EXPECT_NO_THROW(bus_->FireGlobal<TestSignalEvent>());
    EXPECT_EQ(reentrancy_helper_->call_count, 1);
    EXPECT_FALSE(reentrancy_helper_->conn_to_break.IsConnected());

    // 第二次触发：不应执行
    bus_->FireGlobal<TestSignalEvent>();
    EXPECT_EQ(reentrancy_helper_->call_count, 1);
}

TEST_F(EventSystemTest, Reentrancy_RecursiveFire) {
    // 测试回调中触发新事件（递归深度 5）
    reentrancy_helper_->max_depth = 5;
    reentrancy_helper_->current_depth = 0;
    reentrancy_helper_->bus_ref = bus_;

    z3y::ScopedConnection conn = bus_->SubscribeGlobal<TestRecursiveEvent>(
        reentrancy_helper_,
        &ReentrancyHelper::OnRecursiveFire,
        ConnectionType::kDirect
    );

    bus_->FireGlobal<TestRecursiveEvent>(1);

    EXPECT_EQ(reentrancy_helper_->current_depth, reentrancy_helper_->max_depth);
}

TEST_F(EventSystemTest, UnsubscribeAll_ForSubscriber) {
    // 测试批量取消订阅
    (void)bus_->SubscribeGlobal<TestSignalEvent>(receiver_, &MockReceiver::OnSignal);
    (void)bus_->SubscribeToSender<TestPayloadEvent>(sender1_, receiver_, &MockReceiver::OnEvent);

    EXPECT_TRUE(bus_->IsGlobalSubscribed(TestSignalEvent::kEventId));

    bus_->Unsubscribe(receiver_); // 全部取消

    bus_->FireGlobal<TestSignalEvent>();
    bus_->FireToSender<TestPayloadEvent>(sender1_, 1, "msg");
    EXPECT_EQ(receiver_->received_count, 0);
}