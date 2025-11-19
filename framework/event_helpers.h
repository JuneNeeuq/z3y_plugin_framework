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
 * @file event_helpers.h
 * @brief 提供 `Z3Y_DEFINE_EVENT` 宏，用于简化事件结构定义。
 * @author Yue Liu
 * @date 2025-06-21
 * @copyright Copyright (c) 2025 Yue Liu
 */

#pragma once

#ifndef Z3Y_FRAMEWORK_EVENT_HELPERS_H_
#define Z3Y_FRAMEWORK_EVENT_HELPERS_H_

#include "framework/class_id.h"  // 依赖 EventId 和 ConstexprHash

 /**
  * @def Z3Y_DEFINE_EVENT
  * @brief [插件开发者核心] 在事件结构体中注入 `kEventId` 和 `kName`。
  *
  * [受众：插件开发者 (定义事件)]
  *
  * 任何继承自 `z3y::Event` 的事件结构体都 **必须** 在其定义中 (通常是 `public`)
  * 使用此宏。
  *
  * [功能]
  * 它定义了 `kEventId` (EventId) 和 `kName` (const char*)，
  * 这是事件总线 `Subscribe`/`Fire` API 所必需的。
  *
  * @param ClassName  事件的结构体名称 (例如 `PluginLoadSuccessEvent`)。
  * @param UuidString 用于生成 `kEventId` 的唯一 UUID 字符串。
  * **必须** 保证此字符串在所有事件中是全局唯一的。
  *
  * @example
  * \code{.cpp}
  * #include "framework/i_event_bus.h"
  * #include "framework/event_helpers.h"
  *
  * struct MyCustomEvent : public z3y::Event {
  * // 在结构体定义中使用此宏
  * Z3Y_DEFINE_EVENT(MyCustomEvent, "my-custom-event-uuid-string")
  *
  * int data;
  * MyCustomEvent(int d) : data(d) {}
  * };
  * \endcode
  */
#define Z3Y_DEFINE_EVENT(ClassName, UuidString)                               \
  /** \
   * @brief 事件的唯一 ID (由 Z3Y_DEFINE_EVENT 宏定义)。 \
   */                                                                         \
  static constexpr z3y::EventId kEventId = z3y::ConstexprHash(UuidString);    \
  /** \
   * @brief 事件的名称 (由 Z3Y_DEFINE_EVENT 宏定义)。 \
   */                                                                         \
  static constexpr const char* kName = #ClassName;

#endif  // Z3Y_FRAMEWORK_EVENT_HELPERS_H_