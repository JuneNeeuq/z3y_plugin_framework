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
 * @file plugin_impl.h
 * @brief 定义 CRTP 模板基类 z3y::PluginImpl。
 * @author Yue Liu
 * @date 2025-06-22
 * @copyright Copyright (c) 2025 Yue Liu
 *
 * @details
 * [受众：插件开发者 (实现插件)]
 *
 * 这是插件实现者 *最核心* 的工具之一。
 * 它使用“奇异递归模板模式”(CRTP)，为你的实现类
 * *自动生成* `IComponent::QueryInterfaceRaw` 和
 * `GetInterfaceDetails` 这两个最复杂的函数。
 *
 * [使用方法]
 * 你的实现类 (例如 `DemoLoggerService`) 必须：
 * 1. 继承 `z3y::PluginImpl`。
 * 2. 模板参数第一个传入 *类自身* (例如 `DemoLoggerService`)。
 * 3. 模板参数后续传入该类实现的 *所有接口* (例如 `IDemoLogger`)。
 *
 * [受众：框架维护者]
 * 此类利用 C++17 的 `if constexpr` 和模板元编程，
 * 遍历 `Interfaces...` 参数包，
 * 自动为 `QueryInterfaceRaw` 生成 IID 和版本检查的 `if` 链，
 * 并为 `GetInterfaceDetails` 自动收集所有接口的元数据。
 *
 * `static_assert`
 * 检查（例如 `CheckHasClsid`）用于在编译期向插件开发者提供清晰的错误信息，
 * 提醒他们使用 `Z3Y_DEFINE_COMPONENT_ID` 等宏。
 */

#pragma once

#ifndef Z3Y_FRAMEWORK_PLUGIN_IMPL_H_
#define Z3Y_FRAMEWORK_PLUGIN_IMPL_H_

#include <memory>         // 用于 std::enable_shared_from_this
#include <type_traits>    // 用于 SFINAE, std::is_base_of_v (C++17)
#include <vector>         // 用于 std::vector
#include "framework/class_id.h"       // 依赖 ClassId
#include "framework/i_component.h"    // 依赖 z3y::IComponent
#include "framework/i_plugin_query.h" // 依赖 InterfaceDetails
#include "framework/plugin_exceptions.h"  // 依赖 InstanceError

namespace z3y {

    /**
     * @class PluginImpl
     * @brief [插件开发者核心] CRTP 模板基类，用于自动实现 IComponent。
     *
     * @tparam ImplClass 派生类自身的类型 (CRTP) (例如 `DemoLoggerService`)。
     * @tparam Interfaces... 派生类所实现的 *所有* 接口列表 (例如 `IDemoLogger`)。
     *
     * @example
     * \code{.cpp}
     * // 1. MyClass 实现一个接口
     * class MyClass : public z3y::PluginImpl<MyClass, IMyInterface> {
     * public:
     * Z3Y_DEFINE_COMPONENT_ID(...)
     * // ... 实现 IMyInterface 的虚函数 ...
     * };
     *
     * // 2. AdvancedLogger 实现两个接口
     * // (假设 IAdvancedDemoLogger 继承自 IDemoLogger)
     * class AdvancedDemoLogger : public z3y::PluginImpl<AdvancedLogger,
     * IAdvancedDemoLogger, // [!!] 必须列出派生接口
     * IDemoLogger> {       // [!!] 也必须列出基接口
     * public:
     * Z3Y_DEFINE_COMPONENT_ID(...)
     * // ... 实现 IAdvancedDemoLogger 和 IDemoLogger 的虚函数 ...
     * };
     * \endcode
     *
     * [重要提示：接口继承]
     * 如上例 2 所示，如果一个类实现了 `IAdvancedDemoLogger`
     * (它继承自`IDemoLogger`)，
     * 那么 `PluginImpl` 的模板参数列表 *必须同时包含* `IAdvancedDemoLogger`
     * *和* `IDemoLogger`。
     * 这样，`QueryInterfaceRaw` 才能被自动地正确生成，
     * 以响应对 *两个* 接口的查询。
     */
    template <typename ImplClass, typename... Interfaces>
    class PluginImpl : public virtual IComponent,
        public std::enable_shared_from_this<ImplClass>,
        public virtual Interfaces... {
    public:
        // [受众：框架维护者]
        // kClsid 由 ImplClass 通过 Z3Y_DEFINE_COMPONENT_ID 提供
        // static constexpr ClassId kClsid = ImplClass::kClsid;

    private:
        // --- [受众：框架维护者] 编译期静态断言 (SFINAE 和 C++17 模板元编程) ---
        // 以下代码在编译期运行，用于向插件开发者提供清晰的错误信息，
        // 告诉他们忘记了哪个宏。

        /**
         * @brief [内部] C++17 SFINAE
         * 检查：类型 T 是否定义了 `kClsid` 成员。
         */
        template <typename T, typename = std::void_t<>>
        struct has_kClsid : std::false_type {};
        template <typename T>
        struct has_kClsid<T, std::void_t<decltype(T::kClsid)>> : std::true_type {};

        /**
         * @brief [内部] 编译期检查 `ImplClass` 是否正确使用了 `Z3Y_DEFINE_COMPONENT_ID`。
         */
        template <typename T = ImplClass>
        static constexpr bool CheckHasClsid() {
            static_assert(has_kClsid<T>::value,
                "ImplClass must define 'static constexpr z3y::ClassId "
                "kClsid'. (Hint: Use Z3Y_DEFINE_COMPONENT_ID macro inside "
                "your class)");
            if constexpr (has_kClsid<T>::value) {
                static_assert(
                    std::is_same_v<decltype(T::kClsid), const ClassId>,
                    "'kClsid' must be of type 'const z3y::ClassId'. (Hint: Use "
                    "Z3Y_DEFINE_COMPONENT_ID macro)");
            }
            return true;
        }

        /**
         * @brief [内部]
         * 编译期递归检查 `Interfaces...` 包中的所有接口
         * 是否正确使用了 `Z3Y_DEFINE_INTERFACE`。
         */
        template <typename First, typename... Rest>
        static constexpr bool AllDeriveFromIComponent() {
            // 1. 必须继承自 IComponent
            static_assert(std::is_base_of_v<IComponent, First>,
                "Template parameter pack 'Interfaces...' must all derive "
                "from z3y::IComponent.");
            // 2. 必须定义 kIid (Z3Y_DEFINE_INTERFACE)
            static_assert(
                std::is_same_v<decltype(First::kIid), const InterfaceId>,
                "Interface 'First' must define 'static constexpr "
                "z3y::InterfaceId kIid'. (Hint: Use Z3Y_DEFINE_INTERFACE)");
            // 3. 必须定义 kName (Z3Y_DEFINE_INTERFACE)
            static_assert(
                std::is_same_v<decltype(First::kName), const char* const>,
                "Interface 'First' must define 'static constexpr const "
                "char* kName'. (Hint: Use Z3Y_DEFINE_INTERFACE)");
            // 4. 必须定义 kVersionMajor (Z3Y_DEFINE_INTERFACE)
            static_assert(
                std::is_same_v<decltype(First::kVersionMajor), const uint32_t>,
                "Interface 'First' must define 'static constexpr const "
                "uint32_t kVersionMajor'. (Hint: Use Z3Y_DEFINE_INTERFACE)");
            // 5. 必须定义 kVersionMinor (Z3Y_DEFINE_INTERFACE)
            static_assert(
                std::is_same_v<decltype(First::kVersionMinor), const uint32_t>,
                "Interface 'First' must define 'static constexpr const "
                "uint32_t kVersionMinor'. (Hint: Use Z3Y_DEFINE_INTERFACE)");

            // 递归检查包的剩余部分
            if constexpr (sizeof...(Rest) > 0) {
                return AllDeriveFromIComponent<Rest...>();
            }
            return true;
        }

        /**
         * @brief [内部] `QueryInterfaceRaw` 的递归模板实现。
         *
         * [受众：框架维护者]
         * 这是模板元编程的核心。编译器会为 `Interfaces...` 包中的每个接口
         * 实例化一个 `if (iid == First::kIid)` 检查。
         *
         * @param[in] iid 正在查询的接口 ID。
         * @param[in] host_major 宿主期望的主版本。
         * @param[in] host_minor 宿主期望的次版本。
         * @param[out] out_result 详细错误码。
         * @return `void*` 指针或 `nullptr`。
         */
        template <typename First, typename... Rest>
        void* QueryRecursive(InterfaceId iid, uint32_t host_major,
            uint32_t host_minor, InstanceError& out_result) {
            // 1. 检查 IID 是否匹配当前接口
            if (iid == First::kIid) {
                const uint32_t my_major = First::kVersionMajor;
                const uint32_t my_minor = First::kVersionMinor;

                // 2. [版本检查] 检查主版本
                if (my_major != host_major) {
                    out_result = InstanceError::kErrorVersionMajorMismatch;
                    return nullptr;
                }

                // 3. [版本检查] 检查次版本
                //    (插件实现的版本 my_minor 必须 >= 宿主期望的版本 host_minor)
                if (my_minor < host_minor) {
                    out_result = InstanceError::kErrorVersionMinorTooLow;
                    return nullptr;
                }

                // 4. [成功] IID 和版本均兼容
                out_result = InstanceError::kSuccess;
                // 关键：将 `this` (类型为 `PluginImpl*`)
                // 1. static_cast 到 `ImplClass*` (派生类)
                // 2. 再 static_cast 到 `First*` (目标接口)
                // 这是安全的，因为 C++ 保证 `ImplClass` 继承自 `First`
                return static_cast<First*>(static_cast<ImplClass*>(this));
            }

            // 5. [递归] IID 不匹配，继续检查包中的下一个接口
            if constexpr (sizeof...(Rest) > 0) {
                return QueryRecursive<Rest...>(iid, host_major, host_minor, out_result);
            }

            // 6. [失败] 遍历完所有接口，未找到
            out_result = InstanceError::kErrorInterfaceNotImpl;
            return nullptr;
        }

        /**
         * @brief [内部] `GetInterfaceDetails` 的递归模板实现。
         */
        template <typename First, typename... Rest>
        static void CollectDetailsRecursive(
            std::vector<InterfaceDetails>& details) {
            // 收集当前接口的元数据
            details.push_back(InterfaceDetails{
                First::kIid,
                First::kName,
                InterfaceVersion{First::kVersionMajor, First::kVersionMinor} });

            // 递归收集包的剩余部分
            if constexpr (sizeof...(Rest) > 0) {
                CollectDetailsRecursive<Rest...>(details);
            }
        }

    public:
        /**
         * @brief [框架核心] 重写 IComponent::QueryInterfaceRaw。
         *
         * [受众：框架维护者]
         * 此函数由框架自动实现，它按以下顺序检查 IID：
         * 1. 是否是 `IComponent` 自身？
         * 2. (递归) 是否是 `Interfaces...` 包中的任意一个？
         */
        void* QueryInterfaceRaw(InterfaceId iid, uint32_t major, uint32_t minor,
            InstanceError& out_result) override {
            // [编译期] 触发对 ImplClass 和 Interfaces... 的静态断言检查
            [[maybe_unused]] constexpr bool check_clsid = CheckHasClsid();
            if constexpr (sizeof...(Interfaces) > 0) {
                [[maybe_unused]] constexpr bool check_iids =
                    AllDeriveFromIComponent<Interfaces...>();
            }

            // 1. 检查 IID 是否是 IComponent 本身
            if (iid == IComponent::kIid) {
                const uint32_t my_major = IComponent::kVersionMajor;
                const uint32_t my_minor = IComponent::kVersionMinor;

                if (my_major != major) {
                    out_result = InstanceError::kErrorVersionMajorMismatch;
                    return nullptr;
                }
                if (my_minor < minor) {
                    out_result = InstanceError::kErrorVersionMinorTooLow;
                    return nullptr;
                }

                out_result = InstanceError::kSuccess;
                // 转换到 IComponent 基类
                return static_cast<IComponent*>(static_cast<ImplClass*>(this));
            }

            // 2. [递归] 检查 Interfaces... 包
            if constexpr (sizeof...(Interfaces) > 0) {
                return QueryRecursive<Interfaces...>(iid, major, minor, out_result);
            }

            // 3. [失败] 未找到
            out_result = InstanceError::kErrorInterfaceNotImpl;
            return nullptr;
        }

        /**
         * @brief [框架核心] 重写 IComponent::Initialize()。
         *
         * [受众：插件开发者 (实现插件)]
         * 这提供了一个默认的空实现。
         * 你可以在你的 `ImplClass` (例如 `DemoLoggerService`)
         * 中重写此方法以执行初始化，
         * (请遵循 `IComponent::Initialize` 注释中的规则)。
         */
        void Initialize() override {
            // 默认实现为空。
            // 派生类可以重写此方法。
        }

        /**
         * @brief [框架核心] 重写 IComponent::Shutdown()。
         *
         * [受众：插件开发者 (实现插件)]
         * 这提供了一个默认的空实现。
         * 你可以在你的 `ImplClass` (例如 `DemoLoggerService`)
         * 中重写此方法以执行清理，
         * (请遵循 `IComponent::Shutdown` 注释中的规则)。
         */
        void Shutdown() override {
            // 默认实现为空。
            // 派生类可以重写此方法。
        }

        /**
         * @brief [框架核心] 获取此实现类所支持的所有接口的元数据。
         *
         * [受众：框架维护者]
         * 此静态函数由 `RegisterComponent`
         * (在 `auto_registration.h` 中) 自动调用，
         * 用于向 PluginManager 报告此组件实现了哪些接口。
         */
        static std::vector<InterfaceDetails> GetInterfaceDetails() {
            // [编译期] 再次触发静态断言
            [[maybe_unused]] constexpr bool check_clsid = CheckHasClsid();
            if constexpr (sizeof...(Interfaces) > 0) {
                [[maybe_unused]] constexpr bool check_iids =
                    AllDeriveFromIComponent<Interfaces...>();
            }

            std::vector<InterfaceDetails> details;

            // 1. 自动添加 IComponent 自身
            details.push_back(InterfaceDetails{
                IComponent::kIid,
                IComponent::kName,
                InterfaceVersion{IComponent::kVersionMajor, IComponent::kVersionMinor} });

            // 2. [递归] 自动收集 Interfaces... 包中的所有接口
            if constexpr (sizeof...(Interfaces) > 0) {
                CollectDetailsRecursive<Interfaces...>(details);
            }
            return details;
        }
    };

}  // namespace z3y

#endif  // Z3Y_FRAMEWORK_PLUGIN_IMPL_H_