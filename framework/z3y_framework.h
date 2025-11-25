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
 * @file z3y_framework.h
 * @brief [All-in-One] 供“宿主程序” (Host) 使用的便捷头文件。
 * @author Yue Liu
 * @date 2025-06-29
 * @copyright Copyright (c) 2025 Yue Liu
 *
 * @details
 * [受众：框架使用者 (宿主 Host)]
 *
 * 宿主应用程序 (例如 `main.cpp`) 应该 `#include` 此文件。
 *
 * 它提供了一个单一、便捷的入口，包含了宿主与框架交互所需的一切：
 * - `z3y::PluginManager` (核心管理器类)
 * - `z3y::PluginManager::Create()` (用于启动框架的工厂函数)
 * - `z3y::GetDefaultService` (服务定位器，用于获取插件服务)
 * - `z3y::PluginCast` (安全类型转换)
 * - `z3y::event::...` (框架核心事件)
 * - `z3y::PluginException` (标准异常类型)
 * - 所有核心接口 (IEventBus, IPluginQuery 等)
 */

#pragma once

#ifndef Z3Y_FRAMEWORK_H_
#define Z3Y_FRAMEWORK_H_

 // 1. 包含所有“接口”定义所需的文件
#include "framework/z3y_define_interface.h"

// 2. 安全类型转换 (PluginCast)
#include "framework/plugin_cast.h"

// 3. 核心管理器 (PluginManager)
#include "framework/plugin_manager.h"

// 4. 框架事件 (Framework Events)
#include "framework_events.h"

// 5. 服务定位器 (GetDefaultService 等)
#include "framework/z3y_service_locator.h"

#include "framework/z3y_utils.h"

#endif  // Z3Y_FRAMEWORK_H_