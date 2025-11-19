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
 * @file auto_registration.h
 * @brief 定义插件自动注册宏 (Z3Y_AUTO_REGISTER_...)。
 * @author Yue Liu
 * @date 2025-06-29
 * @copyright Copyright (c) 2025 Yue Liu
 *
 * @details
 * [受众：插件开发者 (实现插件)]
 *
 * 这是实现“极简开发体验”的核心文件。
 * 它提供了 `Z3Y_AUTO_REGISTER_...` 和 `Z3Y_DEFINE_PLUGIN_ENTRY`
 * 宏。
 *
 * [使用方法]
 * 1. 在你的实现类 `.cpp` 文件顶部 (全局作用域)，
 * 使用 `Z3Y_AUTO_REGISTER_SERVICE` 或 `Z3Y_AUTO_REGISTER_COMPONENT`
 * 来声明你的类。
 * 2. 在你的插件项目 (DLL/SO) 中，
 * 创建 *一个* `.cpp` 文件 (例如 `plugin_entry.cpp`)，
 * 在其中只包含 `Z3Y_DEFINE_PLUGIN_ENTRY` 宏。
 *
 * [受众：框架维护者 - 设计思想]
 *
 * 这是一个“静态注册”模式的实现。
 *
 * 1. `Z3Y_AUTO_REGISTER_...` 宏会在 `.cpp` 文件的全局作用域中创建一个
 * *静态* `AutoRegistrar` 变量。
 * 2. C++ 标准保证这些静态变量的构造函数会在 `main` (或 `dlopen`) 之前执行。
 * 3. `AutoRegistrar` 的构造函数接收一个`RegistryFunc` (lambda表达式)。
 * 4. 构造函数将这个 lambda 添加到一个全局的 `std::vector`
 * (通过 `GetGlobalRegisterList()`)。
 * 5. 此时，这个全局 vector 中充满了来自不同 `.cpp` 文件的“注册请求”。
 * 6. 当宿主加载 DLL 并调用 `z3yPluginInit` 时，`Z3Y_DEFINE_PLUGIN_ENTRY`
 * 宏所定义的函数体会 *遍历* 这个全局 vector，并执行所有存储的 lambda。
 * 7. 每个 lambda 再调用 `z3y::RegisterComponent` 或 `z3y::RegisterService` (来自
 * `plugin_registration.h`)， 最终完成对 `IPluginRegistry` 的调用。
 *
 * [优点]
 * 插件开发者无需编写 `z3yPluginInit` 函数。他们只需要在实现类的 `.cpp` 文件顶部添加一个宏即可。
 */

#pragma once

#ifndef Z3Y_FRAMEWORK_AUTO_REGISTRATION_H_
#define Z3Y_FRAMEWORK_AUTO_REGISTRATION_H_

#include <functional>  // 用于 std::function, std::move
#include <memory>      // 用于 std::shared_ptr
#include <type_traits> // 用于 std::move
#include <vector>      // 用于 std::vector
#include "framework/i_plugin_registry.h"  // 依赖 IPluginRegistry
#include "framework/plugin_registration.h"  // 依赖 RegisterComponent

namespace z3y {
    namespace internal {
        // [受众：框架维护者]

        // (类型别名，用于存储“注册任务”的函数)
        using RegistryFunc = std::function<void(IPluginRegistry*)>;

        /**
         * @brief [内部] 获取全局的注册任务列表。
         *
         * [设计思想]
         * 使用“Meyers Singleton” (静态局部变量)，
         * 保证线程安全和初始化顺序。
         *
         * @return `std::vector<RegistryFunc>` 的引用。
         */
        inline std::vector<RegistryFunc>& GetGlobalRegisterList() {
            static std::vector<RegistryFunc> g_list;
            return g_list;
        }

        /**
         * @brief [内部] 自动注册辅助结构体。
         *
         * [设计思想]
         * 其构造函数会将一个注册任务 (lambda) 添加到全局列表中。
         */
        struct AutoRegistrar {
            AutoRegistrar(RegistryFunc func) {
                GetGlobalRegisterList().push_back(std::move(func));
            }
        };

        /**
         * @def Z3Y_AUTO_CONCAT_INNER(a, b)
         * @brief [内部] 宏拼接辅助
         */
#define Z3Y_AUTO_CONCAT_INNER(a, b) a##b
         /**
          * @def Z3Y_AUTO_CONCAT(a, b)
          * @brief [内部] 宏拼接辅助
          */
#define Z3Y_AUTO_CONCAT(a, b) Z3Y_AUTO_CONCAT_INNER(a, b)

    }  // namespace internal

    /**
     * @def Z3Y_AUTO_REGISTER_COMPONENT
     * @brief [插件开发者核心]
     * 自动注册一个瞬态组件 (Component)。
     *
     * [受众：插件开发者 (实现插件)]
     *
     * 在插件实现类的 `.cpp` 文件的全局作用域使用此宏。
     * “组件” (Component) 是瞬态的，
     * 意味着每次 `z3y::CreateInstance` 被调用时，都会创建一个 *新* 实例。
     *
     * @param ClassName    要注册的类名 (例如 `DemoSimpleImplA`)。
     * @param Alias        注册的别名 (例如 `"DemoSimple.A"`)。
     * @param IsDefault    是否为默认实现 (`true` 或 `false`)。
     *
     * @example
     * \code{.cpp}
     * // 在 demo_simple_impl_a.cpp 顶部
     * #include "demo_simple_impl_a.h"
     * #include "framework/z3y_define_impl.h"
     *
     * Z3Y_AUTO_REGISTER_COMPONENT(z3y::demo::DemoSimpleImplA, "Demo.Simple.A", true);
     *
     * namespace z3y { namespace demo {
     * // ... DemoSimpleImplA 的实现 ...
     * }}
     * \endcode
     */
#define Z3Y_AUTO_REGISTER_COMPONENT(ClassName, Alias, IsDefault)        \
  static z3y::internal::AutoRegistrar Z3Y_AUTO_CONCAT(                  \
      s_auto_reg_at_line_,                                              \
      __LINE__)( /* [受众：框架维护者] 创建一个唯一的静态变量名 */ \
                 [=](z3y::IPluginRegistry* r) {                         \
                   /* [受众：框架维护者] 任务 lambda：调用模板辅助函数 */ \
                   z3y::RegisterComponent<ClassName>(r, Alias, IsDefault); \
                 });

     /**
      * @def Z3Y_AUTO_REGISTER_SERVICE
      * @brief [插件开发者核心] 自动注册一个单例服务 (Service)。
      *
      * [受众：插件开发者 (实现插件)]
      *
      * 在插件实现类的 `.cpp` 文件的全局作用域使用此宏。
      * “服务” (Service) 是单例的，
      * 意味着 `z3y::GetService` 总是返回 *相同* 的实例。
      *
      * @param ClassName    要注册的类名 (例如 `DemoLoggerService`)。
      * @param Alias        注册的别名 (例如 `"Demo.Logger.Default"`)。
      * @param IsDefault    是否为默认实现 (`true` 或 `false`)。
      *
      * @example
      * \code{.cpp}
      * // 在 demo_logger_service.cpp 顶部
      * #include "demo_logger_service.h"
      * #include "framework/z3y_define_impl.h"
      *
      * Z3Y_AUTO_REGISTER_SERVICE(z3y::demo::DemoLoggerService, "Demo.Logger.Default",
      * true);
      *
      * namespace z3y { namespace demo {
      * // ... DemoLoggerService 的实现 ...
      * }}
      * \endcode
      */
#define Z3Y_AUTO_REGISTER_SERVICE(ClassName, Alias, IsDefault)          \
  static z3y::internal::AutoRegistrar Z3Y_AUTO_CONCAT(                  \
      s_auto_reg_at_line_,                                              \
      __LINE__)( /* [受众：框架维护者] 创建一个唯一的静态变量名 */ \
                 [=](z3y::IPluginRegistry* r) {                         \
                   /* [受众：框架维护者] 任务 lambda：调用模板辅助函数 */ \
                   z3y::RegisterService<ClassName>(r, Alias, IsDefault); \
                 });

      /**
       * @def Z3Y_DEFINE_PLUGIN_ENTRY
       * @brief [插件开发者核心]
       * 自动定义插件的 C 语言入口点 `z3yPluginInit`。
       *
       * [受众：插件开发者 (实现插件)]
       *
       * 你 **必须** 在你的 *每一个* 插件项目 (DLL/SO) 中，
       * 创建一个 `.cpp` 文件 (例如 `plugin_entry.cpp`)，
       * 并在该文件中 *仅* 包含此宏。
       *
       * [功能]
       * 此宏会为你生成 `z3yPluginInit` C 函数，宿主程序会调用此函数。
       * 该函数会遍历并执行所有由 `Z3Y_AUTO_REGISTER_...`
       * 宏在本项目中注册的任务。
       *
       * @example
       * \code{.cpp}
       * // 在 plugin_entry.cpp 中
       * #include "framework/z3y_define_impl.h"
       *
       * Z3Y_DEFINE_PLUGIN_ENTRY
       * \endcode
       */
#define Z3Y_DEFINE_PLUGIN_ENTRY                                            \
  extern "C" Z3Y_PLUGIN_API void z3yPluginInit(z3y::IPluginRegistry* registry) \
  {                                                                           \
    if (!registry) {                                                          \
      return;                                                                 \
    }                                                                         \
    /* [受众：框架维护者] 遍历全局列表并执行所有注册任务 */ \
    for (const auto& reg_func : z3y::internal::GetGlobalRegisterList()) {     \
      reg_func(registry);                                                     \
    }                                                                         \
  }

}  // namespace z3y

#endif  // Z3Y_FRAMEWORK_AUTO_REGISTRATION_H_