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
 * @file component_helpers.h
 * @brief 提供 `Z3Y_DEFINE_COMPONENT_ID` 宏，用于简化组件实现。
 * @author Yue Liu
 * @date 2025-06-21
 * @copyright Copyright (c) 2025 Yue Liu
 */

#pragma once

#ifndef Z3Y_FRAMEWORK_COMPONENT_HELPERS_H_
#define Z3Y_FRAMEWORK_COMPONENT_HELPERS_H_

#include "framework/class_id.h"  // 依赖 ClassId 和 ConstexprHash

 /**
  * @def Z3Y_DEFINE_COMPONENT_ID
  * @brief [插件开发者核心]
  * 在组件实现类中注入 `kClsid` 静态元数据。
  *
  * [受众：插件开发者 (实现插件)]
  *
  * 任何希望被注册的 *具体实现类* (例如 `DemoLoggerService`)
  * 都 **必须** 在其 `public` 区域使用此宏。
  *
  * [功能]
  * 它定义了 `kClsid` (ClassId)，
  * 这是 `PluginImpl` 基类和 `Z3Y_AUTO_REGISTER_...`
  * 宏所必需的。
  *
  * @param UuidString
  * 用于生成 `kClsid` 的唯一 UUID 字符串。
  * **必须** 保证此字符串在所有组件实现中是全局唯一的。
  *
  * @example
  * \code{.cpp}
  * #include "framework/z3y_define_impl.h"
  *
  * class DemoLoggerService : public z3y::PluginImpl<DemoLoggerService, IDemoLogger> {
  * public:
  * // 在 public 区域使用此宏
  * Z3Y_DEFINE_COMPONENT_ID("z3y-demo-CLoggerService-UUID-C50A10B4")
  *
  * // ... 构造函数、析构函数和接口实现 ...
  * };
  * \endcode
  */
#define Z3Y_DEFINE_COMPONENT_ID(UuidString)                                   \
  /** \
   * @brief 组件实现类的唯一 ID (由 Z3Y_DEFINE_COMPONENT_ID 宏定义)。 \
   */                                                                         \
  static constexpr z3y::ClassId kClsid = z3y::ConstexprHash(UuidString);

#endif  // Z3Y_FRAMEWORK_COMPONENT_HELPERS_H_