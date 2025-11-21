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
 * @file plugin_manager.h
 * @brief [核心公共 API] 定义 z3y::PluginManager 类。
 * @author Yue Liu
 * @date 2025-07-05
 * @copyright Copyright (c) 2025 Yue Liu
 *
 * @details
 * [受众：框架使用者 (宿主 Host)]
 * `PluginManager` 是 z3y 插件框架的 *唯一* 核心类。
 * 这是宿主应用程序 (Host) 与框架交互的 *主入口点*。
 *
 * 你的 `main.cpp` 必须：
 * 1. 调用 `z3y::PluginManager::Create()` 来启动框架。
 * 2. 调用 `manager->LoadPluginsFromDirectory(...)` 来加载插件。
 * 3. (推荐) 调用 `manager->SetExceptionHandler(...)` 来捕获异步异常。
 * 4. 在退出前，调用 `manager->UnloadAllPlugins()` 来安全关闭。
 *
 * [受众：插件开发者]
 * **警告：你 *不* 应该直接 `#include` 或使用这个类。**
 *
 * 请优先使用 `z3y_define_impl.h` (它包含了你需要的一切)，
 * 并通过 `z3y::Get...` (服务定位器) 来获取服务。
 *
 * [受众：框架维护者]
 * 此类是框架的核心，它同时实现了 `IPluginRegistry`、 `IEventBus` 和
 * `IPluginQuery` 接口。
 *
 * [设计思想：Pimpl (指向实现的指针)]
 * `PluginManager` 使用 "Pimpl"
 * 模式，将所有私有成员（例如 `std::map`、`std::thread`）
 * 隐藏在 `PluginManagerPimpl` 结构体中（定义在 .cpp
 * 文件中）。
 *
 * 这提供了一个**编译防火墙**和**稳定的 ABI**
 * (应用程序二进制接口)。
 * 我们可以修改框架的内部实现（例如改变 `std::map` 为
 * `std::unordered_map`），
 * 而 *无需* 重新编译宿主或任何插件，
 * 只要此 `plugin_manager.h` 公共头文件不变。
 */

#pragma once

#ifndef Z3Y_FRAMEWORK_PLUGIN_MANAGER_H_
#define Z3Y_FRAMEWORK_PLUGIN_MANAGER_H_

 // --- 核心 API 头文件 ---
#include <filesystem>  // [C++17] (API 中使用)
#include <functional>  // [C++11] (API 中使用 std::function)
#include <memory>      // [C++11] (API 中使用 std::shared_ptr)
#include <mutex>       // [C++11] (API 中使用静态锁)
#include <optional>    // [C++17] (API 中使用)
#include <string>      // (API 中使用)
#include <vector>      // (API 中使用)
// --- 框架接口 ---
#include "framework/component_helpers.h"  // 依赖 Z3Y_DEFINE_COMPONENT_ID
#include "framework/connection.h"        // 依赖 Connection (IEventBus 返回值)
#include "framework/connection_type.h"   // 依赖 ConnectionType
#include "framework/framework_events.h"   // 依赖 Event (IEventBus)
#include "framework/i_event_bus.h"      // 继承 IEventBus
#include "framework/i_plugin_query.h"   // 继承 IPluginQuery
#include "framework/i_plugin_registry.h"  // 继承 IPluginRegistry
#include "framework/plugin_cast.h"        // 依赖 PluginCast (模板 API)
#include "framework/plugin_exceptions.h"  // 依赖 PluginException
#include "framework/plugin_impl.h"        // 继承 PluginImpl
#include "framework/z3y_framework_api.h"  // 依赖 Z3Y_FRAMEWORK_API

namespace z3y {

    /**
     * @brief [受众：框架维护者] Pimpl 前向声明。
     *
     * `PluginManagerPimpl` 结构体（在
     * `src/z3y_plugin_manager/plugin_manager_pimpl.h` 中定义）
     * 持有本类的所有私有成员变量。
     */
    struct PluginManagerPimpl;

    /**
     * @enum EventTracePoint
     * @brief [受众：框架使用者] (调试用) 事件追踪钩子的触发点。
     */
    enum class EventTracePoint {
        kEventFired,         //!< 事件被 `Fire...`
        //!< 调用
        kDirectCallStart,    //!< `kDirect`
        //!< 同步回调即将开始
        kQueuedEntry,        //!< `kQueued`
        //!< 异步事件被推入队列
        kQueuedExecuteStart,  //!< `EventLoop`
        //!< 线程即将执行异步回调
        kQueuedExecuteEnd,    //!< `EventLoop`
        //!< 线程已完成异步回调
    };
    /**
     * @brief [受众：框架使用者] (调试用) 事件追踪钩子的函数签名。
     */
    using EventTraceHook =
        std::function<void(EventTracePoint, EventId, void*, const char*)>;

    /**
     * @class PluginManager
     * @brief [核心] z3y 插件框架的管理器。
     *
     * [受众：框架使用者 (宿主 Host)]
     * 这是框架的中央控制塔。
     *
     * [受众：框架维护者]
     * 此类通过 `PluginImpl` 自动实现了 `IEventBus` 和 `IPluginQuery` 接口，
     * 并 *手动* 实现了 `IPluginRegistry` 接口。
     */
     // Z3Y_FRAMEWORK_API 确保此类从
     // z3y_plugin_manager.dll 导出/导入
    class Z3Y_FRAMEWORK_API PluginManager
        : public IPluginRegistry,
        public PluginImpl<PluginManager,
        IEventBus,       // [实现] 事件总线接口
        IPluginQuery> {  // [实现] 内省查询接口
    public:
        //! [受众：框架维护者]
        //! 定义 PluginManager 自身的 (实现) ClassId
        Z3Y_DEFINE_COMPONENT_ID("z3y-core-plugin-manager-IMPL-UUID")

    private:
        //! [受众：框架维护者]
        //! [Pimpl] 指向私有实现的唯一指针。
        std::unique_ptr<PluginManagerPimpl> pimpl_;

        //! [受众：框架维护者] 静态单例指针 (由 GetStaticInstancePtr() 包装)
        static PluginPtr<PluginManager> s_ActiveInstance;
        //! [受众：框架维护者] 保护 s_ActiveInstance 的互斥锁 (由GetStaticMutex()
        //! 包装)
        static std::mutex s_InstanceMutex;

    public:
        /**
         * @typedef LibHandle
         * @brief 平台特定的库句柄的类型擦除。
         * [受众：框架维护者]
         * 在 Windows 上是 `HMODULE`，在 POSIX 上是 `void*`。
         */
        using LibHandle = void*;

        /**
         * @typedef ExceptionCallback
         * @brief [受众：框架使用者] (OOB) 异步异常处理器的函数签名。
         * @see SetExceptionHandler
         */
        using ExceptionCallback = std::function<void(const std::exception&)>;

        /**
         * @brief [Pimpl 关键] 析构函数。
         *
         * [受众：框架维护者]
         *
         * 析构函数 *必须* 在 `.cpp`
         * 文件中实现（不能是默认的内联析构函数）。
         * 这是为了确保 `std::unique_ptr<PluginManagerPimpl>`
         * 在销毁时，`PluginManagerPimpl` 是一个完整类型（Complete Type），
         * 否则会导致编译错误。
         */
        virtual ~PluginManager();

        // --- 工厂函数 ---

        /**
         * @brief [核心 API] 获取全局激活的 PluginManager 单例实例。
         *
         * [受众：框架使用者 和 插件开发者]
         *
         * 这是 `z3y::Get...` 服务定位器函数（在 `z3y_service_locator.h` 中）
         * 内部调用的函数。
         *
         * @return `PluginPtr<PluginManager>`。 如果框架未初始化 (`Create()` 未被调用)，
         * 返回 `nullptr`。
         */
        [[nodiscard]] static PluginPtr<PluginManager> GetActiveInstance();

        /**
         * @brief [核心 API]
         * 创建并初始化一个新的 PluginManager 实例。
         *
         * [受众：框架使用者 (宿主 Host)]
         *
         * **这是宿主 (Host) 应用程序 *必须* 调用的第一个函数。**
         *
         * 它会：
         * 1. 创建 `PluginManager` 实例。
         * 2. 将其设置为全局 `s_ActiveInstance` 单例。
         * 3. 注册框架的核心服务 (IEventBus, IPluginQuery)
         * 4. 启动 `EventLoop` 工作线程（用于 `kQueued` 异步事件）。
         *
         * @return `PluginPtr<PluginManager>`。
         * @throws std::runtime_error 如果 `Create()` 被调用了两次。
         */
        [[nodiscard]] static PluginPtr<PluginManager> Create();

        /**
        * @brief [销毁] 显式销毁框架实例 (Safe Shutdown)。
        *
        * [设计思想 - 为什么需要这个函数？]
        * 1. **单元测试**: GTest 在同一进程运行多个测试用例。Test A 结束后必须彻底清理环境，
        * 否则 Test B 调用 Create() 时会报错。
        * 2. **软重启**: 服务端程序可能需要“不退进程、重载插件”。Destroy() 提供了这个能力。
        * 3. **确定性析构**: 依赖 C++ 静态变量析构顺序是不安全的（Static De-initialization Order Fiasco）。
        * 显式调用 Destroy() 确保在 main() 退出前，所有业务线程、文件句柄都已安全释放。
        */
        static void Destroy();

        // --- 禁用拷贝/移动 ---
        // [受众：框架维护者]
        // PluginManager 是一个单例，其Pimpl指针和内部状态
        // (如线程、锁) 使其不可拷贝或移动。
        PluginManager(const PluginManager&) = delete;
        PluginManager& operator=(const PluginManager&) = delete;
        PluginManager(PluginManager&&) = delete;
        PluginManager& operator=(PluginManager&&) = delete;

        // --- 插件加载 API ---

        /**
         * @brief [核心 API] 从指定目录加载所有插件 (DLL/SO)。
         *
         * [受众：框架使用者 (宿主 Host)]
         *
         * @param[in] dir 要搜索的目录路径。
         * @param[in] recursive 是否递归搜索子目录 (默认为 `true`)。
         * @param[in] init_func_name [受众：框架维护者]
         * 插件的 C 语言入口点函数名 (默认为`"z3yPluginInit"`)。
         *
         * @return `std::vector<std::string>` 包含所有加载 *失败*
         * 的插件及其错误信息。 如果 vector 为空， 表示全部成功。
         */
        [[nodiscard]] std::vector<std::string> LoadPluginsFromDirectory(
            const std::filesystem::path& dir, bool recursive = true,
            const std::string& init_func_name = "z3yPluginInit");

        /**
         * @brief [核心 API] 加载单个指定的插件文件。
         *
         * [受众：框架使用者 (宿主 Host)]
         *
         * @param[in] file_path 插件文件 (DLL/SO) 的完整路径。
         * @param[out] out_error_message 如果加载失败 (返回 `false`)，此字符串将被填充错误原因。
         * @param[in] init_func_name [受众：框架维护者]
         * 插件的 C 语言入口点函数名 (默认为 `"z3yPluginInit"`)。
         *
         * @return `true` 成功，`false` 失败。
         */
        [[nodiscard]] bool LoadPlugin(
            const std::filesystem::path& file_path, std::string& out_error_message,
            const std::string& init_func_name = "z3yPluginInit");

        /**
         * @brief [核心 API] 卸载所有插件并重置框架。
         *
         * [受众：框架使用者 (宿主 Host)]
         *
         * **这是宿主 (Host) 应用程序在退出前 *应该* 调用的函数。**
         *
         * [受众：框架维护者]
         * 此函数执行两阶段关闭 (Two-Phase Shutdown)：
         * 1. (阶段 1) 调用所有已创建单例的 `Shutdown()` 方法 (按 LIFO 顺序)。
         * 2. (阶段 2) 销毁所有单例实例 (析构)。
         * 3.    卸载所有 DLL/SO 句柄 (调用 `dlclose`/`FreeLibrary`)。
         * 4.    清空所有注册表。
         * 5.    重新注册核心服务 (EventBus, PluginQuery)， 使框架返回到“干净”
         * 状态。
         */
        void UnloadAllPlugins();

        // --- 配置 API ---

        /**
         * @brief [受众：框架使用者] (调试用) 设置事件追踪钩子。
         * @param[in] hook 一个 `std::function`，将在事件总线的关键点被调用。
         */
        void SetEventTraceHook(EventTraceHook hook);

        /**
         * @brief [核心 API]
         * 设置“带外”(Out-of-Band) 异步异常处理器。
         *
         * [受众：框架使用者 (宿主 Host)]
         *
         * **强烈建议宿主 (Host) 调用此函数。**
         *
         * 如果一个 `kQueued` (异步) 事件回调在 `EventLoop`
         * 工作线程中抛出异常，
         * `PluginManager` 会捕获该异常并调用此处理器。
         *
         * 这允许宿主记录该错误，而 *不会* 导致 `std::terminate` (程序崩溃)。
         *
         * @param[in] handler 异常处理回调。
         */
        void SetExceptionHandler(ExceptionCallback handler);

    protected:
        /**
         * @brief [受众：框架维护者]
         * 受保护的构造函数，只能由 `Create()` 静态工厂调用。
         */
        PluginManager();

        // --- [受众：框架维护者] 接口实现 (在 .cpp 中) ---

        // IPluginRegistry 接口实现 (由插件调用)
        void RegisterComponent(ClassId clsid, FactoryFunction factory,
            bool is_singleton, const std::string& alias,
            std::vector<InterfaceDetails> implemented_interfaces,
            bool is_default) override;

        // IEventBus 接口实现 (部分)
        void Unsubscribe(std::shared_ptr<void> subscriber) override;
        void Unsubscribe(std::shared_ptr<void> subscriber, EventId event_id,
            const std::weak_ptr<void>& sender_key) override;
        [[nodiscard]] bool IsGlobalSubscribed(EventId event_id) const override;
        [[nodiscard]] bool IsSenderSubscribed(const std::weak_ptr<void>& sender_id,
            EventId event_id) const override;

    protected:
        // [受众：框架维护者] IEventBus 纯虚函数实现
        [[nodiscard]] Connection SubscribeGlobalImpl(
            EventId event_id, std::weak_ptr<void> sub,
            std::function<void(const Event&)> cb,
            ConnectionType connection_type) override;
        void FireGlobalImpl(EventId event_id, PluginPtr<Event> e_ptr) override;
        [[nodiscard]] Connection SubscribeToSenderImpl(
            EventId event_id, std::weak_ptr<void> sub_id,
            std::weak_ptr<void> sender_id, std::function<void(const Event&)> cb,
            ConnectionType connection_type) override;
        virtual void FireToSenderImpl(const std::weak_ptr<void>& sender_id,
            EventId event_id,
            PluginPtr<Event> e_ptr) override;

        // [受众：框架维护者] IPluginQuery 纯虚函数实现 (const)
        [[nodiscard]] std::vector<ComponentDetails> GetAllComponents() const override;
        [[nodiscard]] bool GetComponentDetails(ClassId clsid, ComponentDetails& out_details) const override;
        [[nodiscard]] bool GetComponentDetailsByAlias(const std::string& alias, ComponentDetails& out_details) const override;
        [[nodiscard]] std::vector<ComponentDetails> FindComponentsImplementing(InterfaceId iid) const override;
        [[nodiscard]] std::vector<std::string> GetLoadedPluginFiles() const override;
        [[nodiscard]] std::vector<ComponentDetails> GetComponentsFromPlugin(const std::string& plugin_path) const override;

    private:
        // --- [受众：框架维护者] 内部辅助函数 (在 .cpp 中实现) ---

        // Get/Create 的非模板化核心实现
        [[nodiscard]] PluginPtr<IComponent> CreateInstanceImpl(const ClassId& clsid);
        [[nodiscard]] PluginPtr<IComponent> GetServiceImpl(const ClassId& clsid);
        [[nodiscard]] std::optional<ClassId> GetClsidFromAlias(const std::string& alias) const;
        [[nodiscard]] std::optional<ClassId> GetDefaultClsidImpl(InterfaceId iid);

        // Pimpl 访问器 (未使用，但 Pimpl 模式中很常见)
        PluginManagerPimpl* GetImpl() { return pimpl_.get(); }
        const PluginManagerPimpl* GetImpl() const { return pimpl_.get(); }

        // 插件加载核心
        [[nodiscard]] bool LoadPluginInternal(const std::filesystem::path& file_path,
            const std::string& init_func_name,
            std::string& out_error_message);
        // 事件循环线程函数
        void EventLoop();
        // 插件加载失败时的事务回滚
        void RollbackRegistrations(const std::vector<ClassId>& clsid_list);
        // UnloadAllPlugins 的核心实现
        void ClearAllRegistries();

        // 平台抽象层 (在 platform_win.cpp / platform_posix.cpp 中实现)
        [[nodiscard]] LibHandle PlatformLoadLibrary(
            const std::filesystem::path& path);
        [[nodiscard]] void* PlatformGetFunction(LibHandle handle,
            const char* func_name);
        void PlatformUnloadLibrary(LibHandle handle);
        void PlatformSpecificLibraryUnload();
        [[nodiscard]] bool PlatformIsPluginFile(const std::filesystem::path& path);
        // 平台特定的错误格式化
#ifdef _WIN32
        [[nodiscard]] std::string PlatformFormatError(unsigned long error_id);
#else
        [[nodiscard]] std::string PlatformGetError();
#endif

    public:
        // [受众：框架维护者]
        // 模板化的 Get/Create API
        //
        // 必须在头文件中定义 (因为它们是模板)。
        //
        // 这些是 `z3y_service_locator.h` 中全局辅助函数的 *实现*。
        // 插件开发者和宿主使用者 *不应* 直接调用 `manager->GetService(...)`，
        // 而应使用 `z3y::GetService(...)`。

        /**
         * @brief [模板 API] 通过别名创建瞬态组件。
         *
         * [受众：框架维护者]
         * 由 `z3y::CreateInstance(alias)` 调用。
         *
         * @tparam T 目标接口 (例如 `IDemoSimple`)。
         * @param[in] alias 别名 (例如 `"DemoSimple.B"`)。
         * @return `PluginPtr<T>`。
         * @throws PluginException 如果别名未找到、 不是组件、 或 `PluginCast<T>` 失败。
         */
        template <typename T>
        [[nodiscard]] PluginPtr<T> CreateInstance(const std::string& alias) {
            std::optional<ClassId> clsid = GetClsidFromAlias(alias);
            if (!clsid) {
                throw PluginException(InstanceError::kErrorAliasNotFound,
                    "Alias '" + alias + "' not found.");
            }
            return CreateInstance<T>(*clsid);
        }

        /**
         * @brief [模板 API] 通过 ClassId 创建瞬态组件。
         *
         * [受众：框架维护者]
         * 由 `z3y::CreateInstance(clsid)` 调用。
         *
         * @tparam T 目标接口 (例如 `IDemoSimple`)。
         * @param[in] clsid ClassId。
         * @return `PluginPtr<T>`。
         * @throws PluginException 如果 CLSID 未找到、 不是组件、 或 `PluginCast<T>` 失败。
         */
        template <typename T>
        [[nodiscard]] PluginPtr<T> CreateInstance(const ClassId& clsid) {
            // 1. 调用核心实现，获取 IComponent 基类
            auto base_obj = CreateInstanceImpl(clsid);
            // 2. [核心] 安全地类型转换到目标接口 T
            InstanceError cast_result = InstanceError::kSuccess;
            PluginPtr<T> out_ptr = PluginCast<T>(base_obj, cast_result);
            // 3. 检查转换是否失败 (例如 IID 不匹配或版本不匹配)
            if (cast_result != InstanceError::kSuccess) {
                throw PluginException(cast_result, "PluginCast failed.");
            }
            return out_ptr;
        }

        /**
         * @brief [模板 API] 通过别名获取单例服务。
         *
         * [受众：框架维护者]
         * 由 `z3y::GetService(alias)` 调用。
         *
         * @tparam T 目标接口 (例如 `IDemoLogger`)。
         * @param[in] alias 别名 (例如 `"Demo.Logger.Default"`)。
         * @return `PluginPtr<T>`。
         * @throws PluginException 如果别名未找到、不是服务、或 `PluginCast<T>`失败。
         */
        template <typename T>
        [[nodiscard]] PluginPtr<T> GetService(const std::string& alias) {
            std::optional<ClassId> clsid = GetClsidFromAlias(alias);
            if (!clsid) {
                throw PluginException(InstanceError::kErrorAliasNotFound,
                    "Alias '" + alias + "' not found.");
            }
            return GetService<T>(*clsid);
        }

        /**
         * @brief [模板 API] 通过 ClassId 获取单例服务。
         *
         * [受众：框架维护者]
         * 由 `z3y::GetService(clsid)` 调用。
         *
         * @tparam T 目标接口 (例如 `IEventBus`)。
         * @param[in] clsid ClassId (例如 `clsid::kEventBus`)。
         * @return `PluginPtr<T>`。
         * @throws PluginException 如果 CLSID 未找到、不是服务、 或 `PluginCast<T>`失败。
         */
        template <typename T>
        [[nodiscard]] PluginPtr<T> GetService(const ClassId& clsid) {
            // 1. 调用核心实现 (此函数负责线程安全的 `std::call_once` 初始化)
            auto base_obj = GetServiceImpl(clsid);
            // 2. 安全地类型转换
            InstanceError cast_result = InstanceError::kSuccess;
            PluginPtr<T> out_ptr = PluginCast<T>(base_obj, cast_result);
            // 3. 检查转换
            if (cast_result != InstanceError::kSuccess) {
                throw PluginException(cast_result,
                    "PluginCast failed for cached service.");
            }
            return out_ptr;
        }

        /**
         * @brief [模板 API] 获取接口的 *默认* 单例服务。
         *
         * [受众：框架维护者]
         * 由 `z3y::GetDefaultService<T>()` 调用。
         *
         * @tparam T 目标接口 (例如 `IDemoLogger`)。
         * @return `PluginPtr<T>`。
         * @throws PluginException 如果没有为 `T` 注册默认实现。
         */
        template <typename T>
        [[nodiscard]] PluginPtr<T> GetDefaultService() {
            static_assert(std::is_base_of_v<IComponent, T>,
                "T must derive from IComponent");
            InterfaceId iid = T::kIid;
            // 查找 IID -> CLSID 的映射
            std::optional<ClassId> default_clsid = GetDefaultClsidImpl(iid);
            if (!default_clsid) {
                throw PluginException(
                    InstanceError::kErrorAliasNotFound,
                    "No 'default' implementation was registered for interface " +
                    std::string(T::kName));
            }
            // 委托给 GetService(ClassId)
            return GetService<T>(*default_clsid);
        }

        /**
         * @brief [模板 API] 创建接口的 *默认* 瞬态组件。
         *
         * [受众：框架维护者]
         * 由 `z3y::CreateDefaultInstance<T>()` 调用。
         *
         * @tparam T 目标接口 (例如 `IDemoSimple`)。
         * @return `PluginPtr<T>` (新实例)。
         * @throws PluginException 如果没有为 `T` 注册默认实现。
         */
        template <typename T>
        [[nodiscard]] PluginPtr<T> CreateDefaultInstance() {
            static_assert(std::is_base_of_v<IComponent, T>,
                "T must derive from IComponent");
            InterfaceId iid = T::kIid;
            std::optional<ClassId> default_clsid = GetDefaultClsidImpl(iid);
            if (!default_clsid) {
                throw PluginException(
                    InstanceError::kErrorAliasNotFound,
                    "No 'default' implementation was registered for interface " +
                    std::string(T::kName));
            }
            // 委托给 CreateInstance(ClassId)
            return CreateInstance<T>(*default_clsid);
        }
    };

}  // namespace z3y

#endif  // Z3Y_FRAMEWORK_PLUGIN_MANAGER_H_