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
  * @file test_type_system.cpp
  * @brief [集成测试] 类型系统、接口继承与版本控制测试
  * * @details
  * 验证框架的动态类型转换能力 (`PluginCast`) 及其安全性：
  * 1. **接口继承**: 验证派生接口指针可以转换为基类接口指针。
  * 2. **类型安全**: 验证无法将实例转换为它未实现的接口。
  * 3. **版本控制**: 验证请求不兼容的高版本接口时，框架会拒绝转换。
  */

#include "common/plugin_test_base.h"
#include "framework/z3y_define_impl.h"
#include "interfaces_demo/i_demo_logger.h"
#include "interfaces_demo/i_advanced_demo_logger.h" // 继承自 IDemoLogger
#include "interfaces_demo/i_demo_simple.h"

using namespace z3y;
using namespace z3y::demo;

class TypeSystemTest : public PluginTestBase {
protected:
    void SetUp() override {
        PluginTestBase::SetUp();
        ASSERT_TRUE(LoadPlugin("plugin_demo_core_services"));
    }
};

// [内部 Mock] 定义一个 IDemoSimple 的 V2 版本接口
// IID 相同，但 Major Version = 2 (原版为 1)
class IDemoSimpleV2 : public virtual z3y::IComponent {
public:
    // 使用与 IDemoSimple 相同的 IID
    Z3Y_DEFINE_INTERFACE(IDemoSimpleV2, "z3y-demo-IDemoSimple-IID-A4736128", 2, 0);
    virtual void NewFunction() = 0;
};

/**
 * @test 接口继承转换 (Downcast / Crosscast)
 * @brief 验证 IAdvancedDemoLogger (派生) 可以被视为 IDemoLogger (基类)。
 */
TEST_F(TypeSystemTest, InterfaceInheritance) {
    // 1. 获取派生接口实例
    auto advanced = z3y::GetService<IAdvancedDemoLogger>("Demo.Logger.Advanced");
    ASSERT_NE(advanced, nullptr);

    // 2. 尝试转换为基类接口
    InstanceError err;
    auto base = z3y::PluginCast<IDemoLogger>(advanced, err);

    // 验证：转换成功
    EXPECT_EQ(err, InstanceError::kSuccess);
    ASSERT_NE(base, nullptr);

    // 验证：底层指针指向同一对象 (在多重继承中可能会有偏移，但 shared_ptr 会处理)
    // 这里我们只验证功能可用性
    // base->Log("Testing base interface call from derived instance");
}

/**
 * @test 非法类型转换防御
 * @brief 验证将对象转换为它未实现的接口时，返回 kErrorInterfaceNotImpl。
 */
TEST_F(TypeSystemTest, InvalidCast_InterfaceNotImpl) {
    // 获取一个 Logger 实例
    auto logger = z3y::GetDefaultService<IDemoLogger>();
    ASSERT_NE(logger, nullptr);

    // 尝试将其转换为 IDemoSimple (Logger 显然没有实现 Simple 接口)
    InstanceError err;
    auto simple = z3y::PluginCast<IDemoSimple>(logger, err);

    // 验证：转换失败，返回空指针和正确错误码
    EXPECT_EQ(simple, nullptr);
    EXPECT_EQ(err, InstanceError::kErrorInterfaceNotImpl);
}

/**
 * @test 版本不匹配防御 (Version Mismatch)
 * @brief 验证当宿主请求 V2 接口，而插件只提供 V1 实现时，转换失败。
 */
TEST_F(TypeSystemTest, VersionMismatch_Major) {
    // "Demo.Simple.A" 实现了 IDemoSimple (V1.0)
    // 我们尝试以 IDemoSimpleV2 (V2.0) 的身份去创建它

    // CreateInstance 内部会先创建对象，然后尝试 QueryInterface(V2)
    try {
        auto v2_ptr = z3y::CreateInstance<IDemoSimpleV2>("Demo.Simple.A");
        FAIL() << "CreateInstance 应该抛出异常，因为版本不匹配";
    } catch (const z3y::PluginException& e) {
        // 验证：错误码必须是 kErrorVersionMajorMismatch
        EXPECT_EQ(e.GetError(), InstanceError::kErrorVersionMajorMismatch)
            << "期望捕获主版本不匹配错误";
    }
}