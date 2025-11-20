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
 * @file z3y_log_macros.h
 * @brief [插件使用] 提供便捷的日志记录宏 (Z3Y_LOG_...)。
 * @author Yue Liu
 *
 * @details
 * [设计意图]
 * 1. **自动上下文捕获**: 利用编译器内置宏 (`__FILE__`, `__LINE__`) 自动填充位置信息。
 * 2. **零开销检查**: 在宏展开层面进行 `IsEnabled` 检查。如果日志级别未开启，后续的 `fmt::format` 格式化代码根本不会执行。
 * 3. **类型安全**: 集成 `{fmt}` 库，提供类型安全的字符串格式化。
 *
 * [依赖说明]
 * 包含此头文件会引入 `<spdlog/fmt/fmt.h>`。这意味着使用此宏的插件编译时需要链接 fmt 库 (通常由 interfaces_core 传递依赖)。
 */

#pragma once

#include "interfaces_core/i_log_service.h"
#include <spdlog/fmt/fmt.h> // 引入 fmt 库支持格式化

 // --- [内部] 跨平台函数名获取宏 ---
 // 不同编译器对函数名的宏定义不同，这里进行统一适配。
#if defined(_MSC_VER)
    // MSVC: 改用 __FUNCTION__ 以获取更简洁的 "Class::Method" 格式
    // 原 __FUNCSIG__ 会输出 "void __cdecl MyClass::Method(int)"，过于冗长
#define Z3Y_CURRENT_FUNCTION __FUNCTION__
#elif defined(__GNUC__) || defined(__clang__)
    // GCC/Clang: "MyClass::Method"
#define Z3Y_CURRENT_FUNCTION __PRETTY_FUNCTION__
#else
    // 标准 C++: 仅函数名 "Method"
#define Z3Y_CURRENT_FUNCTION __func__
#endif

namespace z3y {
    namespace interfaces {
        namespace core {

            /**
             * @brief [内部] 构造当前源码位置信息。
             * @note 使用了 Z3Y_CURRENT_FUNCTION 来获取尽可能详细的函数签名。
             */
#define Z3Y_LOG_SOURCE_LOCATION() \
        z3y::interfaces::core::LogSourceLocation{__FILE__, __LINE__, Z3Y_CURRENT_FUNCTION}

             /**
              * @brief [内部] 日志宏实现核心。
              * @details
              * 使用 do-while(0) 惯用法，确保宏在 if-else 语句中行为正确且不会引入多余的分号。
              * 1. 检查 logger_ptr 是否有效。
              * 2. 调用 IsEnabled 检查级别 (Short-circuit evaluation)。
              * 3. 如果通过，执行 fmt::format 格式化。
              * 4. 调用虚函数 Log 提交。
              */
#define Z3Y_LOG_IMPL(logger_ptr, level, format_str, ...) \
        do { \
            if ((logger_ptr) && (logger_ptr)->IsEnabled(level)) { \
                std::string formatted_msg = fmt::format(format_str, __VA_ARGS__); \
                (logger_ptr)->Log(Z3Y_LOG_SOURCE_LOCATION(), level, formatted_msg); \
            } \
        } while(0)

              /**
               * @name 日志记录宏 (用户 API)
               * @brief 插件开发者应使用这些宏来记录日志。
               *
               * @param logger_ptr ILogger 的智能指针 (PluginPtr<ILogger>)。
               * @param ... 格式化字符串及参数 (遵循 fmt/python 语法)。
               *
               * @example
               * PluginPtr<ILogger> logger = ...;
               * Z3Y_LOG_INFO(logger, "User {} logged in from {}", user_id, ip_addr);
               */
               ///@{
                #define Z3Y_LOG_TRACE(logger_ptr, ...) Z3Y_LOG_IMPL(logger_ptr, z3y::interfaces::core::LogLevel::Trace, __VA_ARGS__)
                #define Z3Y_LOG_DEBUG(logger_ptr, ...) Z3Y_LOG_IMPL(logger_ptr, z3y::interfaces::core::LogLevel::Debug, __VA_ARGS__)
                #define Z3Y_LOG_INFO(logger_ptr, ...)  Z3Y_LOG_IMPL(logger_ptr, z3y::interfaces::core::LogLevel::Info,  __VA_ARGS__)
                #define Z3Y_LOG_WARN(logger_ptr, ...)  Z3Y_LOG_IMPL(logger_ptr, z3y::interfaces::core::LogLevel::Warn,  __VA_ARGS__)
                #define Z3Y_LOG_ERROR(logger_ptr, ...) Z3Y_LOG_IMPL(logger_ptr, z3y::interfaces::core::LogLevel::Error, __VA_ARGS__)
                #define Z3Y_LOG_FATAL(logger_ptr, ...) Z3Y_LOG_IMPL(logger_ptr, z3y::interfaces::core::LogLevel::Fatal, __VA_ARGS__)
                ///@}

        } // namespace core
    } // namespace interfaces
} // namespace z3y