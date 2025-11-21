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
 * @file i_config_service.h
 * @brief [核心接口] 定义配置管理服务接口 (ABI Safe + Self Validation)。
 * @author Yue Liu
 * @date 2025-11-22
 *
 * @details
 * =============================================================================
 * [受众：使用者 (User Guide)]
 * =============================================================================
 *
 * 1. **基本概念**
 * - **Domain (配置域)**: 对应磁盘上的一个独立文件 (如 "plugin_net" -> "config/plugin_net.json")。
 * - **Section Key**: JSON 节点路径。支持 "/" 开头的 Pointer 语法 (如 "/Network/Port")。
 *
 * 2. **自校验机制 (Self-Validation)**
 * - 用户结构体可以定义 `bool Validate(std::string& err_msg)` 函数。
 * - 加载配置时，框架会自动检测并调用该函数。
 * - 若校验失败，`LoadConfig` 将返回 `ConfigStatus::Error` 且不修改配置。
 * - **注意**: 若存在嵌套结构体，父结构体必须显式调用子结构体的 `Validate`。
 *
 * 3. **手动保存原则**
 * - 修改配置 (`SetConfig`) 仅更新内存。
 * - 必须显式调用 `Save` 或 `SaveAll` 才会写入磁盘。
 * - 程序退出时 **不会** 自动保存（防止错误配置被意外持久化）。
 *
 * =============================================================================
 * [受众：维护者 (Maintainer Guide)]
 * =============================================================================
 *
 * [ABI 安全设计]
 * 本接口采用 **"模板-虚函数桥接 (Template-Virtual Bridge)"** 模式。
 * - **模板层 (Header)**: 处理类型转换 (Struct <-> JSON String) 和 自校验逻辑。
 * - **虚函数层 (DLL Boundary)**: 仅传递 `const char*` 和 `void*`，确保跨编译器/版本的二进制兼容性。
 */

#pragma once

#include "framework/z3y_define_interface.h"
#include "framework/event_helpers.h"
#include "framework/i_event_bus.h"
#include <string>
#include <vector>
#include <functional>
#include <type_traits> // for std::false_type, std::void_t
#include <nlohmann/json.hpp>

namespace z3y {
    namespace interfaces {
        namespace core {

            /**
             * @struct ConfigurationReloadedEvent
             * @brief [事件] 配置重载通知。
             * @details 当调用 `Reload` 或 `Reset` 时广播此事件。
             */
            struct ConfigurationReloadedEvent : public z3y::Event {
                Z3Y_DEFINE_EVENT(ConfigurationReloadedEvent, "z3y-evt-config-reload-E1");
                std::string domain; //!< 发生变更的配置域 (文件名)
                explicit ConfigurationReloadedEvent(std::string d) : domain(std::move(d)) {}
            };

            /**
             * @enum ConfigStatus
             * @brief 配置操作结果状态。
             */
            enum class ConfigStatus {
                Success,        //!< 成功读取。
                CreatedDefault, //!< 文件或节点不存在，已使用代码默认值回填。
                Error           //!< 失败：文件损坏、类型不匹配或校验未通过。
            };

            // ABI 安全的回调函数定义
            using ConfigReaderFn = void(*)(const char* json_str, void* user_struct, bool& success);
            using ConfigWriterFn = void(*)(const void* user_struct, std::string& string_out);

            // [SFINAE] 编译期检测结构体是否有 bool Validate(std::string&) 成员函数
            template <typename T, typename = void>
            struct has_validate : std::false_type {};

            template <typename T>
            struct has_validate<T, std::void_t<decltype(std::declval<T>().Validate(std::declval<std::string&>()))>> : std::true_type {};

            /**
             * @class IConfigManagerService
             * @brief 配置管理器服务接口。
             */
            class IConfigManagerService : public virtual IComponent {
            public:
                Z3Y_DEFINE_INTERFACE(IConfigManagerService, "z3y-core-IConfigManagerService-IID-C0000001", 1, 0);

                /**
                 * @brief 初始化服务。
                 * @param config_root_dir 配置文件根目录 (UTF-8)。
                 */
                virtual bool InitializeService(const std::string& config_root_dir) = 0;

                // ---------------------------------------------------------------------------
                // 模板 API (User Friendly)
                // ---------------------------------------------------------------------------

                /**
                 * @brief [核心 API] 加载配置 (Load or Register)。
                 * @tparam T 用户配置结构体。
                 * @param domain 配置域 (文件名，不带后缀)。
                 * @param section_key 节点路径。
                 * @param[in,out] out_config 输入默认值，输出读取值。
                 * @return ConfigStatus 状态码。
                 *
                 * @note **自校验机制**: 若 T 定义了 Validate 函数，加载后会自动调用。
                 * @warning **嵌套结构体**: 必须在父结构体的 Validate 中显式调用子结构体验证。
                 */
                template <typename T>
                ConfigStatus LoadConfig(const std::string& domain, const std::string& section_key, T& out_config) {
                    auto reader = MakeReader<T>();
                    auto writer = MakeWriter<T>();
                    ConfigStatus status = ConfigStatus::Error;

                    // 1. 创建临时对象，拷贝当前的默认值
                    T temp_config = out_config;

                    // 2. 加载到临时对象中
                    LoadConfigRaw(domain, section_key, &temp_config, reader, writer, status);

                    // 3. 结果判断与校验
                    if (status == ConfigStatus::Success || status == ConfigStatus::CreatedDefault) {
                        if constexpr (has_validate<T>::value) {
                            std::string err_msg;
                            // 对临时对象进行校验
                            if (!temp_config.Validate(err_msg)) {
                                LogValidationError(domain, err_msg);
                                return ConfigStatus::Error; // 返回错误，out_config 未被修改
                            }
                        }

                        // 4. 一切正常，提交事务 (Commit)
                        out_config = std::move(temp_config);
                    }

                    return status;
                }

                /**
                 * @brief [探测 API] 仅读取配置 (Peek)。
                 * @details 不会回填默认值，也不会标记为 Dirty。
                 */
                template <typename T>
                ConfigStatus PeekConfig(const std::string& domain, const std::string& section_key, T& out_config) {
                    auto reader = MakeReader<T>();
                    ConfigStatus status = ConfigStatus::Error;
                    // writer 传 nullptr，表示不回填默认值
                    LoadConfigRaw(domain, section_key, &out_config, reader, nullptr, status);
                    return status;
                }

                /**
                 * @brief [更新 API] 更新内存中的配置。
                 * @details 仅修改内存并标记为 Dirty，需手动调用 Save 落盘。
                 */
                template <typename T>
                void SetConfig(const std::string& domain, const std::string& section_key, const T& in_config) {
                    auto writer = MakeWriter<T>();
                    SetConfigRaw(domain, section_key, &in_config, writer);
                }

                // ---------------------------------------------------------------------------
                // 运维与调试 API
                // ---------------------------------------------------------------------------

                /**
                 * @brief 持久化指定域。
                 * @return true 写入成功; false IO错误。
                 */
                virtual bool Save(const std::string& domain) = 0;

                /**
                 * @brief 持久化所有脏域。
                 */
                virtual bool SaveAll() = 0;

                /**
                 * @brief [运维] 强制重载。
                 * @details 丢弃内存修改，强制重读磁盘文件，并广播事件。
                 */
                virtual void Reload(const std::string& domain) = 0;
                virtual void ReloadAll() = 0;

                /**
                 * @brief [运维] 恢复出厂设置。
                 * @details 删除磁盘文件，重置内存。
                 */
                virtual void ResetConfig(const std::string& domain) = 0;
                virtual void ResetAll() = 0;

                virtual std::string DumpAll() = 0;
                virtual std::vector<std::string> GetLoadedDomains() const = 0;

            protected:
                // --- ABI 边界方法 ---
                virtual void LoadConfigRaw(const std::string& domain, const std::string& section_key,
                    void* user_struct, ConfigReaderFn reader, ConfigWriterFn writer,
                    ConfigStatus& out_status) = 0;

                virtual void SetConfigRaw(const std::string& domain, const std::string& section_key,
                    const void* user_struct, ConfigWriterFn writer) = 0;

                /**
                 * @brief 记录校验错误日志。
                 * @details 通过虚函数路由回实现层，利用 LogService 记录中文日志。
                 */
                virtual void LogValidationError(const std::string& domain, const std::string& msg) = 0;

            private:
                // 生成 Reader 回调: JSON String -> User Struct
                template <typename T>
                static ConfigReaderFn MakeReader() {
                    return [](const char* str, void* user, bool& ok) {
                        try {
                            if (str && *str) {
                                auto j = nlohmann::json::parse(str);
                                if (!j.is_null()) {
                                    *static_cast<T*>(user) = j.get<T>();
                                    ok = true;
                                    return;
                                }
                            }
                        } catch (...) {}
                        ok = false;
                        };
                }

                // 生成 Writer 回调: User Struct -> JSON String
                template <typename T>
                static ConfigWriterFn MakeWriter() {
                    return [](const void* user, std::string& out) {
                        try {
                            nlohmann::json j = *static_cast<const T*>(user);
                            out = j.dump(); // 紧凑输出
                        } catch (...) {}
                        };
                }
            };

        } // namespace core
    } // namespace interfaces
} // namespace z3y