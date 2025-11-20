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
 * @file spdlog_provider_service.h
 * @brief [实现声明] ILogManagerService 的 spdlog 实现类。
 * @author Yue Liu
 * @date 2025-11-21
 *
 * @details
 * [架构角色]
 * 本类是日志系统的“引擎”。它负责：
 * 1. 解析 JSON 配置。
 * 2. 管理 spdlog 的全局线程池。
 * 3. 生产和缓存 Logger 实例。
 * 4. 处理 UTF-8 到 Windows 宽字符路径的转换。
 */

#pragma once

#ifndef Z3Y_PLUGIN_SPDLOG_PROVIDER_SERVICE_H_
#define Z3Y_PLUGIN_SPDLOG_PROVIDER_SERVICE_H_

 // 引入 spdlog 核心头文件
#include <spdlog/spdlog.h>
#include <spdlog/async.h> // [必须] 否则无法使用 async_overflow_policy

#include <atomic>
#include <map>
#include <mutex>
#include <shared_mutex>  // C++17 读写锁
#include <string>
#include <vector>
#include <list>
#include <filesystem>

#include "framework/z3y_define_impl.h"
#include "interfaces_core/i_log_service.h"

namespace z3y {
    namespace plugins {
        namespace log {

            using namespace z3y::interfaces::core;

            /**
             * @class LoggerImpl
             * @brief [内部适配器] 将 ILogger 接口调用转发给 spdlog::logger。
             * @details 持有一个 spdlog::logger 的 shared_ptr。
             */
            class LoggerImpl : public PluginImpl<LoggerImpl, ILogger> {
            public:
                Z3Y_DEFINE_COMPONENT_ID("z3y-core-CLogChannelImpl-UUID-L0000004");

                explicit LoggerImpl(std::shared_ptr<spdlog::logger> logger);

                bool IsEnabled(LogLevel level) const noexcept override;
                void Log(const LogSourceLocation& loc, LogLevel level, const std::string& message) override;

                // [内部] 暴露底层指针，供 Service 动态修改级别
                std::shared_ptr<spdlog::logger> GetSpdlogLogger() { return logger_; }

            private:
                std::shared_ptr<spdlog::logger> logger_;
            };

            /**
             * @struct SinkConfig
             * @brief [配置结构] 描述一个输出目标。
             */
            struct SinkConfig {
                std::string type;       // 类型: "stdout_color_sink", "daily_file_sink", "rotating_file_sink"
                std::string base_name;  // 路径 (UTF-8 编码)

                // [Rotating Sink 参数]
                size_t max_size = 1024 * 1024 * 5; // 默认 5MB
                size_t max_files = 3;              // 默认 3 个备份

                spdlog::level::level_enum level;   // Sink 级过滤门槛
                std::shared_ptr<spdlog::sinks::sink> instance = nullptr; // 懒加载缓存实例
            };

            /**
             * @struct RuleConfig
             * @brief [配置结构] 描述路由规则。
             */
            struct RuleConfig {
                std::string matcher;                  // 前缀匹配串
                std::vector<std::string> sink_names;  // 目标 Sinks
            };

            /**
             * @struct LevelOverride
             * @brief [运行时状态] 动态等级覆写规则。
             * @details 用于记录通过 SetLevel 接口设置的动态规则，以便应用到未来创建的 Logger 上。
             */
            struct LevelOverride {
                std::string prefix;
                spdlog::level::level_enum level;
            };

            /**
             * @class SpdlogProviderService
             * @brief [核心实现] 基于 spdlog 的高性能日志管理器。
             *
             * @section Maintainer 维护者指南
             * [并发模型]
             * - `provider_lock_` (shared_mutex): 保护 `logger_cache_` 和 `level_overrides_`。
             * - GetLogger 采用 "读写分离" 策略：绝大多数命中缓存的调用只需获取读锁，性能极高。
             *
             * [生命周期]
             * - **析构安全**: `Shutdown()` 仅刷新缓冲区，不关闭 spdlog。真正的 cleanup 延迟到析构函数。
             * - 这防止了框架卸载过程中，其他组件在析构函数里写日志导致崩溃的问题。
             */
            class SpdlogProviderService
                : public PluginImpl<SpdlogProviderService, ILogManagerService> {
            public:
                // [Fix] 使用字面量 ID，这是该服务的唯一实现标识
                Z3Y_DEFINE_COMPONENT_ID("z3y-core-CSpdlogProvider-UUID-L0000001");

                SpdlogProviderService();
                ~SpdlogProviderService() override;

                bool InitializeService(const std::string& config_file_path,
                    const std::string& log_root_directory) override;

                PluginPtr<ILogger> GetLogger(const std::string& name) override;

                // 动态调整接口实现
                void SetLevel(const std::string& name_prefix, LogLevel level) override;
                void Flush() override;
                void Shutdown() override;

            private:
                PluginPtr<ILogger> GetFallbackLogger(const std::string& name);
                spdlog::level::level_enum ParseLogLevel(const std::string& level_str,
                    spdlog::level::level_enum default_level);

                // [内部] 获取 Sink (必须在 provider_lock_ 的写锁保护下调用)
                std::shared_ptr<spdlog::sinks::sink> GetOrCreateSink_UNLOCKED(
                    const std::string& sink_name);

                // [内部] 检查是否有动态覆写规则适用于该 logger (无锁辅助函数)
                void ApplyLevelOverrides_UNLOCKED(const std::string& name, std::shared_ptr<spdlog::logger> logger);

                // [P0 Fix] 跨平台路径转码 (解决 Windows 中文路径问题)
                static std::filesystem::path Utf8ToPath(const std::string& path_str);

                std::mutex init_mutex_;           // 保护初始化过程
                std::shared_mutex provider_lock_; // 读写锁，保护缓存和状态

                bool is_initialized_ = false;

                // 运行时配置
                std::string log_directory_;
                std::string format_pattern_;

                // 异步策略配置
                size_t async_queue_size_ = 8192;
                spdlog::async_overflow_policy async_policy_ = spdlog::async_overflow_policy::block;

                // 自动 Flush 配置
                spdlog::level::level_enum flush_level_ = spdlog::level::err;
                size_t flush_interval_sec_ = 5;

                std::map<std::string, SinkConfig> sinks_config_;
                std::vector<RuleConfig> rules_;
                RuleConfig default_rule_;

                // 运行时动态调整的等级记录表
                std::list<LevelOverride> level_overrides_;

                // Logger 缓存 (Key: Logger Name)
                std::map<std::string, std::shared_ptr<LoggerImpl>> logger_cache_;

                // 备用 Logger (初始化失败时使用)
                PluginPtr<ILogger> fallback_logger_;
            };

        }  // namespace log
    }  // namespace plugins
}  // namespace z3y

#endif  // Z3Y_PLUGIN_SPDLOG_PROVIDER_SERVICE_H_