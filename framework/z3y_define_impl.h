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
 * @file z3y_define_impl.h
 * @brief [All-in-One] 供“插件实现”开发者使用的便捷头文件。
 * @author Yue Liu
 * @date 2025-06-29
 * @copyright Copyright (c) 2025 Yue Liu
 *
 * @details
 * [受众：插件开发者 (实现插件)]
 *
 * 任何实现具体插件功能的 `.h` 或 `.cpp` 文件都应 `#include` 此文件。
 * 它自动包含了 `z3y_define_interface.h` 中的所有内容，并额外添加了
 * 实现、注册和使用插件所需的所有工具。
 *
 * 包含的内容:
 * - `PluginImpl` (CRTP基类，自动实现QueryInterface)
 * - `Z3Y_DEFINE_COMPONENT_ID` (用于定义实现类的CLSID)
 * - `Z3Y_AUTO_REGISTER_SERVICE` (自动注册单例服务)
 * - `Z3Y_AUTO_REGISTER_COMPONENT` (自动注册瞬态组件)
 * - `Z3Y_DEFINE_PLUGIN_ENTRY` (自动生成插件入口函数)
 * - `z3y::GetDefaultService` (服务定位器，用于获取依赖)
 * - `z3y::PluginCast` (安全类型转换)
 */

#pragma once

#ifndef Z3Y_FRAMEWORK_DEFINE_IMPL_H_
#define Z3Y_FRAMEWORK_DEFINE_IMPL_H_

 // 1. 包含所有“接口”定义所需的文件
#include "framework/z3y_define_interface.h"

// 2. 安全类型转换 (PluginCast)
#include "framework/plugin_cast.h"

// 3. CRTP 模板基类 (PluginImpl)
#include "framework/plugin_impl.h"

// 4. 组件 ID 定义宏 (Z3Y_DEFINE_COMPONENT_ID)
#include "framework/component_helpers.h"

// 5. 插件注册表接口 (IPluginRegistry)
//    (Z3Y_DEFINE_PLUGIN_ENTRY 需要)
#include "framework/i_plugin_registry.h"

// 6. 注册模板 (RegisterComponent, RegisterService)
#include "framework/plugin_registration.h"

// 7. 自动注册宏 (Z3Y_AUTO_REGISTER_...)
#include "framework/auto_registration.h"

// 8. 插件管理器头文件 (PluginManager)
//    (z3y_service_locator 需要)
#include "framework/plugin_manager.h"

// 9. 服务定位器 (GetDefaultService 等)
#include "framework/z3y_service_locator.h"

#endif  // Z3Y_FRAMEWORK_DEFINE_IMPL_H_