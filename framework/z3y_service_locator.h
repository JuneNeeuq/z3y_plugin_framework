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
 * @file z3y_service_locator.h
 * @brief [核心 API]
 * 提供全局辅助函数 (服务定位器)，用于在插件代码中方便地获取服务。
 * @author Yue Liu
 * @date 2025-06-29
 * @copyright Copyright (c) 2025 Yue Liu
 *
 * @details
 * [受众：插件开发者 和 框架使用者]
 *
 * 此文件提供了一组 *全局* 模板函数，实现了“服务定位器”(Service Locator)
 * 设计模式。
 *
 * 这是与框架交互的 **首选 API**。
 * 它隐藏了获取 `PluginManager` 实例的细节，
 * 提供了简单、全局的函数（如 `z3y::GetDefaultService`）。
 *
 * [API 分类]
 * 1. `Get...` / `Create...` (抛出异常 API):
 * - 用于插件的 *关键* 依赖路径（例如 `Initialize` 之后的业务逻辑中）。
 * - 如果服务获取失败 (例如未找到)，将抛出 `z3y::PluginException`。
 * - 优点：代码简洁，失败路径由上层 `try...catch` 统一处理。
 *
 * 2. `TryGet...` / `TryCreate...` (noexcept API):
 * - **[重要]** 用于 *不能* 抛出异常的上下文中
 * (例如插件的 `Shutdown()` 钩子、析构函数、或对可选依赖的处理)。
 * - 它们 *从不* 抛出异常 (标记为 `noexcept`)。
 * - 它们返回 `std::pair<PluginPtr<T>, InstanceError>`。
 * - 优点：健壮性高，允许安全地执行清理逻辑。
 *
 * [受众：框架维护者]
 * 此文件中的所有函数都是 `inline` 的。
 * 它们内部调用 `PluginManager::GetActiveInstance()` 来获取全局的 `PluginManager`
 * 单例， 然后调用该实例的模板成员函数 (例如 `manager->GetDefaultService<T>()`)。
 */

#pragma once

#ifndef Z3Y_FRAMEWORK_SERVICE_LOCATOR_H_
#define Z3Y_FRAMEWORK_SERVICE_LOCATOR_H_

#include <utility>  // 用于 std::pair, std::forward
#include "framework/i_event_bus.h"   // 依赖 IEventBus (用于事件辅助函数)
#include "framework/plugin_exceptions.h"  // 依赖 PluginException, InstanceError
#include "framework/plugin_manager.h"  // [核心] 依赖 PluginManager

namespace z3y {

    // --- [API 1: 抛出异常 (Throwing API)] ---
    // (适用于初始化之后的常规业务逻辑)

    /**
     * @brief [核心 API] 获取一个接口的 *默认* 单例服务。
     *
     * [受众：插件开发者 和 框架使用者]
     *
     * @tparam T 接口类型 (例如 `IDemoLogger`)。
     * @return `PluginPtr<T>`。
     * @throws PluginException
     * 如果 `PluginManager` 未激活，或没有注册 `T` 的默认实现
     * (kErrorAliasNotFound)。
     *
     * @example
     * \code{.cpp}
     * void MyClass::DoWork() {
     * // 如果 IDemoLogger 未注册为默认，这里将抛出异常
     * auto logger = z3y::GetDefaultService<IDemoLogger>();
     * logger->Log("Doing work...");
     * }
     * \endcode
     */
    template <typename T>
    [[nodiscard]] inline PluginPtr<T> GetDefaultService() {
        auto manager = PluginManager::GetActiveInstance();
        if (!manager) {
            // 明确处理 Manager 未激活的情况
            throw PluginException(InstanceError::kErrorInternal,
                "PluginManager is not active or has been destroyed.");
        }
        return manager->GetDefaultService<T>();
    }

    /**
     * @brief [核心 API] 通过 *别名* 获取一个单例服务。
     * @tparam T 接口类型 (例如 `IAdvancedLogger`)。
     * @param[in] alias 注册的别名 (例如 `"Logger.Advanced"`)。
     * @return `PluginPtr<T>`。
     * @throws PluginException 如果别名未找到、CLSID 不是服务、或类型转换失败。
     */
    template <typename T>
    [[nodiscard]] inline PluginPtr<T> GetService(const std::string& alias) {
        auto manager = PluginManager::GetActiveInstance();
        if (!manager) {
            throw PluginException(InstanceError::kErrorInternal,
                "PluginManager not active.");
        }
        return manager->GetService<T>(alias);
    }

    /**
     * @brief [核心 API] 通过 *ClassId* 获取一个单例服务。
     * @tparam T 接口类型 (例如 `IEventBus`)。
     * @param[in] clsid 注册的 ClassId (例如 `clsid::kEventBus`)。
     * @return `PluginPtr<T>`。
     * @throws PluginException 如果 CLSID 未找到、不是服务、或类型转换失败。
     */
    template <typename T>
    [[nodiscard]] inline PluginPtr<T> GetService(const ClassId& clsid) {
        auto manager = PluginManager::GetActiveInstance();
        if (!manager) {
            throw PluginException(InstanceError::kErrorInternal,
                "PluginManager not active.");
        }
        return manager->GetService<T>(clsid);
    }

    /**
     * @brief [核心 API] 创建一个接口的 *默认* 瞬态组件实例。
     * @tparam T 接口类型 (例如 `IDemoSimple`)。
     * @return `PluginPtr<T>` (一个 *新* 实例)。
     * @throws PluginException 如果没有注册 `T` 的默认实现。
     */
    template <typename T>
    [[nodiscard]] inline PluginPtr<T> CreateDefaultInstance() {
        auto manager = PluginManager::GetActiveInstance();
        if (!manager) {
            throw PluginException(InstanceError::kErrorInternal,
                "PluginManager not active.");
        }
        return manager->CreateDefaultInstance<T>();
    }

    /**
     * @brief [核心 API] 通过 *别名* 创建一个瞬态组件实例。
     * @tparam T 接口类型 (例如 `IDemoSimple`)。
     * @param[in] alias 注册的别名 (例如 `"DemoSimple.B"`)。
     * @return `PluginPtr<T>` (一个 *新* 实例)。
     * @throws PluginException 如果别名未找到、CLSID 不是组件、或类型转换失败。
     */
    template <typename T>
    [[nodiscard]] inline PluginPtr<T> CreateInstance(const std::string& alias) {
        auto manager = PluginManager::GetActiveInstance();
        if (!manager) {
            throw PluginException(InstanceError::kErrorInternal,
                "PluginManager not active.");
        }
        return manager->CreateInstance<T>(alias);
    }

    /**
     * @brief [核心 API] 通过 *ClassId* 创建一个瞬态组件实例。
     * @tparam T 接口类型。
     * @param[in] clsid 注册的 ClassId。
     * @return `PluginPtr<T>` (一个 *新* 实例)。
     * @throws PluginException
     * 如果 CLSID 未找到、不是组件、或类型转换失败。
     */
    template <typename T>
    [[nodiscard]] inline PluginPtr<T> CreateInstance(const ClassId& clsid) {
        auto manager = PluginManager::GetActiveInstance();
        if (!manager) {
            throw PluginException(InstanceError::kErrorInternal,
                "PluginManager not active.");
        }
        return manager->CreateInstance<T>(clsid);
    }

    // --- 事件总线辅助函数 (抛出异常) ---

    /**
     * @brief [便捷 API] 发布一个全局事件。
     *
     * [受众：插件开发者 和 框架使用者]
     *
     * @note [受众：框架维护者]
     * Fire 操作是“尽力而为”(best-effort)。
     * 如果 `PluginManager` 或 `IEventBus`
     * 在框架关闭期间未激活，此函数将静默失败 (不抛出)。
     *
     * @tparam TEvent 事件类型。
     * @tparam Args... 事件构造函数参数。
     */
    template <typename TEvent, typename... Args>
    inline void FireGlobalEvent(Args&&... args) {
        auto manager = PluginManager::GetActiveInstance();
        if (!manager) {
            return;  // 静默失败
        }
        try {
            auto bus = manager->GetService<IEventBus>(clsid::kEventBus);
            if (bus) {
                bus->FireGlobal<TEvent>(std::forward<Args>(args)...);
            }
        }
        catch (const PluginException&) {
            // 忽略异常 (例如，在关闭过程中 EventBus 服务已被销毁)
        }
    }

    /**
     * @brief [便捷 API] 订阅一个全局事件。
     *
     * [受众：插件开发者 和 框架使用者]
     *
     * 这是 `IEventBus::SubscribeGlobal` 的一个包装器。
     *
     * @return `z3y::Connection` 句柄。
     * @throws PluginException 如果 `IEventBus` 服务获取失败。
     */
    template <typename TEvent, typename TSubscriber, typename TCallback>
    [[nodiscard]] inline Connection SubscribeGlobalEvent(
        std::shared_ptr<TSubscriber> subscriber, TCallback&& callback,
        ConnectionType type = ConnectionType::kDirect) {
        auto manager = PluginManager::GetActiveInstance();
        if (!manager) {
            throw PluginException(InstanceError::kErrorInternal,
                "PluginManager not active.");
        }
        // [受众：框架维护者] 订阅是关键操作，必须获取 EventBus
        auto bus = manager->GetService<IEventBus>(clsid::kEventBus);
        return bus->SubscribeGlobal<TEvent>(subscriber,
            std::forward<TCallback>(callback), type);
    }

    /**
     * @brief [便捷 API] 取消一个订阅者的所有订阅。
     *
     * [受众：插件开发者]
     * **警告：这是一个重量级操作，应避免使用。**
     * 推荐使用 `z3y::ScopedConnection` 进行 RAII
     * 自动管理，或 `Connection::Disconnect()` 进行精确管理。
     */
    template <typename TSubscriber>
    inline void Unsubscribe(std::shared_ptr<TSubscriber> subscriber) {
        auto manager = PluginManager::GetActiveInstance();
        if (!manager)
            return;  // noexcept
        try {
            auto bus = manager->GetService<IEventBus>(clsid::kEventBus);
            if (bus) {
                bus->Unsubscribe(subscriber);
            }
        }
        catch (const PluginException&) {
            // 忽略异常
        }
    }

    // --- [API 2: 非抛出 (noexcept API)] ---
    // (适用于清理、析构函数和可选依赖)

    /**
     * @brief [核心 API] 尝试获取一个接口的 *默认* 单例服务。
     *
     * [受众：插件开发者]
     * **这是在 `Shutdown()` 钩子或析构函数中获取服务的 *唯一* 安全方式。**
     *
     * @tparam T 接口类型 (例如 `IDemoLogger`)。
     * @return `std::pair<PluginPtr<T>, InstanceError>`
     * - 成功: `{ Ptr, kSuccess }`
     * - 失败: `{ nullptr, kError... }`
     *
     * @example
     * \code{.cpp}
     * // 在 MyClass::Shutdown() 中
     * void MyClass::Shutdown() {
     * // 使用 C++17 结构化绑定
     * if (auto [logger, err] = z3y::TryGetDefaultService<IDemoLogger>();
     * err == z3y::InstanceError::kSuccess)
     * {
     * logger->Log("Shutting down safely.");
     * }
     * // 此处无需 try...catch
     * }
     * \endcode
     */
    template <typename T>
    [[nodiscard]] inline std::pair<PluginPtr<T>, InstanceError>
        TryGetDefaultService() noexcept {
        auto manager = PluginManager::GetActiveInstance();
        if (!manager) {
            return { nullptr, InstanceError::kErrorInternal };
        }
        try {
            // [受众：框架维护者]
            // 内部仍然调用 *抛出* 版本，但将其捕获并转换为错误码。
            return { manager->GetDefaultService<T>(), InstanceError::kSuccess };
        }
        catch (const PluginException& e) {
            return { nullptr, e.GetError() };
        }
        catch (...) {
            return { nullptr, InstanceError::kErrorInternal };
        }
    }

    /**
     * @brief [核心 API] 尝试通过 *别名* 获取一个单例服务。
     * (noexcept)
     * @tparam T 接口类型。
     * @param[in] alias 注册的别名。
     * @return `std::pair<PluginPtr<T>, InstanceError>`
     */
    template <typename T>
    [[nodiscard]] inline std::pair<PluginPtr<T>, InstanceError> TryGetService(
        const std::string& alias) noexcept {
        auto manager = PluginManager::GetActiveInstance();
        if (!manager) {
            return { nullptr, InstanceError::kErrorInternal };
        }
        try {
            return { manager->GetService<T>(alias), InstanceError::kSuccess };
        }
        catch (const PluginException& e) {
            return { nullptr, e.GetError() };
        }
        catch (...) {
            return { nullptr, InstanceError::kErrorInternal };
        }
    }

    /**
     * @brief [核心 API] 尝试通过 *ClassId* 获取一个单例服务。
     * (noexcept)
     * @tparam T 接口类型。
     * @param[in] clsid 注册的 ClassId。
     * @return `std::pair<PluginPtr<T>, InstanceError>`
     */
    template <typename T>
    [[nodiscard]] inline std::pair<PluginPtr<T>, InstanceError> TryGetService(
        const ClassId& clsid) noexcept {
        auto manager = PluginManager::GetActiveInstance();
        if (!manager) {
            return { nullptr, InstanceError::kErrorInternal };
        }
        try {
            return { manager->GetService<T>(clsid), InstanceError::kSuccess };
        }
        catch (const PluginException& e) {
            return { nullptr, e.GetError() };
        }
        catch (...) {
            return { nullptr, InstanceError::kErrorInternal };
        }
    }

    /**
     * @brief [核心 API] 尝试创建一个接口的 *默认* 瞬态组件实例。
     * (noexcept)
     * @tparam T 接口类型。
     * @return `std::pair<PluginPtr<T>, InstanceError>`
     */
    template <typename T>
    [[nodiscard]] inline std::pair<PluginPtr<T>, InstanceError>
        TryCreateDefaultInstance() noexcept {
        auto manager = PluginManager::GetActiveInstance();
        if (!manager) {
            return { nullptr, InstanceError::kErrorInternal };
        }
        try {
            return { manager->CreateDefaultInstance<T>(), InstanceError::kSuccess };
        }
        catch (const PluginException& e) {
            return { nullptr, e.GetError() };
        }
        catch (...) {
            return { nullptr, InstanceError::kErrorInternal };
        }
    }

    /**
     * @brief [核心 API] 尝试通过 *别名* 创建一个瞬态组件实例。
     * (noexcept)
     * @tparam T 接口类型。
     * @param[in] alias 注册的别名。
     * @return `std::pair<PluginPtr<T>, InstanceError>`
     */
    template <typename T>
    [[nodiscard]] inline std::pair<PluginPtr<T>, InstanceError> TryCreateInstance(
        const std::string& alias) noexcept {
        auto manager = PluginManager::GetActiveInstance();
        if (!manager) {
            return { nullptr, InstanceError::kErrorInternal };
        }
        try {
            return { manager->CreateInstance<T>(alias), InstanceError::kSuccess };
        }
        catch (const PluginException& e) {
            return { nullptr, e.GetError() };
        }
        catch (...) {
            return { nullptr, InstanceError::kErrorInternal };
        }
    }

    /**
     * @brief [核心 API] 尝试通过 *ClassId* 创建一个瞬态组件实例。
     * (noexcept)
     * @tparam T 接口类型。
     * @param[in] clsid 注册的 ClassId。
     * @return `std::pair<PluginPtr<T>, InstanceError>`
     */
    template <typename T>
    [[nodiscard]] inline std::pair<PluginPtr<T>, InstanceError> TryCreateInstance(
        const ClassId& clsid) noexcept {
        auto manager = PluginManager::GetActiveInstance();
        if (!manager) {
            return { nullptr, InstanceError::kErrorInternal };
        }
        try {
            return { manager->CreateInstance<T>(clsid), InstanceError::kSuccess };
        }
        catch (const PluginException& e) {
            return { nullptr, e.GetError() };
        }
        catch (...) {
            return { nullptr, InstanceError::kErrorInternal };
        }
    }

    // --- 事件总线辅助函数 (noexcept) ---

    /**
     * @brief [便捷 API] 尝试发布一个全局事件 (noexcept)。
     * @return `kSuccess` 如果成功发布， 或 `kError...` 如果 `IEventBus` 获取失败。
     */
    template <typename TEvent, typename... Args>
    inline InstanceError TryFireGlobalEvent(Args&&... args) noexcept {
        auto manager = PluginManager::GetActiveInstance();
        if (!manager)
            return InstanceError::kErrorInternal;
        try {
            auto bus = manager->GetService<IEventBus>(clsid::kEventBus);
            if (bus) {
                bus->FireGlobal<TEvent>(std::forward<Args>(args)...);
                return InstanceError::kSuccess;
            }
            return InstanceError::kErrorInternal;  // EventBus 服务不存在
        }
        catch (const PluginException& e) {
            return e.GetError();
        }
        catch (...) {
            return InstanceError::kErrorInternal;
        }
    }

    /**
     * @brief [便捷 API] 尝试订阅一个全局事件 (noexcept)。
     *
     * [受众：插件开发者 和 框架使用者]
     * 适用于不希望处理异常的订阅场景（例如在宿主初始化时）。
     *
     * @return `std::pair<Connection, InstanceError>`
     * - 成功: `{ ValidConnection, kSuccess }`
     * - 失败: `{ InvalidConnection, kError... }`
     *
     * @example
     * \code{.cpp}
     * // 在 HostEventListener::SubscribeToFrameworkEvents 中
     * if (auto [conn, err] =
     * z3y::TrySubscribeGlobalEvent<...>(...);
     * err != z3y::InstanceError::kSuccess)
     * {
     * std::cerr << "Failed to subscribe!" << std::endl;
     * return;
     * }
     * // 可以选择存储 conn
     * \endcode
     */
    template <typename TEvent, typename TSubscriber, typename TCallback>
    [[nodiscard]] inline std::pair<Connection, InstanceError>
        TrySubscribeGlobalEvent(std::shared_ptr<TSubscriber> subscriber,
            TCallback&& callback,
            ConnectionType type = ConnectionType::kDirect) noexcept {
        auto manager = PluginManager::GetActiveInstance();
        if (!manager) {
            return { z3y::Connection{}, InstanceError::kErrorInternal };
        }
        try {
            auto bus = manager->GetService<IEventBus>(clsid::kEventBus);
            if (!bus) {
                return { z3y::Connection{}, InstanceError::kErrorInternal };
            }
            return { bus->SubscribeGlobal<TEvent>(
                        subscriber, std::forward<TCallback>(callback), type),
                    InstanceError::kSuccess };
        }
        catch (const PluginException& e) {
            return { z3y::Connection{}, e.GetError() };
        }
        catch (...) {
            return { z3y::Connection{}, InstanceError::kErrorInternal };
        }
    }

    /**
     * @brief [便捷 API] 尝试取消一个订阅者的所有订阅 (noexcept)。
     * @return `kSuccess` 或 `kError...`。
     */
    template <typename TSubscriber>
    inline InstanceError TryUnsubscribe(
        std::shared_ptr<TSubscriber> subscriber) noexcept {
        auto manager = PluginManager::GetActiveInstance();
        if (!manager)
            return InstanceError::kErrorInternal;
        try {
            auto bus = manager->GetService<IEventBus>(clsid::kEventBus);
            if (bus) {
                bus->Unsubscribe(subscriber);
                return InstanceError::kSuccess;
            }
            return InstanceError::kErrorInternal;
        }
        catch (const PluginException& e) {
            return e.GetError();
        }
        catch (...) {
            return InstanceError::kErrorInternal;
        }
    }

}  // namespace z3y

#endif  // Z3Y_FRAMEWORK_SERVICE_LOCATOR_H_