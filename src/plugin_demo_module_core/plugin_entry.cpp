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
 * @brief `plugin_demo_module_core` 插件的入口点文件。
 * @author Yue Liu
 * @date 2025-08-10
 * @copyright Copyright (c) 2025 Yue Liu
 *
 * @details
 * [受众：插件开发者]
 *
 * [插件入口点清单]
 * 1. `#include "framework/z3y_define_impl.h"`
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
  * 此宏将自动执行 `demo_core_features.cpp`
  * 中定义的 `Z3Y_AUTO_REGISTER_COMPONENT` 宏。
  */
Z3Y_DEFINE_PLUGIN_ENTRY;