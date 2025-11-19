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
 * @file plugin_exceptions.h
 * @brief 定义框架的错误码枚举 InstanceError 和异常类 PluginException。
 * @author Yue Liu
 * @date 2025-06-08
 * @copyright Copyright (c) 2025 Yue Liu
 *
 * @details
 * [受众：所有人]
 *
 * 此文件定义了框架的标准错误处理机制：
 * 1. `z3y::InstanceError` (枚举)：用于 `Try...` API 的详细错误码。
 * 2. `z3y::PluginException` (异常)：用于 `Get...` API 的标准异常类型。
 */

#pragma once

#ifndef Z3Y_FRAMEWORK_PLUGIN_EXCEPTIONS_H_
#define Z3Y_FRAMEWORK_PLUGIN_EXCEPTIONS_H_

#include <map>          // (保留)
#include <stdexcept>    // 继承自 std::exception
#include <string>       // 用于错误信息
#include "framework/z3y_framework_api.h"  // Z3Y_FRAMEWORK_API 导出

namespace z3y {

    /**
     * @enum InstanceError
     * @brief [受众：插件开发者 和 框架使用者]
     *
     * 定义在获取/创建插件实例时可能发生的详细错误码。
     * 这是 `TryGet...` / `TryCreate...` API
     * (在 `z3y_service_locator.h` 中) 的核心返回类型，
     * 也用于填充 `PluginException`。
     */
    enum class InstanceError : uint32_t {
        /**
         * @brief 操作成功。
         * (例如 `TryGetDefaultService` 成功返回实例)。
         */
        kSuccess = 0,

        /**
         * @brief 错误：未找到别名 (Alias)。
         * (例如 `GetService("Logger.Unknown")`)。
         */
        kErrorAliasNotFound = 1,

        /**
         * @brief 错误：未找到 ClassId (CLSID)。
         * (CLSID 未在任何插件中注册)。
         */
        kErrorClsidNotFound = 2,

        /**
         * @brief 错误：请求的 CLSID 是一个瞬态组件 (Component)，但使用了 `GetService`。
         * @note [使用提示] 请改用 `CreateInstance`。
         */
        kErrorNotAService = 3,

        /**
         * @brief 错误：请求的 CLSID 是一个单例服务 (Service)，但使用了
         * `CreateInstance`。
         * @note [使用提示] 请改用 `GetService`。
         */
        kErrorNotAComponent = 4,

        /**
         * @brief 错误：插件的工厂函数 (Factory) 执行失败。
         * [受众：框架维护者]
         * (例如 `std::make_shared` 返回了 `nullptr`，或构造函数中抛出了异常)。
         */
        kErrorFactoryFailed = 5,

        /**
         * @brief 错误：`PluginCast` 失败，目标接口未实现。
         * (CLSID 存在，但它没有继承 `QueryInterfaceRaw` 所请求的 IID)。
         */
        kErrorInterfaceNotImpl = 6,

        /**
         * @brief 错误：接口主版本 (Major) 不匹配。
         * (例如 宿主请求 V2.0，但插件实现的是 V1.0)。
         * @note 这通常是破坏性 API 变更，不允许加载。
         */
        kErrorVersionMajorMismatch = 7,

        /**
         * @brief 错误：插件的接口次版本 (Minor) 过低。
         * (例如 宿主请求 V1.2，但插件实现的是 V1.1)。
         * @note 这表示插件太旧，缺少宿主所需的新功能。
         */
        kErrorVersionMinorTooLow = 8,

        /**
         * @brief 错误：框架内部错误。
         * (例如 PluginManager 尚未初始化或已被销毁，或出现意外逻辑)。
         */
        kErrorInternal = 9
    };

    /**
     * @brief 将 InstanceError 错误码转换为人类可读的字符串。
     *
     * [受众：插件开发者 和 框架使用者]
     * 用于日志记录和 `PluginException::what()`。
     *
     * @param[in] error 要转换的错误码。
     * @return 描述错误码的 C++ std::string。
     */
    inline std::string ResultToString(InstanceError error) {
        // [受众：框架维护者]
        // 使用 switch 效率高，且能被编译器检查（如果缺少枚举值）
        switch (error) {
        case InstanceError::kSuccess:
            return "kSuccess";
        case InstanceError::kErrorAliasNotFound:
            return "kErrorAliasNotFound (Alias not found)";
        case InstanceError::kErrorClsidNotFound:
            return "kErrorClsidNotFound (CLSID not found)";
        case InstanceError::kErrorNotAService:
            return "kErrorNotAService (Is a component, not a service)";
        case InstanceError::kErrorNotAComponent:
            return "kErrorNotAComponent (Is a service, not a component)";
        case InstanceError::kErrorFactoryFailed:
            return "kErrorFactoryFailed (Plugin factory failed)";
        case InstanceError::kErrorInterfaceNotImpl:
            return "kErrorInterfaceNotImpl (IID not implemented)";
        case InstanceError::kErrorVersionMajorMismatch:
            return "kErrorVersionMajorMismatch (Major version mismatch)";
        case InstanceError::kErrorVersionMinorTooLow:
            return "kErrorVersionMinorTooLow (Plugin version is too old)";
        case InstanceError::kErrorInternal:
            return "kErrorInternal";
        default:
            return "Unknown ErrorCode";
        }
    }

    /**
     * @class PluginException
     * @brief 框架的标准异常类型。
     *
     * [受众：插件开发者 和 框架使用者]
     *
     * 当 `Get...` 或 `Create...` API (非 `Try...` 版本) 失败时，
     * 会抛出此异常。
     *
     * [受众：框架维护者]
     * Z3Y_FRAMEWORK_API 确保此类可以安全地跨 DLL 边界 `catch`。
     */
    class Z3Y_FRAMEWORK_API PluginException : public std::exception {
    public:
        /**
         * @brief 构造插件异常。
         * @param[in] error 导致异常的 `InstanceError` 错误码。
         * @param[in] message 可选的附加上下文信息。
         */
        PluginException(InstanceError error, const std::string& message = "")
            : error_(error), message_(message) {
            // [受众：框架维护者] 预先格式化 what() 消息
            full_message_ = "[z3y::PluginException] ";
            if (!message_.empty()) {
                full_message_ += message_ + " (Reason: ";
            }
            full_message_ += ResultToString(error_);
            if (!message_.empty()) {
                full_message_ += ")";
            }
        }

        /**
         * @brief 获取 C 风格的错误描述字符串 (兼容 std::exception)。
         * @return `const char*` 形式的详细错误信息。
         */
        const char* what() const noexcept override;

        /**
         * @brief 获取导致此异常的原始 `InstanceError` 错误码。
         * @return InstanceError 枚举值。
         */
        InstanceError GetError() const noexcept;

    private:
        InstanceError error_;
        std::string message_;
        std::string full_message_;
    };

    // [受众：框架维护者]
    // 将 'what' 和 'GetError' 的实现内联在头文件中
    // 这对于跨 DLL 导出的类是推荐做法，
    // 避免了模板和内联函数相关的链接问题。

    inline const char* PluginException::what() const noexcept {
        return full_message_.c_str();
    }

    inline InstanceError PluginException::GetError() const noexcept {
        return error_;
    }

}  // namespace z3y

#endif  // Z3Y_FRAMEWORK_PLUGIN_EXCEPTIONS_H_