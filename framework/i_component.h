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
 * @file i_component.h
 * @brief 定义 z3y 插件框架的核心基类 IComponent 和智能指针 PluginPtr。
 * @author Yue Liu
 * @date 2025-06-07
 * @copyright Copyright (c) 2025 Yue Liu
 *
 * @details
 * [受众：所有人]
 *
 * 此文件定义了 z3y 插件框架的 *对象模型基础*。
 * 1. `z3y::PluginPtr`：用于管理所有插件对象生命周期的官方智能指针。
 * 2. `z3y::IComponent`：所有插件接口和实现 *必须* 继承的绝对基类。
 */

#pragma once

#ifndef Z3Y_FRAMEWORK_I_COMPONENT_H_
#define Z3Y_FRAMEWORK_I_COMPONENT_H_

#include <memory>       // 用于 std::shared_ptr
#include <typeindex>    // (保留, 用于未来可能的 RTTI 扩展)
#include "framework/class_id.h"          // 依赖 ClassId, InterfaceId
#include "framework/interface_helpers.h" // 依赖 Z3Y_DEFINE_INTERFACE
#include "framework/plugin_exceptions.h" // 依赖 InstanceError

namespace z3y {

    /**
     * @brief 插件框架的官方智能指针类型。
     *
     * [受众：所有人]
     *
     * `PluginPtr` 是 `std::shared_ptr` 的别名。
     * 框架内的 *所有* 插件对象都必须通过此智能指针进行管理。
     *
     * [设计思想：框架维护者]
     * 使用 `std::shared_ptr` 是实现跨 DLL (动态链接库)
     * 边界安全管理 C++ 对象生命周期的关键。
     * 它确保了即使宿主和插件使用不同的 C++
     * 运行时堆，对象的析构函数也会在正确的模块（分配它的模块）中被调用。
     */
    template <typename T>
    using PluginPtr = std::shared_ptr<T>;

    /**
     * @class IComponent
     * @brief 框架中所有可查询组件的绝对基类。
     *
     * [受众：插件开发者]
     * 1. **定义接口**：你定义的 *所有* 接口 (例如 `IDemoLogger`) 都必须 `public virtual`
     * 继承自 `IComponent`。
     * 2. **实现接口**：你不应 *直接* 继承 `IComponent`，而应使用 `z3y::PluginImpl`
     * 模板基类，它会为你处理所有复杂的实现细节。
     *
     * [受众：框架维护者]
     * `IComponent` 类似于 `IUnknown` (COM) 或 `Object` (Java/C#)。
     * 它定义了框架的"类型发现" (`QueryInterfaceRaw`) 和"生命周期"
     * (`Initialize`, `Shutdown`) 钩子。
     */
    class IComponent {
    public:
        //! [受众：插件开发者]
        //! 定义 IComponent 自身的基础接口元数据。
        Z3Y_DEFINE_INTERFACE(IComponent, "z3y-core-IComponent-IID-A0000001", 1, 0);

        /**
        * @brief 虚拟析构函数。
        * [受众：框架维护者]
        * 确保通过 `PluginPtr<IComponent>` 基类指针销毁派生类时行为正确。
        */
        virtual ~IComponent() = default;

        /**
         * @brief [框架核心] 查询组件是否实现了指定的接口 (底层)。
         *
         * [受众：插件开发者 和 框架使用者]
         * **警告：永远不要手动调用此函数。**
         * 这是一个底层的、不安全的虚函数。
         * - 若要实现此函数，请使用 `z3y::PluginImpl` 基类。
         * - 若要调用此功能，请使用 `z3y::PluginCast`、`z3y::GetService` 或
         * `z3y::CreateInstance`。
         *
         * [受众：框架维护者]
         * 这是 `z3y::PluginCast` 的核心，是框架的 `dynamic_cast`
         * 替代品。
         * 它必须检查 `iid` (接口ID) 和版本 (major/minor) 的兼容性。
         *
         * @param[in] iid     正在查询的接口的唯一ID (InterfaceId)。
         * @param[in] major   调用者期望的接口主版本号。
         * @param[in] minor   调用者期望的接口次版本号。
         * @param[out] out_result 用于返回详细的错误代码 (例如 `kErrorVersionMajorMismatch`)。
         *
         * @return 成功 (接口已实现且版本兼容) 时，返回指向该接口的 `void*` 指针。
         * @return 失败时，返回 `nullptr`，并通过 `out_result` 返回失败原因。
         */
        virtual void* QueryInterfaceRaw(InterfaceId iid, uint32_t major,
            uint32_t minor, InstanceError& out_result) = 0;

        /**
         * @brief [生命周期钩子] 实例初始化。
         *
         * [受众：插件开发者 (实现插件)]
         *
         * 在插件实例被创建 (构造函数调用) 之后，
         * 并且在 `GetService` / `CreateInstance` 返回给调用者之前，
         * PluginManager 会调用此方法。
         *
         * [使用时机]
         * 这是执行需要 `PluginPtr` (即 `shared_from_this()`) 的初始化的安全位置，
         * 例如**订阅事件**。
         *
         * [禁忌]
         * 1. **不要**在 *构造函数* 中订阅事件，因为 `shared_from_this()` 此时不可用。
         * 2. **不要**在此函数中调用 `z3y::GetService` 或
         * `z3y::CreateInstance` 来获取 *其他* 插件。
         * 这可能导致循环依赖和启动死锁。依赖项应在首次使用时（例如在 `RunTest`
         * 或 `Log` 方法中）进行“懒加载”。
         */
        virtual void Initialize() {}

        /**
         * @brief [生命周期钩子] 实例销毁前清理。
         *
         * [受众：插件开发者 (实现插件)]
         *
         * 在 `PluginManager::UnloadAllPlugins()` 期间，
         * 在插件实例被销毁 (析构函数调用) *之前*， PluginManager 会调用此方法。
         *
         * [使用时机]
         * 这是执行清理操作（例如记录日志、释放对其他服务的强引用）的安全位置。
         * PluginManager 保证以 LIFO (后进先出)
         * 的顺序调用 `Shutdown()`，确保依赖关系正确解除。
         *
         * [注意]
         * 在此函数中调用 `z3y::TryGetService` (noexcept API)
         * 是安全的（例如为了日志记录），
         * 但不应假定所有服务都仍然可用。
         */
        virtual void Shutdown() {}
    };

}  // namespace z3y

#endif  // Z3Y_FRAMEWORK_I_COMPONENT_H_