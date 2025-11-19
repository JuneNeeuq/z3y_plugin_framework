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
 * @file plugin_cast.h
 * @brief 定义 z3y 框架的安全类型转换函数 `z3y::PluginCast`。
 * @author Yue Liu
 * @date 2025-06-28
 * @copyright Copyright (c) 2025 Yue Liu
 *
 * @details
 * [受众：插件开发者 和 框架使用者]
 *
 * `PluginCast` 是框架提供的 `dynamic_cast` 替代品。
 * 它封装了 `IComponent::QueryInterfaceRaw` 的复杂调用，
 * 提供了 C++ 风格的模板化接口。
 *
 * [设计思想：为什么不使用 `dynamic_cast`？]
 * 1. **跨 DLL 边界**：`dynamic_cast` 依赖 RTTI
 * 信息，跨 DLL 传递 RTTI 是不可靠的，
 * 尤其是在 C++ 运行时库不匹配时。
 * 2. **版本控制**：`dynamic_cast`
 * 无法处理版本。`PluginCast` 在转换时会强制执行
 * `QueryInterfaceRaw` 中内置的版本匹配逻辑（主版本必须匹配，次版本必须兼容）。
 *
 * [使用方法]
 * 推荐使用 `z3y::GetService` 或 `z3y::CreateInstance`，
 * 它们在内部自动调用 `PluginCast`。
 *
 * 仅当你有一个 `PluginPtr<IComponent>` 或 `PluginPtr<IBaseInterface>`，
 * 并希望将其转换为 `PluginPtr<IDerivedInterface>` 时，才需要手动调用它。
 */

#pragma once

#ifndef Z3Y_FRAMEWORK_PLUGIN_CAST_H_
#define Z3Y_FRAMEWORK_PLUGIN_CAST_H_

#include <memory>  // 用于 std::static_pointer_cast
#include "framework/class_id.h"         // 依赖 kIid
#include "framework/i_component.h"      // 依赖 IComponent, PluginPtr
#include "framework/plugin_exceptions.h"  // 依赖 InstanceError

namespace z3y {

    // --- [受众：框架维护者] 内部实现 ---
    namespace internal {
        /**
         * @brief [内部] `PluginCast` 的核心实现。
         *
         * @tparam T 要转换到的目标接口类型。
         * @param[in] component 来源 `IComponent` 智能指针。
         * @param[out] out_result 用于接收 `QueryInterfaceRaw` 的执行结果。
         * @return 成功则返回 `PluginPtr<T>`，失败则返回 `nullptr`。
         */
        template <typename T>
        PluginPtr<T> PluginCastImpl(PluginPtr<IComponent> component,
            InstanceError& out_result) {
            if (!component) {
                // 传入的指针为空
                out_result = InstanceError::kErrorInternal;
                return nullptr;
            }

            // 1. [核心] 调用 IComponent 的虚函数。
            // (T::kIid, T::kVersionMajor, T::kVersionMinor
            // 由 Z3Y_DEFINE_INTERFACE 宏提供)
            void* interface_ptr = component->QueryInterfaceRaw(
                T::kIid, T::kVersionMajor, T::kVersionMinor,
                out_result);

            if (!interface_ptr) {
                // 2. 查询失败 (out_result 已被 QueryInterfaceRaw 填充)
                return nullptr;
            }

            // 3. 查询成功 (out_result == kSuccess)
            // [C++核心：shared_ptr 别名构造函数]
            //
            // 创建一个新的 `shared_ptr` (`PluginPtr<T>`)，
            // 它指向 `interface_ptr` (类型为 `T*`)，
            // 但 *共享* `component` (`shared_ptr<IComponent>`)
            // 的控制块 (引用计数)。
            //
            // 这确保了：
            // a. 返回的指针是正确的 `T*` 类型。
            // b. 原始的 `IComponent`
            // 实例的生命周期被正确管理，不会提前析构。
            return PluginPtr<T>(component, static_cast<T*>(interface_ptr));
        }
    }  // namespace internal

    /**
     * @brief [核心 API]
     * 将 `PluginPtr<IComponent>` 安全地转换为 `PluginPtr<T>`。
     *
     * [受众：插件开发者 和 框架使用者]
     *
     * 这是从 `IComponent` 基类指针转换为具体接口指针的标准方法。
     * 它会执行接口实现检查和版本兼容性检查。
     *
     * @tparam T 目标接口类型 (例如 `IDemoLogger`)。
     * @param[in] component 来源 `IComponent` 智能指针。
     * @param[out] out_result 详细的转换结果 (例如 `kErrorInterfaceNotImpl`)。
     * @return 成功则返回 `PluginPtr<T>`，失败则返回 `nullptr`。
     *
     * @example
     * \code{.cpp}
     * PluginPtr<IComponent> my_component = ...;
     * InstanceError err;
     * PluginPtr<IDemoLogger> logger = z3y::PluginCast<IDemoLogger>(my_component, err);
     * if (err == InstanceError::kSuccess) {
     * logger->Log("Success!");
     * }
     * \endcode
     */
    template <typename T>
    PluginPtr<T> PluginCast(PluginPtr<IComponent> component,
        InstanceError& out_result) {
        // [受众：框架维护者]
        // `if constexpr` 用于处理 `PluginCast<IComponent>`
        // 这种无意义但合法的转换
        if constexpr (std::is_same_v<T, IComponent>) {
            return internal::PluginCastImpl<T>(component, out_result);
        }
        else {
            return internal::PluginCastImpl<T>(component, out_result);
        }
    }

    /**
     * @brief [核心 API]
     * 将 `PluginPtr<U>` 安全地转换为 `PluginPtr<T>` (接口到接口)。
     *
     * [受众：插件开发者 和 框架使用者]
     *
     * 这是一个便捷的重载，允许从一个已知的接口 (如 `IDemoLogger`)
     * 转换为另一个接口 (如 `IAdvancedDemoLogger`)，
     * 而无需先转回 `IComponent`。
     *
     * @tparam T 目标接口类型 (例如 `IAdvancedDemoLogger`)。
     * @tparam U 来源接口类型 (例如 `IDemoLogger`)。
     * @param[in] component_interface 来源接口的智能指针。
     * @param[out] out_result 详细的转换结果。
     * @return 成功则返回 `PluginPtr<T>`，失败则返回 `nullptr`。
     */
    template <typename T, typename U>
    PluginPtr<T> PluginCast(PluginPtr<U> component_interface,
        InstanceError& out_result) {
        // [受众：框架维护者]
        // 1. 先安全地 `static_pointer_cast` 回 IComponent
        PluginPtr<IComponent> base_component =
            std::static_pointer_cast<IComponent>(component_interface);

        // 2. 再调用 IComponent -> T 的标准转换
        return PluginCast<T>(base_component, out_result);
    }

}  // namespace z3y

#endif  // Z3Y_FRAMEWORK_PLUGIN_CAST_H_