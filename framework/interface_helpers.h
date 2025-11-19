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
 * @file interface_helpers.h
 * @brief 提供 `Z3Y_DEFINE_INTERFACE` 宏，用于简化接口定义。
 * @author Yue Liu
 * @date 2025-06-08
 * @copyright Copyright (c) 2025 Yue Liu
 */

#pragma once

#ifndef Z3Y_FRAMEWORK_INTERFACE_HELPERS_H_
#define Z3Y_FRAMEWORK_INTERFACE_HELPERS_H_

#include "framework/class_id.h"  // 依赖 InterfaceId 和 ConstexprHash

 /**
  * @def Z3Y_DEFINE_INTERFACE
  * @brief [插件开发者核心]
  * 在接口类定义中注入所需的静态元数据。
  *
  * [受众：插件开发者 (定义接口)]
  *
  * 任何继承自 `IComponent` 的 *接口类* (例如 `IDemoLogger`)
  * 都 **必须** 在其 `public` 区域使用此宏。
  *
  * [功能]
  * 它在编译期定义了4个 `static constexpr` 成员，
  * 这些成员是 `PluginImpl` 和 `PluginCast`
  * 运行所必需的：
  * 1. `kIid` (InterfaceId): 编译期哈希的接口 ID。
  * 2. `kName` (const char*): 接口的字符串名称 (用于调试和内省)。
  * 3. `kVersionMajor` (uint32_t): 接口的主版本号。
  * 4. `kVersionMinor` (uint32_t): 接口的次版本号。
  *
  * [版本控制规则]
  * - **主版本 (Major)**: 当发生破坏性 API 变更时（例如函数签名改变、删除函数），
  * 必须递增此版本。`PluginCast` 会拒绝主版本不匹配的转换。
  * - **次版本 (Minor)**: 当发生非破坏性 API
  * 变更时（例如新增虚函数），应递增此版本。`PluginCast`
  * 允许宿主（请求者）的次版本号 *低于* 插件（实现者）的次版本号。
  *
  * @param ClassName    接口的 C++ 类名 (例如 `IDemoLogger`)。
  * @param UuidString   用于生成 `kIid` 的唯一 UUID 字符串。
  * @param VersionMajor 接口的主版本号 (例如 `1`)。
  * @param VersionMinor 接口的次版本号 (例如 `0`)。
  *
  * @example
  * \code{.cpp}
  * class IDemoLogger : public virtual IComponent {
  * public:
  * // 在 public 区域使用此宏
  * Z3Y_DEFINE_INTERFACE(IDemoLogger, "z3y-demo-IDemoLogger-IID-B1B542F8", 1, 0)
  *
  * virtual void Log(const std::string& message) = 0;
  * };
  * \endcode
  */
#define Z3Y_DEFINE_INTERFACE(ClassName, UuidString, VersionMajor, VersionMinor) \
  /** \
   * @brief 接口的唯一 ID (由 Z3Y_DEFINE_INTERFACE 宏定义)。             \
   */                                                                           \
  static constexpr z3y::InterfaceId kIid = z3y::ConstexprHash(UuidString);       \
  /** \
   * @brief 接口的名称 (由 Z3Y_DEFINE_INTERFACE 宏定义)。               \
   */                                                                           \
  static constexpr const char* kName = #ClassName; \
  /** \
   * @brief 接口的主版本号 (用于 ABI 兼容性检查)。                        \
   */                                                                           \
  static constexpr uint32_t kVersionMajor = VersionMajor;                       \
  /** \
   * @brief 接口的次版本号 (用于 ABI 兼容性检查)。                        \
   */                                                                           \
  static constexpr uint32_t kVersionMinor = VersionMinor;

#endif  // Z3Y_FRAMEWORK_INTERFACE_HELPERS_H_