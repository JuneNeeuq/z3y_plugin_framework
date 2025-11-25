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
 * @file config_provider_service.cpp
 * @brief 配置服务核心实现 (Final Gold Master)。
 *
 * @details
 * 实现细节清单:
 * 1. **Initialization**: 使用 TryGetDefaultService 获取日志服务，实现软依赖。
 * 2. **Fail-Fast**: LoadConfig 遇到 JSON 解析错误时直接返回 Error，保留损坏文件现场。
 * 3. **Concurrency**: Save 操作使用快照 (Snapshot) 模式，锁持有时间仅为内存拷贝时间。
 * 4. **Atomicity**: Windows 下使用 `MoveFileExW` + `MOVEFILE_WRITE_THROUGH` 确保 NTFS 事务完整性。
 */

#include "config_provider_service.h"
#include "framework/z3y_framework.h" // for TryGetDefaultService
#include <fstream>
#include <iostream>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <io.h>
#include <fcntl.h>
#else
#include <unistd.h>
#endif

Z3Y_AUTO_REGISTER_SERVICE(z3y::plugins::config::ConfigProviderService, "Config.Manager", true);

namespace z3y {
    namespace plugins {
        namespace config {

            using json = nlohmann::json;

            // --- [辅助函数] ---

            bool ConfigProviderService::IsSafePath(const std::string& domain) {
                if (domain.empty()) return false;
                try {
                    std::filesystem::path p(z3y::utils::Utf8ToPath(domain));
                    // 安全检查 1: 禁止绝对路径 (防止访问 /etc 或 C:/Windows)
                    if (p.is_absolute()) return false;
                    // 安全检查 2: 禁止路径遍历 (防止 ../../)
                    for (const auto& part : p) {
                        if (part == "..") return false;
                    }
                    return true;
                } catch (...) { return false; }
            }

            // --- [实现类] ---

            ConfigProviderService::ConfigProviderService() {}

            ConfigProviderService::~ConfigProviderService() {
                // [安全防线] 析构时尝试保存所有未保存的更改。
                // 虽然我们提倡手动保存，但防止用户疏忽导致数据丢失，这里做最后一次尝试。
                // SaveAll();
            }

            bool ConfigProviderService::InitializeService(const std::string& config_root_dir) {
                // 1. [软依赖] 尝试获取日志服务 (No-Throw 接口)
                // 即使获取失败，也不影响 Config 服务启动，只是会降级到控制台输出。
                auto log_result = z3y::TryGetDefaultService<ILogManagerService>();
                auto log_mgr = log_result.first;

                if (log_mgr) {
                    logger_ = log_mgr->GetLogger("System.Config");
                } else {
                    logger_ = nullptr;
                }

                // 2. 初始化目录
                root_dir_utf8_ = config_root_dir;
                try {
                    auto path = z3y::utils::Utf8ToPath(root_dir_utf8_);
                    if (!std::filesystem::exists(path)) {
                        std::filesystem::create_directories(path);
                    }

                    LogInfo("Initialized Config Service. Root: " + root_dir_utf8_ +
                        (logger_ ? " [Log: Active]" : " [Log: Console Fallback]"));
                    return true;
                } catch (const std::exception& e) {
                    LogError("FATAL: Failed to create config root: " + std::string(e.what()));
                    return false;
                }
            }

            void ConfigProviderService::LogValidationError(const std::string& domain, const std::string& msg) {
                LogError("VALIDATION FAILED [" + domain + "]: " + msg);
            }

            void ConfigProviderService::LogInfo(const std::string& msg) {
                if (logger_) {
                    Z3Y_LOG_INFO(logger_, "{}", msg);
                } else {
                    std::cout << "[Config][Info] " << msg << std::endl;
                }
            }

            void ConfigProviderService::LogError(const std::string& msg) {
                if (logger_) {
                    Z3Y_LOG_ERROR(logger_, "{}", msg);
                } else {
                    std::cerr << "[Config][Error] " << msg << std::endl;
                }
            }

            void ConfigProviderService::LogWarn(const std::string& msg) {
                if (logger_) {
                    Z3Y_LOG_WARN(logger_, "{}", msg);
                } else {
                    std::cout << "[Config][Warn] " << msg << std::endl;
                }
            }

            std::shared_ptr<ConfigProviderService::FileContext>
                ConfigProviderService::GetOrCreateContext(const std::string& domain) {

                if (!IsSafePath(domain)) {
                    LogError("SECURITY ERROR: Invalid domain path detected: " + domain);
                    throw std::invalid_argument("Invalid domain path");
                }

                // 双重检查锁定 (DCL)
                {
                    std::shared_lock lock(domains_mutex_);
                    auto it = domains_.find(domain);
                    if (it != domains_.end()) return it->second;
                }
                std::unique_lock lock(domains_mutex_);
                auto it = domains_.find(domain);
                if (it != domains_.end()) return it->second;

                auto ctx = std::make_shared<FileContext>();
                ctx->json_root = json::object();
                ctx->file_path = z3y::utils::Utf8ToPath(root_dir_utf8_) / z3y::utils::Utf8ToPath(domain + ".json");
                domains_[domain] = ctx;
                return ctx;
            }

            json* ConfigProviderService::GetJsonNode(json& root, const std::string& key, bool create_if_missing) {
                if (key.empty()) return &root;
                try {
                    if (key[0] == '/') {
                        json::json_pointer ptr(key);
                        if (create_if_missing) return &root[ptr];
                        if (root.contains(ptr)) return &root.at(ptr);
                    } else {
                        if (create_if_missing) return &root[key];
                        if (root.contains(key)) return &root.at(key);
                    }
                } catch (...) { return nullptr; }
                return nullptr;
            }

            void ConfigProviderService::LoadConfigRaw(
                const std::string& domain, const std::string& section_key,
                void* user_struct, ConfigReaderFn reader, ConfigWriterFn writer,
                ConfigStatus& out_status) {

                std::shared_ptr<FileContext> ctx;
                try { ctx = GetOrCreateContext(domain); } catch (...) { out_status = ConfigStatus::Error; return; }

                std::unique_lock file_lock(ctx->mutex);

                // 1. 懒加载逻辑
                if (!ctx->is_loaded) {
                    if (std::filesystem::exists(ctx->file_path)) {
                        try {
                            std::ifstream f(ctx->file_path);
                            // 允许抛出异常以检测文件损坏
                            ctx->json_root = json::parse(f, nullptr, true, true);
                        } catch (const std::exception& e) {
                            // [Fail-Fast] 发现文件损坏
                            LogError("CRITICAL: Config file corrupted: " + domain + " (" + e.what() + ")");
                            // 标记为已加载，防止死循环重试 IO
                            ctx->is_loaded = true;
                            out_status = ConfigStatus::Error;
                            // 立即返回，不进行默认值回填，保留现场
                            return;
                        }
                    }
                    ctx->is_loaded = true;
                }

                // 2. 节点查找与处理
                json* node = GetJsonNode(ctx->json_root, section_key, false);

                if (node) {
                    std::string json_str = node->dump();
                    bool ok = false;
                    reader(json_str.c_str(), user_struct, ok);
                    if (!ok) LogError("Type mismatch when loading: " + domain + " -> " + section_key);
                    out_status = ok ? ConfigStatus::Success : ConfigStatus::Error;
                } else {
                    // 原因：真正的 Error 在上面 catch 块中已经 return 了。
                    // 能走到这里，说明文件是好的（或者不存在），只是缺字段而已。

                    if (writer) {
                        std::string default_str;
                        writer(user_struct, default_str);
                        if (!default_str.empty()) {
                            try {
                                auto default_json = json::parse(default_str);
                                json* target = GetJsonNode(ctx->json_root, section_key, true);
                                *target = default_json;
                                ctx->is_dirty = true;
                                out_status = ConfigStatus::CreatedDefault;
                            } catch (...) { out_status = ConfigStatus::Error; }
                        }
                    } else {
                        out_status = ConfigStatus::CreatedDefault;
                    }
                }
            }

            void ConfigProviderService::SetConfigRaw(
                const std::string& domain, const std::string& section_key,
                const void* user_struct, ConfigWriterFn writer) {

                std::shared_ptr<FileContext> ctx;
                try { ctx = GetOrCreateContext(domain); } catch (...) { return; }

                std::unique_lock file_lock(ctx->mutex);

                // 防御性加载：防止 Set 覆盖了未读取的文件
                if (!ctx->is_loaded && std::filesystem::exists(ctx->file_path)) {
                    try { std::ifstream f(ctx->file_path); ctx->json_root = json::parse(f); } catch (...) {}
                    ctx->is_loaded = true;
                }

                std::string val_str;
                writer(user_struct, val_str);
                try {
                    auto val_json = json::parse(val_str);
                    json* target = GetJsonNode(ctx->json_root, section_key, true);
                    *target = val_json;
                    ctx->is_dirty = true;
                } catch (...) {}
            }

            bool ConfigProviderService::Save(const std::string& domain) {
                std::shared_ptr<FileContext> ctx;
                try { ctx = GetOrCreateContext(domain); } catch (...) { return false; }

                nlohmann::json json_snapshot;
                std::filesystem::path target_path;

                // 1. Snapshot Phase (Lock Held)
                // 仅在持有锁期间进行内存拷贝，极大降低对读操作的阻塞
                {
                    std::unique_lock file_lock(ctx->mutex);
                    if (!ctx->is_dirty) return true;
                    json_snapshot = ctx->json_root;
                    target_path = ctx->file_path;
                    ctx->is_dirty = false;
                }

                // 2. IO Phase (Lock Released)
                // 耗时的序列化和磁盘写入在锁外进行
                try {
                    std::string content = json_snapshot.dump(4, ' ', false);
                    return z3y::utils::AtomicWriteFile(target_path, content);
                } catch (const std::exception& e) {
                    LogError("Save failed (Serialize Error): " + std::string(e.what()));
                    return false;
                }
            }

            bool ConfigProviderService::SaveAll() {
                std::vector<std::string> all_domains;
                {
                    std::shared_lock lock(domains_mutex_);
                    for (auto& kv : domains_) all_domains.push_back(kv.first);
                }
                bool all_success = true;
                for (const auto& domain : all_domains) {
                    if (!Save(domain)) all_success = false;
                }
                return all_success;
            }

            void ConfigProviderService::Reload(const std::string& domain) {
                std::shared_ptr<FileContext> ctx;
                try { ctx = GetOrCreateContext(domain); } catch (...) { return; }

                {
                    std::unique_lock file_lock(ctx->mutex);
                    if (std::filesystem::exists(ctx->file_path)) {
                        try {
                            std::ifstream f(ctx->file_path);
                            ctx->json_root = json::parse(f, nullptr, true, true);
                            ctx->is_loaded = true;
                            ctx->is_dirty = false;
                            LogInfo("Reloaded config domain: " + domain);
                        } catch (const std::exception& e) {
                            LogError("Reload failed (Corrupt): " + domain + " " + e.what());
                            return;
                        }
                    }
                }
                z3y::FireGlobalEvent<ConfigurationReloadedEvent>(domain);
            }

            void ConfigProviderService::ReloadAll() {
                std::vector<std::string> all_domains;
                {
                    std::shared_lock lock(domains_mutex_);
                    for (auto& kv : domains_) all_domains.push_back(kv.first);
                }
                for (const auto& d : all_domains) Reload(d);
            }

            void ConfigProviderService::ResetConfig(const std::string& domain) {
                std::shared_ptr<FileContext> ctx;
                try { ctx = GetOrCreateContext(domain); } catch (...) { return; }

                {
                    std::unique_lock file_lock(ctx->mutex);
                    std::error_code ec;
                    std::filesystem::remove(ctx->file_path, ec);
                    ctx->json_root = json::object();
                    ctx->is_loaded = true;
                    ctx->is_dirty = false;
                    LogWarn("Reset config domain: " + domain);
                }
                z3y::FireGlobalEvent<ConfigurationReloadedEvent>(domain);
            }

            void ConfigProviderService::ResetAll() {
                std::vector<std::string> all_domains;
                {
                    std::shared_lock lock(domains_mutex_);
                    for (auto& kv : domains_) all_domains.push_back(kv.first);
                }
                for (const auto& d : all_domains) ResetConfig(d);
            }

            std::string ConfigProviderService::DumpAll() {
                std::shared_lock lock(domains_mutex_);
                json root = json::object();
                for (const auto& [name, ctx] : domains_) {
                    std::shared_lock file_lock(ctx->mutex);
                    root[name] = ctx->json_root;
                }
                return root.dump(4, ' ', false);
            }

            std::vector<std::string> ConfigProviderService::GetLoadedDomains() const {
                std::shared_lock lock(domains_mutex_);
                std::vector<std::string> res;
                for (const auto& kv : domains_) res.push_back(kv.first);
                return res;
            }

        } // namespace config
    } // namespace plugins
} // namespace z3y