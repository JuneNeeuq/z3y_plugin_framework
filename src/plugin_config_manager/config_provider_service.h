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
 * @file config_provider_service.h
 * @brief [实现声明] 工业级配置管理服务实现类。
 * @author Yue Liu
 * @date 2025-11-22
 *
 * @details
 * =============================================================================
 * [维护者指南 (Maintainer Guide)]
 * =============================================================================
 *
 * 1. **架构核心: 基于域的文件管理**
 * - 系统维护一个 `map<Domain, FileContext>`。每个 Domain 对应一个文件，拥有一把独立的读写锁。
 * - **设计意图**: 插件 A 操作 `a.json` 不会阻塞插件 B 操作 `b.json`，最大化并发吞吐量。
 *
 * 2. **关键机制**
 * - **Fail-Fast**: JSON 解析失败时，标记为 Error 并停止处理，**绝对禁止**覆盖默认值，以保留事故现场。
 * - **Soft Dependency**: 尝试获取 LogManager。若失败，自动降级到控制台输出。
 * - **Snapshot Save**: `Save` 操作采用 "Copy-on-Write" 策略，仅在锁内拷贝内存，在锁外执行耗时 IO。
 */

#pragma once

#include "framework/z3y_define_impl.h"
#include "interfaces_core/i_config_service.h"
#include "interfaces_core/i_log_service.h"
#include "interfaces_core/z3y_log_macros.h"

#include <nlohmann/json.hpp>
#include <shared_mutex>
#include <map>
#include <vector>
#include <filesystem>

namespace z3y {
    namespace plugins {
        namespace config {

            using namespace z3y::interfaces::core;

            class ConfigProviderService : public PluginImpl<ConfigProviderService, IConfigManagerService> {
            public:
                Z3Y_DEFINE_COMPONENT_ID("z3y-core-CConfigProvider-UUID-C0000001");

                ConfigProviderService();
                /**
                 * @brief 析构函数。
                 * @note 在此处执行最后的 SaveAll 以防止数据丢失（作为最后一道防线）。
                 */
                ~ConfigProviderService() override;

                bool InitializeService(const std::string& config_root_dir) override;

                // --- 接口实现 ---
                void LoadConfigRaw(const std::string& domain, const std::string& section_key,
                    void* user_struct, ConfigReaderFn reader, ConfigWriterFn writer,
                    ConfigStatus& out_status) override;

                void SetConfigRaw(const std::string& domain, const std::string& section_key,
                    const void* user_struct, ConfigWriterFn writer) override;

                bool Save(const std::string& domain) override;
                bool SaveAll() override;

                void Reload(const std::string& domain) override;
                void ReloadAll() override;
                void ResetConfig(const std::string& domain) override;
                void ResetAll() override;

                std::string DumpAll() override;
                std::vector<std::string> GetLoadedDomains() const override;

            protected:
                void LogValidationError(const std::string& domain, const std::string& msg) override;

            private:
                /**
                 * @struct FileContext
                 * @brief 单个配置文件的运行时上下文。
                 */
                struct FileContext {
                    std::filesystem::path file_path;
                    nlohmann::json json_root;
                    bool is_loaded = false; //!< 懒加载标记
                    bool is_dirty = false;  //!< 脏标记 (内存已修改未落盘)
                    mutable std::shared_mutex mutex; //!< 文件级读写锁
                };

                std::shared_ptr<FileContext> GetOrCreateContext(const std::string& domain);
                nlohmann::json* GetJsonNode(nlohmann::json& root, const std::string& key, bool create_if_missing);

                /**
                 * @brief 安全路径检查。
                 * @return true 如果路径合法 (非绝对路径，且不包含 "..")。
                 */
                static bool IsSafePath(const std::string& domain);

                // 内部日志包装器 (处理 logger_ 为空的降级逻辑)
                void LogInfo(const std::string& msg);
                void LogError(const std::string& msg);
                void LogWarn(const std::string& msg);

                mutable std::shared_mutex domains_mutex_;
                std::map<std::string, std::shared_ptr<FileContext>> domains_;
                std::string root_dir_utf8_;

                // 软依赖的日志服务句柄 (可能为空)
                std::shared_ptr<ILogger> logger_;
            };

        } // namespace config
    } // namespace plugins
} // namespace z3y