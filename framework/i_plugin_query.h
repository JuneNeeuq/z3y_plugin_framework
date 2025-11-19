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
 * @file i_plugin_query.h
 * @brief 定义 z3y 框架的内省 (Introspection) 接口 IPluginQuery。
 * @author Yue Liu
 * @date 2025-06-15
 * @copyright Copyright (c) 2025 Yue Liu
 *
 * @details
 * [受众：插件开发者 和 框架使用者]
 *
 * 此文件定义了 `IPluginQuery` 接口，
 * 这是一个核心的“只读”服务，
 * 允许你查询（内省）框架的当前状态。
 *
 * [使用方法]
 * 你可以通过 `z3y::GetService`（或 `TryGetService`）来获取此服务：
 * \code{.cpp}
 * auto query_service = z3y::GetService<IPluginQuery>(z3y::clsid::kPluginQuery);
 * auto details = query_service->FindComponentsImplementing(IDemoLogger::kIid);
 * \endcode
 *
 * [线程安全]
 * 此接口中的所有函数都是 `const`
 * 的，保证了它们是只读的，并且可以从多个线程安全地调用。
 */

#pragma once

#ifndef Z3Y_FRAMEWORK_I_PLUGIN_QUERY_H_
#define Z3Y_FRAMEWORK_I_PLUGIN_QUERY_H_

#include <string>   // 用于 std::string
#include <vector>   // 用于 std::vector
#include "framework/class_id.h"         // 依赖 ClassId, InterfaceId
#include "framework/i_component.h"      // 依赖 IComponent
#include "framework/interface_helpers.h"  // 依赖 Z3Y_DEFINE_INTERFACE

namespace z3y {

    /**
     * @brief `IPluginQuery` 服务的全局唯一 ClassId。
     *
     * [受众：插件开发者 和 框架使用者]
     * 在调用 `z3y::GetService<IPluginQuery>(...)` 时使用此 ID。
     */
    namespace clsid {
        constexpr ClassId kPluginQuery =
            ConstexprHash("z3y-core-plugin-query-SERVICE-UUID");
    }  // namespace clsid

    /**
     * @struct InterfaceVersion
     * @brief [数据结构] 描述一个接口的版本号。
     */
    struct InterfaceVersion {
        uint32_t major;  //!< 主版本号 (Major)
        uint32_t minor;  //!< 次版本号 (Minor)
    };

    /**
     * @struct InterfaceDetails
     * @brief [数据结构] 描述一个已实现的接口的详细信息。
     */
    struct InterfaceDetails {
        InterfaceId iid;      //!< 接口的唯一 ID (IID)
        std::string name;     //!< 接口的名称 (例如 "IDemoSimple")
        InterfaceVersion version;  //!< 接口的版本 (vMajor.vMinor)
    };

    /**
     * @struct ComponentDetails
     * @brief [数据结构] 描述一个已注册的组件/服务的详细信息。
     */
    struct ComponentDetails {
        ClassId clsid;               //!< 组件实现类的唯一 ID (CLSID)
        std::string alias;           //!< 组件的别名 (例如 "Demo.Logger.Default")
        bool is_singleton;           //!< 是服务 (true) 还是组件 (false)
        std::string source_plugin_path;  //!< 加载此组件的插件 DLL/SO 的完整路径
        bool is_registered_as_default;  //!< 是否被注册为至少一个接口的“默认”实现
        std::vector<InterfaceDetails>
            implemented_interfaces;  //!< 此组件实现的所有接口的列表
    };

    /**
     * @class IPluginQuery
     * @brief [核心服务] 框架的内省服务接口。
     *
     * [受众：插件开发者 和 框架使用者]
     *
     * 提供了查询框架当前状态的能力，例如加载了哪些插件、
     * 注册了哪些组件、组件实现了哪些接口等。
     *
     * @example
     * \code{.cpp}
     * void DebugPrintAllLoggers() {
     * // 1. 获取内省服务
     * auto query = z3y::GetService<IPluginQuery>(clsid::kPluginQuery);
     * // 2. 查找所有实现了 IDemoLogger 接口的组件
     * auto loggers = query->FindComponentsImplementing(IDemoLogger::kIid);
     * for (const auto& details : loggers) {
     * std::cout << "Found Logger: " << details.alias << std::endl;
     * }
     * }
     * \endcode
     */
    class IPluginQuery : public virtual IComponent {
    public:
        Z3Y_DEFINE_INTERFACE(IPluginQuery, "z3y-core-IPluginQuery-IID-A0000003", 1, 0)

            /**
             * @brief 获取当前注册在框架中的 *所有* 组件和服务的详细信息。
             * @return `ComponentDetails` 的 `std::vector`。
             */
            [[nodiscard]] virtual std::vector<ComponentDetails> GetAllComponents()
            const = 0;

        /**
         * @brief 通过 ClassId (CLSID) 获取特定组件的详细信息。
         * @param[in] clsid 要查询的组件 CLSID。
         * @param[out] out_details 如果找到，用于填充组件信息的结构体。
         * @return `true` 如果找到了该 CLSID，`false` 则未找到。
         */
        [[nodiscard]] virtual bool GetComponentDetails(
            ClassId clsid, ComponentDetails& out_details) const = 0;

        /**
         * @brief 通过别名 (Alias) 获取特定组件的详细信息。
         * @param[in] alias 要查询的组件别名 (例如 "Demo.Logger.Default")。
         * @param[out] out_details 如果找到，用于填充组件信息的结构体。
         * @return `true` 如果找到了该别名，`false` 则未找到。
         */
        [[nodiscard]] virtual bool GetComponentDetailsByAlias(
            const std::string& alias, ComponentDetails& out_details) const = 0;

        /**
         * @brief 查找所有实现了特定接口 (IID) 的组件。
         * @param[in] iid 要查询的接口 ID (例如 `IDemoLogger::kIid`)。
         * @return 实现了该接口的所有组件的 `ComponentDetails` 列表。
         */
        [[nodiscard]] virtual std::vector<ComponentDetails> FindComponentsImplementing(
            InterfaceId iid) const = 0;

        /**
         * @brief 获取所有已成功加载的插件 (DLL/SO) 文件的路径列表。
         * @return 插件文件路径的 `std::vector<std::string>`。
         */
        [[nodiscard]] virtual std::vector<std::string> GetLoadedPluginFiles()
            const = 0;

        /**
         * @brief 获取特定插件文件注册的所有组件。
         * @param[in] plugin_path 插件的完整路径 (应来自
         * `GetLoadedPluginFiles()`)。
         * @return 该插件注册的所有组件的 `ComponentDetails` 列表。
         */
        [[nodiscard]] virtual std::vector<ComponentDetails> GetComponentsFromPlugin(
            const std::string& plugin_path) const = 0;
    };

}  // namespace z3y

#endif  // Z3Y_FRAMEWORK_I_PLUGIN_QUERY_H_