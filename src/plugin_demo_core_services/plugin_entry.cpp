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
 * @file plugin_entry.cpp
 * @brief `plugin_demo_core_services` 插件的入口点文件。
 * @author Yue Liu
 * @date 2025-08-03
 * @copyright Copyright (c) 2025 Yue Liu
 *
 * @details
 * [受众：插件开发者]
 *
 * [插件入口点清单]
 * 1. `#include "framework/z3y_define_impl.h"`
 * (它包含了 `Z3Y_DEFINE_PLUGIN_ENTRY` 宏的定义)。
 * 2. 使用 `Z3Y_DEFINE_PLUGIN_ENTRY` 宏。
 *
 * 每一个插件 (DLL/SO) 项目都 *必须* 有一个 (且只有一个)
 * 包含 `Z3Y_DEFINE_PLUGIN_ENTRY` 宏的 `.cpp` 文件。
 */

#include "framework/z3y_define_impl.h"  // 包含 Z3Y_DEFINE_PLUGIN_ENTRY

 /**
  * @brief [插件开发者核心] 自动定义 z3yPluginInit 函数。
  *
  * [受众：框架维护者]
  * 此宏会自动生成 C 语言入口点 `z3yPluginInit`。
  * 该函数会查找并执行此 *特定插件* (DLL/SO)
  * 中所有通过 `Z3Y_AUTO_REGISTER_...` 定义的静态注册任务。
  *
  * 在本插件中，它将执行以下文件中的宏：
  * - `demo_logger_service.cpp`
  * - `advanced_demo_logger.cpp`
  * - `demo_simple_impl_a.cpp`
  * - `demo_simple_impl_b.cpp`
  */
Z3Y_DEFINE_PLUGIN_ENTRY;