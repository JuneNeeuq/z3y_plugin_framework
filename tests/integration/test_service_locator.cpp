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
 * 6. **手动注册 (Manual Registration)**: [新增] 验证在运行时手动注册本地类的能力。
 * 7. **依赖链 (Dependency Chain)**: [新增] 验证服务 A 依赖 B，B 依赖 C 的递归初始化能力。
 */

#include "common/plugin_test_base.h"
#include "framework/z3y_define_impl.h"
 // 引入测试所需的 Demo 接口
#include "interfaces_demo/i_demo_logger.h" 
#include "interfaces_demo/i_demo_simple.h"

using namespace z3y;
using namespace z3y::demo;

// =============================================================================
// [Mock Objects] 定义用于测试手动注册和依赖链的本地类
// =============================================================================

// 接口定义
class IChainServiceC : public virtual z3y::IComponent {
public:
    Z3Y_DEFINE_INTERFACE(IChainServiceC, "z3y-test-chain-C-IID", 1, 0);
    virtual int GetValue() = 0;
};

class IChainServiceB : public virtual z3y::IComponent {
public:
    Z3Y_DEFINE_INTERFACE(IChainServiceB, "z3y-test-chain-B-IID", 1, 0);
    virtual int GetValue() = 0;
};

class IChainServiceA : public virtual z3y::IComponent {
public:
    Z3Y_DEFINE_INTERFACE(IChainServiceA, "z3y-test-chain-A-IID", 1, 0);
    virtual int GetTotalValue() = 0;
};

// 实现定义
class ChainServiceC : public z3y::PluginImpl<ChainServiceC, IChainServiceC> {
public:
    Z3Y_DEFINE_COMPONENT_ID("z3y-test-chain-C-IMPL");
    int GetValue() override { return 10; }
};

class ChainServiceB : public z3y::PluginImpl<ChainServiceB, IChainServiceB> {
public:
    Z3Y_DEFINE_COMPONENT_ID("z3y-test-chain-B-IMPL");

    void Initialize() override {
        // B 依赖 C
        c_ = z3y::GetDefaultService<IChainServiceC>();
    }

    int GetValue() override { return c_->GetValue() * 2; } // 10 * 2 = 20
private:
    z3y::PluginPtr<IChainServiceC> c_;
};

class ChainServiceA : public z3y::PluginImpl<ChainServiceA, IChainServiceA> {
public:
    Z3Y_DEFINE_COMPONENT_ID("z3y-test-chain-A-IMPL");

    void Initialize() override {
        // A 依赖 B
        b_ = z3y::GetDefaultService<IChainServiceB>();
    }

    int GetTotalValue() override { return b_->GetValue() + 5; } // 20 + 5 = 25
private:
    z3y::PluginPtr<IChainServiceB> b_;
};

// =============================================================================
// [Test Fixture] 测试夹具
// =============================================================================

class ServiceLocatorTest : public PluginTestBase {
protected:
    void SetUp() override {
        PluginTestBase::SetUp();
        // 加载 Demo 核心插件，它提供了 Logger (Service) 和 Simple (Component) 的实现
        ASSERT_TRUE(LoadPlugin("plugin_demo_core_services"));
    }
};

// =============================================================================
// 1. 基础功能测试 (Singleton / Transient)
// =============================================================================

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

// =============================================================================
// 2. 错误处理测试 (Error Handling)
// =============================================================================

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
    // [Fix C4834] 显式忽略返回值，因为 EXPECT_THROW 宏内部可能展开为导致警告的代码
    EXPECT_THROW({
        (void)z3y::GetService<IDemoLogger>("Bad.Alias");
        }, z3y::PluginException);

    // 捕获异常并验证错误码
    try {
        (void)z3y::GetService<IDemoLogger>("Bad.Alias");
    } catch (const z3y::PluginException& e) {
        EXPECT_EQ(e.GetError(), InstanceError::kErrorAliasNotFound);
    }
}

// =============================================================================
// 3. 依赖链与手动注册测试 (Dependency Chain & Manual Registration) - [新增]
// =============================================================================

/**
 * @test 依赖链初始化
 * @brief 验证框架能正确处理 A->B->C 的递归初始化依赖。
 * @details
 * 1. 在测试中手动注册 A, B, C 三个服务。
 * 2. 请求获取服务 A。
 * 3. 框架应自动初始化 A，A 初始化时请求 B，B 初始化时请求 C。
 * 4. 最终所有服务就绪，并返回正确结果。
 */
TEST_F(ServiceLocatorTest, DependencyChain_RecursiveInit) {
    // 1. 手动注册本地定义的 Mock 服务
    // 注意：IPluginRegistry 是 PluginManager 的基类接口
    auto registry = dynamic_cast<z3y::IPluginRegistry*>(manager_.get());
    ASSERT_NE(registry, nullptr);

    // 使用框架提供的模板辅助函数进行注册
    // 注册 C (最底层)
    z3y::RegisterService<ChainServiceC>(registry, "Chain.C", true);
    // 注册 B (依赖 C)
    z3y::RegisterService<ChainServiceB>(registry, "Chain.B", true);
    // 注册 A (依赖 B)
    z3y::RegisterService<ChainServiceA>(registry, "Chain.A", true);

    // 2. 请求最顶层服务 A
    // 这会触发一连串的递归 Initialize() 调用
    PluginPtr<IChainServiceA> service_a;
    EXPECT_NO_THROW({
        service_a = z3y::GetDefaultService<IChainServiceA>();
        });
    ASSERT_NE(service_a, nullptr);

    // 3. 验证功能
    // Value = (10 * 2) + 5 = 25
    EXPECT_EQ(service_a->GetTotalValue(), 25);
}

/**
 * @test 手动注册瞬态组件
 * @brief 验证在测试代码中手动注册 Component 并实例化的能力。
 */
TEST_F(ServiceLocatorTest, ManualRegistration_Component) {
    auto registry = dynamic_cast<z3y::IPluginRegistry*>(manager_.get());
    ASSERT_NE(registry, nullptr);

    // 注册一个瞬态组件 (复用 ChainServiceC 的实现，但注册为 Component)
    z3y::RegisterComponent<ChainServiceC>(registry, "Manual.Component.C", false);

    // 创建实例
    auto ptr1 = z3y::CreateInstance<IChainServiceC>("Manual.Component.C");
    auto ptr2 = z3y::CreateInstance<IChainServiceC>("Manual.Component.C");

    ASSERT_NE(ptr1, nullptr);
    ASSERT_NE(ptr2, nullptr);
    EXPECT_NE(ptr1.get(), ptr2.get()) << "手动注册的组件应每次返回新实例";
    EXPECT_EQ(ptr1->GetValue(), 10);
}