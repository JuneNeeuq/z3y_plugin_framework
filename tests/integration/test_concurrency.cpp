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
  * @file test_concurrency.cpp
  * @brief [集成测试] 核心框架并发安全性测试
  * * @details
  * 重点验证 **单例服务初始化 (Singleton Initialization)** 的线程安全性。
  * * 场景：
  * 多个线程同时调用 `GetDefaultService<IDemoLogger>()`。
  * 预期：
  * 1. `Initialize()` 只能被执行一次。
  * 2. 所有线程必须拿到同一个对象指针。
  * 3. 不发生死锁或 Race Condition。
  */

#include "common/plugin_test_base.h"
#include "framework/z3y_define_impl.h"
#include "interfaces_demo/i_demo_logger.h"
#include <thread>
#include <vector>
#include <set>

using namespace z3y;
using namespace z3y::demo;

class ConcurrencyTest : public PluginTestBase {
protected:
    void SetUp() override {
        PluginTestBase::SetUp();
        ASSERT_TRUE(LoadPlugin("plugin_demo_core_services"));
    }
};

/**
 * @test 多线程并发获取单例服务
 * @brief 启动 20 个线程同时请求同一个服务，验证其单例性质和初始化安全性。
 */
TEST_F(ConcurrencyTest, ConcurrentGetService) {
    const int kThreadCount = 20;
    std::vector<std::thread> threads;
    std::vector<PluginPtr<IDemoLogger>> results(kThreadCount);
    std::atomic<int> exception_count{ 0 };

    // 1. 启动多个线程
    for (int i = 0; i < kThreadCount; ++i) {
        threads.emplace_back([&, i]() {
            try {
                // 高并发调用点
                results[i] = z3y::GetDefaultService<IDemoLogger>();
            } catch (...) {
                exception_count++;
            }
            });
    }

    // 2. 等待所有线程完成
    for (auto& t : threads) {
        if (t.joinable()) t.join();
    }

    // 3. 验证结果
    EXPECT_EQ(exception_count, 0) << "并发获取服务不应抛出异常";

    // 收集所有获取到的指针地址
    std::set<IDemoLogger*> pointers;
    for (const auto& ptr : results) {
        ASSERT_NE(ptr, nullptr);
        pointers.insert(ptr.get());
    }

    // 关键断言：集合中应该只有一个地址
    EXPECT_EQ(pointers.size(), 1) << "所有线程必须获取到同一个单例实例";
}

/**
 * @test 并发创建瞬态组件
 * @brief 验证 CreateInstance 在多线程下的稳定性（虽然它们是独立的，但共享 Registry 读锁）。
 */
TEST_F(ConcurrencyTest, ConcurrentCreateInstance) {
    const int kThreadCount = 20;
    std::vector<std::thread> threads;
    std::atomic<int> success_count{ 0 };

    for (int i = 0; i < kThreadCount; ++i) {
        threads.emplace_back([&]() {
            try {
                auto ptr = z3y::CreateInstance<z3y::IComponent>("Demo.Simple.A"); // 使用基类接收以简化
                if (ptr) success_count++;
            } catch (...) {}
            });
    }

    for (auto& t : threads) t.join();

    EXPECT_EQ(success_count, kThreadCount) << "所有并发创建请求都应成功";
}