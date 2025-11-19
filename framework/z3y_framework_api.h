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
 * @file z3y_framework_api.h
 * @brief 定义 z3y 框架核心库 (z3y_plugin_manager) 的 DLL 导出/导入宏。
 * @author Yue Liu
 * @date 2025-06-15
 * @copyright Copyright (c) 2025 Yue Liu
 *
 * @details
 * [受众：框架维护者]
 *
 * 此文件是实现 Pimpl 模式和 ABI 稳定性的关键。
 * 它定义了 `Z3Y_FRAMEWORK_API` 宏，
 * 该宏根据编译环境（Windows/POSIX）和编译目标（构建框架/使用框架）
 * 自动切换为 `dllexport`、`dllimport` 或 `__attribute__((visibility("default")))`。
 *
 * [工作流程]
 * 1. `z3y_plugin_manager` 核心库的 `CMakeLists.txt`
 * 定义了 `Z3Y_PLUGIN_MANAGER_AS_DLL` 宏。
 * 2. 当编译 `z3y_plugin_manager` 时，`Z3Y_FRAMEWORK_API` 变为 `dllexport`，
 * 导出 `PluginManager` 等类。
 * 3. 当宿主 (Host) 或其他插件 `#include "framework/..."`
 * 头文件时，`Z3Y_PLUGIN_MANAGER_AS_DLL` *未* 定义。
 * 4. 此时 `Z3Y_FRAMEWORK_API` 变为 `dllimport`，
 * 导入 `PluginManager` 等类。
 *
 * [受众：插件开发者 和 框架使用者]
 * 你永远不需要关心此文件。它由框架头文件自动包含。
 */

#pragma once

#ifndef Z3Y_FRAMEWORK_API_H_
#define Z3Y_FRAMEWORK_API_H_

#ifdef _WIN32
 // 平台是 Windows

 // Z3Y_PLUGIN_MANAGER_AS_DLL 是由 z3y_plugin_manager 自己的 CMakeLists.txt 定义的。
#ifdef Z3Y_PLUGIN_MANAGER_AS_DLL
/**
 * @def Z3Y_FRAMEWORK_API
 * @brief [内部] 标记为 `dllexport` (导出)。
 * @details
 * 当 *构建* z3y_plugin_manager.dll 本身时，
 * 此宏用于将其 C++ 类和函数导出，
 * 以便宿主和其他插件可以链接到它们。
 */
#define Z3Y_FRAMEWORK_API __declspec(dllexport)
#else
/**
 * @def Z3Y_FRAMEWORK_API
 * @brief [内部] 标记为 `dllimport` (导入)。
 * @details
 * 当 *使用* z3y_plugin_manager.dll 时 (例如在 host.exe 或其他插件中)，
 * 此宏用于从 DLL 导入符号。
 */
#define Z3Y_FRAMEWORK_API __declspec(dllimport)
#endif
#else
 // 平台是 POSIX (Linux, macOS)
 /**
  * @def Z3Y_FRAMEWORK_API
  * @brief [内部] 标记为 "default" 可见性 (导出)。
  * @details
  * 在 GCC/Clang
  * 上，这确保了符号在 .so/.dylib 之外是可见的，
  * 实现了 `dllexport` 和 `dllimport` 的统一功能。
  */
#define Z3Y_FRAMEWORK_API __attribute__((visibility("default")))
#endif

#endif  // Z3Y_FRAMEWORK_API_H_