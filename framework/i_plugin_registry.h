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
 * @file i_plugin_registry.h
 * @brief 定义插件注册接口 IPluginRegistry。
 * @author Yue Liu
 * @date 2025-06-14
 * @copyright Copyright (c) 2025 Yue Liu
 *
 * @details
 * [受众：框架维护者]
 * 此文件定义了 `IPluginRegistry` 接口，
 * 这是插件 (DLL/SO) 在加载时与 `PluginManager`
 * 通信的 *唯一* 桥梁。
 *
 * [工作流程]
 * 1. 宿主调用 `manager->LoadPlugin(...)`。
 * 2. `PluginManager` 加载 DLL/SO 并查找 C 函数 `z3yPluginInit`。
 * 3. `PluginManager` 调用 `z3yPluginInit(this)`，将 *自身* (作为 `IPluginRegistry*`)
 * 传递给插件。
 * 4. 插件的 `z3yPluginInit` 函数（由 `Z3Y_DEFINE_PLUGIN_ENTRY` 宏生成）
 * 负责调用 `registry->RegisterComponent(...)` 来注册其所有组件和服务。
 *
 * [受众：插件开发者]
 * **警告：你永远不需要直接使用、包含或实现此文件中的任何内容。**
 *
 * 你的所有注册工作都应通过 `framework/z3y_define_impl.h` 中提供的
 * 宏自动完成：
 * - `Z3Y_AUTO_REGISTER_SERVICE(...)`
 * - `Z3Y_AUTO_REGISTER_COMPONENT(...)`
 * - `Z3Y_DEFINE_PLUGIN_ENTRY`
 */

#pragma once

#ifndef Z3Y_FRAMEWORK_I_PLUGIN_REGISTRY_H_
#define Z3Y_FRAMEWORK_I_PLUGIN_REGISTRY_H_

#include <functional>  // 用于 std::function
#include <string>      // 用于 std::string
#include <vector>      // 用于 std::vector
#include "framework/class_id.h"       // 依赖 ClassId
#include "framework/i_component.h"    // 依赖 PluginPtr, IComponent
#include "framework/i_plugin_query.h" // 依赖 InterfaceDetails

namespace z3y {

    // 定义平台相关的 DLL 导出/导入宏
#ifdef _WIN32
/**
 * @def Z3Y_PLUGIN_API
 * @brief 在 Windows 平台上标记函数为 `dllexport`。
 * @details [受众：框架维护者]
 * 这确保了宿主程序 (Host) 可以从 DLL 中找到 `z3yPluginInit` 入口点。
 */
#define Z3Y_PLUGIN_API __declspec(dllexport)
#else
/**
 * @def Z3Y_PLUGIN_API
 * @brief 在 POSIX 平台上标记函数为 'default' 可见性 (导出)。
 * @details [受众：框架维护者]
 * 这确保了宿主程序 (Host) 可以从 .so/.dylib 中找到 `z3yPluginInit`
 * 入口点。
 */
#define Z3Y_PLUGIN_API __attribute__((visibility("default")))
#endif

 /**
  * @typedef FactoryFunction
  * @brief 工厂函数类型。
  *
  * [受众：框架维护者]
  *
  * 这是一个类型擦除的 `std::function`，
  * 它封装了创建插件实例的 `std::make_shared` 调用。
  * 框架使用它来 *延迟* 创建插件实例（例如，在 `GetService` 首次被调用时）。
  *
  * @return 一个新创建的、基于 `PluginPtr<IComponent>`
  * 的插件实例。
  */
    using FactoryFunction = std::function<PluginPtr<IComponent>()>;

    /**
     * @class IPluginRegistry
     * @brief [框架内部] 插件注册器接口。
     *
     * [受众：插件开发者]
     * **警告：不要使用此接口。**
     * 这是一个内部接口，由 `PluginManager` 实现，
     * 并由 `Z3Y_DEFINE_PLUGIN_ENTRY` 宏在 `z3yPluginInit` 函数中自动调用。
     */
    class IPluginRegistry {
    public:
        virtual ~IPluginRegistry() = default;

        /**
         * @brief [框架内部] 向框架注册一个组件或服务。
         *
         * [受众：框架维护者]
         * 这是由 `z3y::RegisterComponent` 和 `z3y::RegisterService`
         * (在 `plugin_registration.h` 中) 调用的核心内部 API。
         *
         * @param[in] clsid 待注册的实现类 (例如 `LoggerService::kClsid`)。
         * @param[in] factory
         * 一个能创建该类实例的工厂函数（由 `Z3Y_AUTO_REGISTER_...` 宏自动生成）。
         * @param[in] is_singleton
         * - `true`: 注册为单例服务 (Service)。框架将只创建一个实例并缓存它。(通过
         * `GetService` 获取)。
         * - `false`: 注册为瞬态组件 (Component)。每次请求都会创建一个新实例。(通过
         * `CreateInstance` 获取)。
         * @param[in] alias
         * 组件的唯一别名字符串 (例如 "Demo.Logger.Default")。
         * 如果为空，则不注册别名。
         * @param[in] implemented_interfaces
         * 一个 `InterfaceDetails` 列表 (由
         * `PluginImpl::GetInterfaceDetails()` 自动提供)。
         * @param[in] is_default (默认为 `false`)
         * - `true`: 标记这个实现为它所实现的 *所有* 接口的“默认”实现。
         * (允许使用者通过 `GetDefaultService<IDemoLogger>()` 来获取它)。
         * - `false`: 不标记为默认。
         */
        virtual void RegisterComponent(
            ClassId clsid, FactoryFunction factory, bool is_singleton,
            const std::string& alias,
            std::vector<InterfaceDetails> implemented_interfaces,
            bool is_default = false) = 0;
    };

}  // namespace z3y

#endif  // Z3Y_FRAMEWORK_I_PLUGIN_REGISTRY_H_