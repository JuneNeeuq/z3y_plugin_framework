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
 * @file z3y_define_interface.h
 * @brief [All-in-One] 供“插件接口”定义者使用的便捷头文件。
 * @author Yue Liu
 * @date 2025-06-29
 * @copyright Copyright (c) 2025 Yue Liu
 *
 * @details
 * [受众：插件开发者 (定义接口)]
 *
 * 任何定义 *纯抽象接口* (例如 `IDemoLogger.h`) 的头文件都应 `#include` 此文件。
 * 它包含了定义一个接口所需的所有工具和基类，确保了ABI的兼容性。
 *
 * 包含的内容:
 * - `IComponent` (所有接口的基类)
 * - `Z3Y_DEFINE_INTERFACE` (用于注入IID和版本信息的宏)
 * - `IEventBus`, `Event` (用于定义自定义事件)
 * - `Z3Y_DEFINE_EVENT` (用于定义事件ID的宏)
 * - `IPluginQuery` (用于内省的接口)
 * - `PluginException` (用于API签名的异常类)
 * - `Connection` (用于事件API)
 */

#pragma once

#ifndef Z3Y_FRAMEWORK_DEFINE_INTERFACE_H_
#define Z3Y_FRAMEWORK_DEFINE_INTERFACE_H_

 // 1. API 导出/导入 (dllexport / dllimport)
#include "framework/z3y_framework_api.h"

// 2. 核心 ID 定义 (ClassId, InterfaceId, EventId)
#include "framework/class_id.h"

// 3. 核心异常 (PluginException, InstanceError)
#include "framework/plugin_exceptions.h"

// 4. 接口定义宏 (Z3Y_DEFINE_INTERFACE)
#include "framework/interface_helpers.h"

// 5. 事件定义宏 (Z3Y_DEFINE_EVENT)
#include "framework/event_helpers.h"

// 6. 核心基类 (IComponent, PluginPtr)
#include "framework/i_component.h"

// 7. 事件连接类型 (ConnectionType)
#include "framework/connection_type.h"

// 8. 事件连接句柄 (Connection, ScopedConnection)
#include "framework/connection.h"

// 9. 事件总线接口 (IEventBus, Event)
#include "framework/i_event_bus.h"

// 10. 内省接口 (IPluginQuery, ComponentDetails)
#include "framework/i_plugin_query.h"

#endif  // Z3Y_FRAMEWORK_DEFINE_INTERFACE_H_