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
 * @file i_log_service.h
 * @brief [核心接口] 定义 z3y 核心日志服务接口 (ILogManagerService, ILogger)。
 * @author Yue Liu
 * @date 2025-11-20
 *
 * @details
 * [设计思想]
 * 这是日志系统的 ABI (二进制接口) 契约。
 * 为了保持二进制兼容性，此文件不包含任何第三方库（如 spdlog）的头文件。
 * 所有依赖都隐藏在实现层 (Type Erasure)。
 *
 * [编码契约]
 * 为了保证跨平台（特别是 Windows/Linux）的一致性，本接口中所有的
 * std::string 参数（尤其是文件路径）**必须使用 UTF-8 编码**。
 * 宿主程序负责将本地编码（如 Windows 的 GBK 或 wstring）转换为 UTF-8 后再传入。
 */

#pragma once

#include "framework/z3y_define_interface.h"
#include <string>

namespace z3y {
    namespace interfaces {
        namespace core {

            /**
             * @enum LogLevel
             * @brief 通用日志级别定义。
             * @note 实现层必须将其映射到具体库（如 spdlog）的级别。
             */
            enum class LogLevel {
                Trace,
                Debug,
                Info,
                Warn,
                Error,
                Fatal
            };

            /**
             * @struct LogSourceLocation
             * @brief 源代码位置信息容器。
             * @details 用于在日志中记录文件名、行号和函数名。
             * 通常由 Z3Y_LOG_SOURCE_LOCATION 宏自动生成。
             */
            struct LogSourceLocation {
                const char* file_name;
                int line_number;
                const char* function_name;
            };

            /**
             * @class ILogger
             * @brief [插件使用] 日志记录器接口。
             *
             * @section User 使用者指南
             * - **获取方式**: 不要直接 `new`，应通过 `ILogManagerService::GetLogger()` 获取。
             * - **最佳实践**: 建议在插件初始化 (`Initialize`) 时获取并缓存 `PluginPtr<ILogger>`，
             * 避免在热路径（如高频循环）中重复调用 `GetLogger`。
             * - **线程安全**: 实现类必须保证 `Log` 方法是线程安全的。
             */
            class ILogger : public virtual IComponent {
            public:
                Z3Y_DEFINE_INTERFACE(ILogger, "z3y-core-ILogger-IID-L0000002", 1, 0);

                /**
                 * @brief [高频] 检查指定日志级别是否启用。
                 * @details 用于在执行昂贵的格式化操作前进行快速检查 (Short-circuit evaluation)。
                 */
                virtual bool IsEnabled(LogLevel level) const noexcept = 0;

                /**
                 * @brief [底层] 提交日志。
                 * @warning 即使 level 未启用，此函数也可能被调用。
                 * 强烈建议使用 `Z3Y_LOG_...` 宏，宏内部会自动先调用 `IsEnabled`。
                 *
                 * @param loc 代码位置信息。
                 * @param level 日志级别。
                 * @param message 已经格式化好的日志内容 (必须为 UTF-8 编码)。
                 */
                virtual void Log(const LogSourceLocation& loc, LogLevel level, const std::string& message) = 0;
            };

            /**
             * @class ILogManagerService
             * @brief [核心服务] 日志系统管理器。
             *
             * @section Maintainer 维护者指南
             * - **单例模式**: 全局负责管理 spdlog 线程池、Sinks 和 Logger 缓存。
             * - **生命周期**: 它是宿主程序配置日志系统的唯一入口。
             * - **职责**: 解析配置、分发 Logger、执行全局 Flush。
             */
            class ILogManagerService : public virtual IComponent {
            public:
                Z3Y_DEFINE_INTERFACE(ILogManagerService, "z3y-core-ILogManagerService-IID-L0000003", 2, 0);

                /**
                 * @brief [宿主调用] 初始化日志系统。
                 * @param config_file_path 配置文件 (JSON) 的绝对路径。**[必须 UTF-8]**
                 * @param log_root_directory 日志文件存放的根目录。**[必须 UTF-8]**
                 * @return true 初始化成功；false 初始化失败 (如配置文件格式错误，此时将回退到控制台输出)。
                 */
                virtual bool InitializeService(
                    const std::string& config_file_path,
                    const std::string& log_root_directory
                ) = 0;

                /**
                 * @brief [插件调用] 获取或创建指定名称的日志器。
                 * @param name 推荐使用分层命名，例如 "Plugins.Camera.Hikvision"。**[必须 UTF-8]**
                 * 系统将根据配置规则自动匹配输出目标 (Sinks) 和级别。
                 * @return 永远返回有效的 ILogger 指针 (如果系统未初始化，返回 Fallback Logger)。
                 */
                virtual PluginPtr<ILogger> GetLogger(const std::string& name) = 0;

                /**
                 * @brief [运维调用] 动态设置日志级别 (无需重启)。
                 * @param name_prefix Logger 名称前缀。空字符串代表设置所有 Logger。
                 * @param level 新的日志级别。
                 * @details 此设置会持久化到内存中，新创建的符合前缀的 Logger 也会应用此级别。
                 */
                virtual void SetLevel(const std::string& name_prefix, LogLevel level) = 0;

                /**
                 * @brief [运维调用] 强制刷新所有缓冲区到磁盘。
                 */
                virtual void Flush() = 0;
            };

        } // namespace core
    } // namespace interfaces
} // namespace z3y