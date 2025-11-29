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
 * @file spdlog_provider_service.cpp
 * @brief ILogManagerService 的工业级实现逻辑。
 * @details
 * 实现了基于 JSON 配置的动态日志路由、读写锁并发控制以及生产级的轮转策略。
 */

#include "spdlog_provider_service.h"

 // 引入宏定义头文件，解决 Z3Y_LOG_SOURCE_LOCATION 编译错误
#include "interfaces_core/z3y_log_macros.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>

// spdlog headers
#include <spdlog/async.h>
#include <spdlog/sinks/daily_file_sink.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <nlohmann/json.hpp>

// Windows 平台头文件 (用于 MultiByteToWideChar)
#ifdef _WIN32
#include <Windows.h>
#endif

// [注册] 将本类注册为 ILogManagerService 的默认实现 (is_default = true)
// 宿主调用 GetDefaultService<ILogManagerService>() 时将获取到本实例。
Z3Y_AUTO_REGISTER_SERVICE(z3y::plugins::log::SpdlogProviderService,
    "Logger.Provider", true);

namespace z3y {
    namespace plugins {
        namespace log {

            using json = nlohmann::json;

            // --- 辅助函数 ---

            spdlog::level::level_enum ToSpdlogLevel(LogLevel level) {
                switch (level) {
                case LogLevel::Trace: return spdlog::level::trace;
                case LogLevel::Debug: return spdlog::level::debug;
                case LogLevel::Info:  return spdlog::level::info;
                case LogLevel::Warn:  return spdlog::level::warn;
                case LogLevel::Error: return spdlog::level::err;
                case LogLevel::Fatal: return spdlog::level::critical;
                }
                return spdlog::level::info;
            }

            // --- LoggerImpl 实现 ---

            LoggerImpl::LoggerImpl(std::shared_ptr<spdlog::logger> logger)
                : logger_(std::move(logger)) {
            }

            bool LoggerImpl::IsEnabled(LogLevel level) const noexcept {
                return logger_->should_log(ToSpdlogLevel(level));
            }

            void LoggerImpl::Log(const LogSourceLocation& loc, LogLevel level,
                const std::string& message) {
                // 使用宏捕获的精确位置信息构造 spdlog source_loc
                spdlog::source_loc spdlog_loc{ loc.file_name, (int)loc.line_number,
                                              loc.function_name };
                logger_->log(spdlog_loc, ToSpdlogLevel(level), message);
            }

            // --- SpdlogProviderService 实现 ---

            SpdlogProviderService::SpdlogProviderService() {
                // 构造函数：初始化一个备用的 Fallback Logger。
                // 在 InitializeService 被调用前，或配置加载失败时，使用此 Logger 输出到控制台。
                try {
                    auto fallback_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
                    fallback_sink->set_level(spdlog::level::info);
                    auto fallback_spd_logger = std::make_shared<spdlog::logger>("fallback", fallback_sink);
                    fallback_spd_logger->set_pattern("[%H:%M:%S] [fallback] %v");
                    fallback_logger_ = std::make_shared<LoggerImpl>(fallback_spd_logger);
                } catch (...) {
                    std::cerr << "CRITICAL: Failed to create fallback logger!" << std::endl;
                }
            }

            SpdlogProviderService::~SpdlogProviderService() {
                // [生命周期] 仅在析构函数中彻底关闭 spdlog。
                // 此时应保证没有其他业务组件在运行。
                if (is_initialized_) {
                    spdlog::shutdown();
                }
            }

            void SpdlogProviderService::Shutdown() {
                // [生命周期] 仅 Flush，不 Shutdown。
                // 防止框架卸载过程中，其他组件析构函数写日志导致崩溃。
                if (is_initialized_) {
                    spdlog::apply_all([](std::shared_ptr<spdlog::logger> l) { l->flush(); });
                }
            }

            void SpdlogProviderService::Flush() {
                if (is_initialized_) {
                    spdlog::apply_all([](std::shared_ptr<spdlog::logger> l) { l->flush(); });
                }
            }

            // [P1] 动态设置日志级别
            // 注意：此函数需要同时更新“当前内存中的 Logger”和“未来创建的 Logger”。
            void SpdlogProviderService::SetLevel(const std::string& name_prefix, LogLevel level) {
                if (!is_initialized_) return;
                auto spd_level = ToSpdlogLevel(level);

                std::unique_lock<std::shared_mutex> lock(provider_lock_);

                // 1. 持久化规则：将此规则存入列表，以便 ApplyLevelOverrides_UNLOCKED 在创建新 Logger 时使用。
                level_overrides_.push_back({ name_prefix, spd_level });

                // 2. 即时生效：遍历当前缓存的所有 Logger，匹配前缀并更新级别。
                int count = 0;
                for (auto& [name, logger_impl] : logger_cache_) {
                    // rfind(prefix, 0) == 0 用于判断 starts_with
                    if (name_prefix.empty() || name.rfind(name_prefix, 0) == 0) {
                        if (logger_impl) {
                            logger_impl->GetSpdlogLogger()->set_level(spd_level);
                            count++;
                        }
                    }
                }

                // 记录操作日志
                if (fallback_logger_) {
                    fallback_logger_->Log(Z3Y_LOG_SOURCE_LOCATION(), LogLevel::Info,
                        fmt::format("SetLevel: Prefix='{}' -> Level={}. Updated {} loggers.", name_prefix, (int)spd_level, count));
                }
            }

            void SpdlogProviderService::ApplyLevelOverrides_UNLOCKED(const std::string& name, std::shared_ptr<spdlog::logger> logger) {
                // 遍历所有已记录的覆写规则，后设置的优先级更高（覆盖前面的）。
                for (const auto& override : level_overrides_) {
                    if (override.prefix.empty() || name.rfind(override.prefix, 0) == 0) {
                        logger->set_level(override.level);
                    }
                }
            }

            spdlog::level::level_enum SpdlogProviderService::ParseLogLevel(
                const std::string& level_str, spdlog::level::level_enum default_level) {
                std::string lower = level_str;
                std::transform(lower.begin(), lower.end(), lower.begin(),
                    [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

                if (lower == "trace") return spdlog::level::trace;
                if (lower == "debug") return spdlog::level::debug;
                if (lower == "info") return spdlog::level::info;
                if (lower == "warn") return spdlog::level::warn;
                if (lower == "error") return spdlog::level::err;
                if (lower == "fatal") return spdlog::level::critical;
                return default_level;
            }

            PluginPtr<ILogger> SpdlogProviderService::GetFallbackLogger(const std::string& name) {
                if (fallback_logger_) {
                    fallback_logger_->Log(Z3Y_LOG_SOURCE_LOCATION(), LogLevel::Error,
                        fmt::format("Service not init, fallback for '{}'", name));
                }
                return fallback_logger_;
            }

            bool SpdlogProviderService::InitializeService(
                const std::string& config_file_path,
                const std::string& log_root_directory) {
                std::lock_guard<std::mutex> lock(init_mutex_);
                if (is_initialized_) return true; // 幂等性保护

                try {
                    // 使用 Utf8ToPath 将 UTF-8 路径转换为 std::filesystem::path
                    // 这样 std::ifstream 在 Windows 上会使用宽字符路径打开文件，支持中文。
                    std::filesystem::path fs_config_path = z3y::utils::Utf8ToPath(config_file_path);
                    std::ifstream f(fs_config_path);

                    if (!f.is_open()) {
                        // 如果打开失败，抛出包含路径的错误信息
                        throw std::runtime_error("Config file not found: " + config_file_path);
                    }
                    // [Robustness] 允许 JSON 解析抛出异常，我们在外层捕获并打印错误日志
                    json config = json::parse(f);

                    std::unique_lock<std::shared_mutex> provider_lock(provider_lock_);
                    log_directory_ = log_root_directory;
                    level_overrides_.clear();

                    // 1. 解析全局配置
                    if (config.contains("global_settings")) {
                        auto& gs = config["global_settings"];
                        format_pattern_ = gs.value("format_pattern", "[%Y-%m-%d %H:%M:%S.%e] [%L] [%n] %v");
                        async_queue_size_ = gs.value("async_queue_size", 8192);
                        std::string p_str = gs.value("async_overflow_policy", "block");
                        async_policy_ = (p_str == "overrun_oldest") ? spdlog::async_overflow_policy::overrun_oldest
                            : spdlog::async_overflow_policy::block;

                        flush_interval_sec_ = gs.value("flush_interval_seconds", 5);
                        std::string f_lvl = gs.value("flush_on_level", "error");
                        flush_level_ = ParseLogLevel(f_lvl, spdlog::level::err);
                    }

                    // 2. 初始化线程池 (安全检查)
                    // 注意：如果宿主程序或其他插件已经初始化了 spdlog 线程池，再次初始化会抛出异常。
                    // 我们捕获该异常并记录警告，继续使用现有的线程池。
                    try {
                        spdlog::init_thread_pool(async_queue_size_, 1);
                    } catch (const spdlog::spdlog_ex&) {
                        fallback_logger_->Log(Z3Y_LOG_SOURCE_LOCATION(), LogLevel::Warn,
                            "Spdlog thread pool already initialized. Configured async_queue_size ignored.");
                    }

                    // 3. 设置定期刷盘
                    spdlog::flush_every(std::chrono::seconds(flush_interval_sec_));

                    // 4. 解析 Sinks (输出目标)
                    if (config.contains("sinks")) {
                        for (auto& [name, sink_conf] : config["sinks"].items()) {
                            SinkConfig cfg;
                            cfg.type = sink_conf.value("type", "stdout_color_sink");
                            cfg.level = ParseLogLevel(sink_conf.value("level", "info"), spdlog::level::info);

                            if (cfg.type.find("file") != std::string::npos) {
                                cfg.base_name = sink_conf.value("base_name", "app.log");
                            }
                            if (cfg.type == "rotating_file_sink") {
                                cfg.max_size = sink_conf.value("max_size", 1024 * 1024 * 5);
                                cfg.max_files = sink_conf.value("max_files", 3);
                            }
                            sinks_config_[name] = cfg;
                        }
                    }

                    // 5. 解析 Rules (路由规则)
                    if (config.contains("default_rule")) {
                        default_rule_.sink_names = config["default_rule"].value("sinks", std::vector<std::string>{});
                    }
                    if (config.contains("rules")) {
                        for (const auto& rule : config["rules"]) {
                            RuleConfig cfg;
                            cfg.matcher = rule.value("matcher", "");
                            cfg.sink_names = rule.value("sinks", std::vector<std::string>{});
                            if (!cfg.matcher.empty()) rules_.push_back(cfg);
                        }
                    }
                    // 排序规则：按 matcher 长度降序排列 (最长前缀匹配优先)
                    std::sort(rules_.begin(), rules_.end(), [](const auto& a, const auto& b) {
                        return a.matcher.length() > b.matcher.length();
                        });

                    is_initialized_ = true;

                    fallback_logger_->Log(Z3Y_LOG_SOURCE_LOCATION(), LogLevel::Info,
                        fmt::format("Log Service Initialized. Conf: {}", config_file_path));

                    return true;

                } catch (const std::exception& e) {
                    // 初始化失败，返回 false，宿主可据此决定是否终止程序
                    fallback_logger_->Log(Z3Y_LOG_SOURCE_LOCATION(), LogLevel::Fatal,
                        fmt::format("Log Service Init FAILED: {}", e.what()));
                    return false;
                }
            }

            std::shared_ptr<spdlog::sinks::sink>
                SpdlogProviderService::GetOrCreateSink_UNLOCKED(const std::string& sink_name) {
                auto it = sinks_config_.find(sink_name);
                if (it == sinks_config_.end()) {
                    throw std::runtime_error("Undefined sink: " + sink_name);
                }

                SinkConfig& config = it->second;
                if (config.instance) return config.instance;

                // 使用 Utf8ToPath 处理路径，确保在 Windows 上支持中文路径
                std::filesystem::path p_root = z3y::utils::Utf8ToPath(log_directory_);
                std::filesystem::path p_base = z3y::utils::Utf8ToPath(config.base_name);
                std::filesystem::path full_path = p_root / p_base;

                // 自动创建目录
                if ((config.type == "daily_file_sink" || config.type == "rotating_file_sink") && full_path.has_parent_path()) {
                    std::error_code ec;
                    std::filesystem::create_directories(full_path.parent_path(), ec);
                }

                // 获取本地路径字符串 (Windows: wstring, Linux: string) 传递给 spdlog
                auto native_path = full_path.native();

                std::shared_ptr<spdlog::sinks::sink> new_sink;

                if (config.type == "stdout_color_sink") {
                    new_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
                } else if (config.type == "daily_file_sink") {
                    new_sink = std::make_shared<spdlog::sinks::daily_file_sink_mt>(native_path, 0, 0);
                } else if (config.type == "rotating_file_sink") {
                    new_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                        native_path, config.max_size, config.max_files);
                } else {
                    throw std::runtime_error("Unsupported sink type: " + config.type);
                }

                new_sink->set_level(config.level);
                new_sink->set_pattern(format_pattern_);
                config.instance = new_sink;
                return new_sink;
            }

            PluginPtr<ILogger> SpdlogProviderService::GetLogger(const std::string& name) {
                if (!is_initialized_) return GetFallbackLogger(name);

                // [性能优化] 1. 读锁检查缓存 (Fast Path)
                {
                    std::shared_lock<std::shared_mutex> read_lock(provider_lock_);
                    auto it = logger_cache_.find(name);
                    if (it != logger_cache_.end()) return it->second;
                }

                // [性能优化] 2. 写锁创建 (Slow Path)
                std::unique_lock<std::shared_mutex> write_lock(provider_lock_);
                // [双重检查锁定] 再次检查，防止在锁升级期间被其他线程创建
                auto it = logger_cache_.find(name);
                if (it != logger_cache_.end()) return it->second;

                try {
                    // 匹配规则
                    RuleConfig* matched_rule = &default_rule_;
                    for (auto& r : rules_) {
                        if (name.rfind(r.matcher, 0) == 0) {
                            matched_rule = &r;
                            break;
                        }
                    }

                    // 收集 Sinks
                    std::vector<spdlog::sink_ptr> sinks;
                    for (const auto& s_name : matched_rule->sink_names) {
                        sinks.push_back(GetOrCreateSink_UNLOCKED(s_name));
                    }

                    // 创建异步 Logger
                    auto spd_logger = std::make_shared<spdlog::async_logger>(
                        name, sinks.begin(), sinks.end(), spdlog::thread_pool(),
                        async_policy_);

                    spd_logger->set_level(spdlog::level::trace); // 默认全开，由 Sinks 过滤
                    spd_logger->flush_on(flush_level_);          // 自动刷盘策略

                    // 应用动态覆写规则 (检查是否有运维命令要求修改此 Logger 的级别)
                    ApplyLevelOverrides_UNLOCKED(name, spd_logger);

                    // 必须注册到 spdlog 全局表，调用 Flush() 时才能找到它
                    spdlog::register_logger(spd_logger);

                    auto wrapper = std::make_shared<LoggerImpl>(spd_logger);
                    logger_cache_[name] = wrapper;
                    return wrapper;

                } catch (const std::exception& e) {
                    auto fb = GetFallbackLogger(name);
                    fb->Log(Z3Y_LOG_SOURCE_LOCATION(), LogLevel::Error,
                        fmt::format("Create logger '{}' failed: {}", name, e.what()));
                    return fb;
                }
            }

        }  // namespace log
    }  // namespace plugins
}  // namespace z3y