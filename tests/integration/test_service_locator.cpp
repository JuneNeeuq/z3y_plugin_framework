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
  * @file test_service_locator.cpp
  * @brief [集成测试] 服务定位器 (Service Locator) 与错误处理机制测试
  * * @details
  * 本测试文件主要验证以下核心机制：
  * 1. **单例服务 (Service)**: 验证 GetService 返回全局唯一实例。
  * 2. **瞬态组件 (Component)**: 验证 CreateInstance 每次返回新实例。
  * 3. **异常处理 (Exception)**: 验证 Get... 系列 API 在失败时抛出正确的 PluginException。
  * 4. **无异常 API (Try...)**: 验证 TryGet... 系列 API 在失败时返回正确的错误码而不抛出异常。
  * 5. **类型约束**: 验证 Service 和 Component 不能混用（类型安全）。
  */

#include "common/plugin_test_base.h"
#include "framework/z3y_define_impl.h"
  // 引入测试所需的 Demo 接口
#include "interfaces_demo/i_demo_logger.h" 
#include "interfaces_demo/i_demo_simple.h"

using namespace z3y;
using namespace z3y::demo;

class ServiceLocatorTest : public PluginTestBase {
protected:
    void SetUp() override {
        PluginTestBase::SetUp();
        // 加载 Demo 核心插件，它提供了 Logger (Service) 和 Simple (Component) 的实现
        ASSERT_TRUE(LoadPlugin("plugin_demo_core_services"));
    }
};

/**
 * @test 单例服务的一致性验证
 * @brief 验证 GetService 无论调用多少次，返回的都是同一个 C++ 对象地址。
 */
TEST_F(ServiceLocatorTest, SingletonConsistency) {
    // 1. 通过默认接口获取
    auto ptr1 = z3y::GetDefaultService<IDemoLogger>();
    ASSERT_NE(ptr1, nullptr);

    // 2. 通过别名获取
    auto ptr2 = z3y::GetService<IDemoLogger>("Demo.Logger.Default");
    ASSERT_NE(ptr2, nullptr);

    // 3. 通过 ClassId 获取
    // (需要包含 demo_logger_service.h 才能访问 kClsid，这里简化为对比前两个)

    // 验证：两个指针指向同一个内存地址
    EXPECT_EQ(ptr1.get(), ptr2.get()) << "GetService 必须返回单例实例";
}

/**
 * @test 瞬态组件的独立性验证
 * @brief 验证 CreateInstance 每次调用都会创建一个新的 C++ 对象。
 */
TEST_F(ServiceLocatorTest, TransientIndependence) {
    // 1. 创建第一个实例
    auto ptr1 = z3y::CreateInstance<IDemoSimple>("Demo.Simple.A");
    ASSERT_NE(ptr1, nullptr);

    // 2. 创建第二个实例
    auto ptr2 = z3y::CreateInstance<IDemoSimple>("Demo.Simple.A");
    ASSERT_NE(ptr2, nullptr);

    // 验证：两个指针指向不同的内存地址
    EXPECT_NE(ptr1.get(), ptr2.get()) << "CreateInstance 必须每次创建新实例";
}

/**
 * @test 错误码：别名不存在
 * @brief 验证请求不存在的别名时，返回 kErrorAliasNotFound。
 */
TEST_F(ServiceLocatorTest, Error_AliasNotFound) {
    // 使用 Try... API (不抛出异常)
    auto [ptr, err] = z3y::TryGetService<IDemoLogger>("Non.Existent.Service");

    EXPECT_EQ(ptr, nullptr);
    EXPECT_EQ(err, InstanceError::kErrorAliasNotFound);
}

/**
 * @test 错误码：类型错配 (组件当服务用)
 * @brief 验证试图用 GetService 获取一个注册为 Component 的类时，框架是否拦截。
 */
TEST_F(ServiceLocatorTest, Error_ComponentAsService) {
    // "Demo.Simple.A" 是一个 Component (瞬态)
    // 错误用法：调用 TryGetService
    auto [ptr, err] = z3y::TryGetService<IDemoSimple>("Demo.Simple.A");

    EXPECT_EQ(ptr, nullptr);
    EXPECT_EQ(err, InstanceError::kErrorNotAService)
        << "框架应禁止通过 GetService 获取 Component";
}

/**
 * @test 错误码：类型错配 (服务当组件用)
 * @brief 验证试图用 CreateInstance 创建一个注册为 Service 的类时，框架是否拦截。
 */
TEST_F(ServiceLocatorTest, Error_ServiceAsComponent) {
    // "Demo.Logger.Default" 是一个 Service (单例)
    // 错误用法：调用 TryCreateInstance
    auto [ptr, err] = z3y::TryCreateInstance<IDemoLogger>("Demo.Logger.Default");

    EXPECT_EQ(ptr, nullptr);
    EXPECT_EQ(err, InstanceError::kErrorNotAComponent)
        << "框架应禁止通过 CreateInstance 创建 Service";
}

/**
 * @test 异常抛出 API 测试
 * @brief 验证 Get... API 在失败时是否正确抛出 PluginException。
 */
TEST_F(ServiceLocatorTest, ThrowingAPI_Behavior) {
    // 预期抛出异常
    EXPECT_THROW({
        z3y::GetService<IDemoLogger>("Bad.Alias");
        }, z3y::PluginException);

    // 捕获异常并验证错误码
    try {
        z3y::GetService<IDemoLogger>("Bad.Alias");
    } catch (const z3y::PluginException& e) {
        EXPECT_EQ(e.GetError(), InstanceError::kErrorAliasNotFound);
    }
}