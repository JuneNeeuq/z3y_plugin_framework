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
 * @file plugin_registration.h
 * @brief 定义 `RegisterComponent` 和 `RegisterService` 模板辅助函数。
 * @author Yue Liu
 * @date 2025-06-28
 * @copyright Copyright (c) 2025 Yue Liu
 *
 * @details
 * [受众：框架维护者]
 *
 * 此文件是 `Z3Y_AUTO_REGISTER_...` 宏的 *实现*。
 * 插件开发者 *不应* 直接包含或使用此文件。
 *
 * [设计思想]
 * `auto_registration.h` 中的宏负责创建 lambda，
 * 而此文件中的模板函数负责 *实现* 那个 lambda 的内容。
 *
 * 它们封装了调用 `IPluginRegistry::RegisterComponent` 所需的两个关键步骤：
 * 1. **工厂创建 (Factory)**: 自动创建一个 `std::make_shared<ImplClass>`
 * 的 lambda 函数 (即 `FactoryFunction`)。
 * 2. **接口详情 (Details)**: 自动调用 `ImplClass::GetInterfaceDetails()`
 * (该静态函数由 `PluginImpl` 基类提供) 来获取元数据。
 */

#pragma once

#ifndef Z3Y_FRAMEWORK_PLUGIN_REGISTRATION_H_
#define Z3Y_FRAMEWORK_PLUGIN_REGISTRATION_H_

#include <memory>    // 用于 std::make_shared
#include <string>    // 用于 std::string
#include <vector>    // 用于 std::vector
#include "framework/i_plugin_registry.h"  // 依赖 IPluginRegistry
#include "framework/plugin_impl.h"  // 依赖 PluginImpl (为了
 // ImplClass::GetInterfaceDetails)

namespace z3y {
    /**
     * @brief [框架内部] 注册一个瞬态组件 (Component) 的模板辅助函数。
     *
     * @tparam ImplClass 要注册的组件实现类 (例如 `DemoSimpleImplA`)。
     * @param[in] registry `z3yPluginInit` 提供的 `IPluginRegistry` 指针。
     * @param[in] alias 组件的可选别名 (例如 "DemoSimple.A")。
     * @param[in] is_default 是否将其标记为默认实现。
     */
    template <typename ImplClass>
    void RegisterComponent(IPluginRegistry* registry, const std::string& alias = "",
        bool is_default = false) {
        // 1. 自动创建工厂 lambda
        FactoryFunction factory = []() -> PluginPtr<IComponent> {
            return std::make_shared<ImplClass>();
            };

        // 2. 自动调用 ImplClass 的静态函数 (该函数由 PluginImpl 基类提供)
        registry->RegisterComponent(ImplClass::kClsid, std::move(factory),
            false,  // is_singleton = false
            alias, ImplClass::GetInterfaceDetails(),
            is_default);
    }

    /**
     * @brief [框架内部] 注册一个单例服务 (Service) 的模板辅助函数。
     *
     * @tparam ImplClass 要注册的服务实现类 (例如 `DemoLoggerService`)。
     * @param[in] registry `z3yPluginInit` 提供的 `IPluginRegistry` 指针。
     * @param[in] alias 服务的可选别名 (例如 "Demo.Logger.Default")。
     * @param[in] is_default 是否将其标记为默认实现。
     */
    template <typename ImplClass>
    void RegisterService(IPluginRegistry* registry, const std::string& alias = "",
        bool is_default = false) {
        // 1. 自动创建工厂 lambda
        FactoryFunction factory = []() -> PluginPtr<IComponent> {
            return std::make_shared<ImplClass>();
            };

        // 2. 自动调用 ImplClass 的静态函数
        registry->RegisterComponent(ImplClass::kClsid, std::move(factory),
            true,  // is_singleton = true
            alias, ImplClass::GetInterfaceDetails(),
            is_default);
    }

}  // namespace z3y

#endif  // Z3Y_FRAMEWORK_PLUGIN_REGISTRATION_H_