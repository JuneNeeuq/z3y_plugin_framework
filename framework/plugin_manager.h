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
 * @brief [核心类] PluginManager 的定义。整个框架的控制中心。
 * @author Yue Liu
 * @date 2025
 *
 * @details
 * **文件作用：**
 * 这是一个上帝类 (God Class) 的头文件。
 * `PluginManager` 负责管理整个系统的方方面面：加载插件、管理服务单例、分发事件、提供反射查询等。
 *
 * **设计模式：**
 * 1. **单例模式 (Singleton)**: 全局只有一个 PluginManager 实例。
 * 2. **Pimpl 惯用法**: 使用 `std::unique_ptr<PluginManagerPimpl> pimpl_` 隐藏了所有复杂的成员变量。
 * 这样做的好处是：当框架内部数据结构改变时，不需要重新编译所有使用了这个头文件的插件（ABI 兼容性）。
 */

#pragma once

#ifndef Z3Y_FRAMEWORK_PLUGIN_MANAGER_H_
#define Z3Y_FRAMEWORK_PLUGIN_MANAGER_H_

#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

 // 引入所有需要的辅助头文件
#include "framework/component_helpers.h"
#include "framework/connection.h"
#include "framework/connection_type.h"
#include "framework/framework_events.h"
#include "framework/i_event_bus.h"
#include "framework/i_plugin_query.h"
#include "framework/i_plugin_registry.h"
#include "framework/plugin_cast.h"
#include "framework/plugin_exceptions.h"
#include "framework/plugin_impl.h"
#include "framework/z3y_framework_api.h"

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4251)
#endif

namespace z3y {

    // 前向声明 Pimpl 结构体，具体定义在 .cpp 文件中
    struct PluginManagerPimpl;

    /**
     * @enum EventTracePoint
     * @brief 事件追踪埋点。用于调试，告诉你事件走到了哪一步。
     */
    enum class EventTracePoint {
        kEventFired,         //!< 事件刚刚被 Fire 出来
        kDirectCallStart,    //!< 同步模式：开始调用回调函数
        kQueuedEntry,        //!< 异步模式：任务被放入队列
        kQueuedExecuteStart, //!< 异步模式：工作线程取出了任务，准备执行
        kQueuedExecuteEnd,   //!< 异步模式：任务执行完毕
    };

    /** @brief 追踪钩子函数类型。允许用户注册一个函数来监听上述埋点。 */
    using EventTraceHook = std::function<void(EventTracePoint, EventId, void*, const char*)>;

    /**
     * @class PluginManager
     * @brief 框架总管。
     *
     * @details
     * 这个类继承了三个接口：
     * 1. `IPluginRegistry`: 提供给插件的入口函数使用，用于注册组件。
     * 2. `IEventBus`: 提供事件订阅和发布功能。
     * 3. `IPluginQuery`: 提供查询当前系统状态的功能。
     */
    class Z3Y_FRAMEWORK_API PluginManager
        : public IPluginRegistry,
        public PluginImpl<PluginManager, IEventBus, IPluginQuery> {
    public:
        // 定义 PluginManager 自己的组件 ID
        Z3Y_DEFINE_COMPONENT_ID("z3y-core-plugin-manager-IMPL-UUID")

    private:
        /** @brief Pimpl 指针。所有私有数据成员都藏在这里面。 */
        std::unique_ptr<PluginManagerPimpl> pimpl_;

    public:
        using LibHandle = void*; // 动态库句柄 (Windows HMODULE / Linux void*)
        using ExceptionCallback = std::function<void(const std::exception&)>;

        virtual ~PluginManager();

        /**
         * @brief 获取当前的单例指针。
         * @return 如果框架已销毁，可能返回 nullptr。
         */
        [[nodiscard]] static std::shared_ptr<PluginManager> GetActiveInstance();

        /**
         * @brief [宿主专用] 启动框架。创建单例。
         */
        [[nodiscard]] static std::shared_ptr<PluginManager> Create();

        /**
         * @brief [宿主专用] 关闭框架。销毁单例。
         */
        static void Destroy();

        // 禁用拷贝和移动
        PluginManager(const PluginManager&) = delete;
        PluginManager& operator=(const PluginManager&) = delete;
        PluginManager(PluginManager&&) = delete;
        PluginManager& operator=(PluginManager&&) = delete;

        /**
         * @brief 加载指定目录下的所有插件。
         * @param dir 目录路径。
         * @param recursive 是否递归子目录。
         * @param init_func_name 插件入口函数名 (默认 "z3yPluginInit")。
         * @return 失败的插件列表 (文件名: 错误信息)。
         */
        [[nodiscard]] std::vector<std::string> LoadPluginsFromDirectory(
            const std::filesystem::path& dir, bool recursive = true,
            const std::string& init_func_name = "z3yPluginInit");

        /**
         * @brief 加载单个插件。
         * @param file_path 插件路径。
         * @param out_error_message 输出错误信息。
         * @return 成功返回 true。
         */
        [[nodiscard]] bool LoadPlugin(
            const std::filesystem::path& file_path, std::string& out_error_message,
            const std::string& init_func_name = "z3yPluginInit");

        /**
         * @brief 卸载所有插件。安全清理资源的入口。
         */
        void UnloadAllPlugins();

        /** @brief 设置调试用的事件追踪钩子。 */
        void SetEventTraceHook(EventTraceHook hook);

        /** @brief 设置“带外”(Out-of-Band) 异常处理器。处理异步回调中抛出的异常。 */
        void SetExceptionHandler(ExceptionCallback handler);

        // --- IEventBus 接口的公共部分 ---
        // 允许外界查询某个事件是否有订阅者 (用于优化性能)
        [[nodiscard]] bool IsGlobalSubscribed(EventId event_id) const override;
        [[nodiscard]] bool IsSenderSubscribed(const std::weak_ptr<void>& sender_id,
            EventId event_id) const override;

    protected:
        // 构造函数是 protected 的，强制用户使用 Create()
        PluginManager();

        // --- IPluginRegistry 接口实现 ---
        // 供 z3yPluginInit 调用
        void RegisterComponent(ClassId clsid, FactoryFunction factory,
            bool is_singleton, const std::string& alias,
            std::vector<InterfaceDetails> implemented_interfaces,
            bool is_default) override;

        // --- IEventBus 接口实现 ---
        void Unsubscribe(std::shared_ptr<void> subscriber) override;
        void Unsubscribe(std::shared_ptr<void> subscriber, EventId event_id,
            const std::weak_ptr<void>& sender_key) override;

        // --- IEventBus 内部虚函数实现 ---
        // 这些是 IEventBus 定义的底层操作，PluginManager 负责具体实现逻辑
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

        // --- IPluginQuery 接口实现 ---
        [[nodiscard]] std::vector<ComponentDetails> GetAllComponents() const override;
        [[nodiscard]] bool GetComponentDetails(ClassId clsid, ComponentDetails& out_details) const override;
        [[nodiscard]] bool GetComponentDetailsByAlias(const std::string& alias, ComponentDetails& out_details) const override;
        [[nodiscard]] std::vector<ComponentDetails> FindComponentsImplementing(InterfaceId iid) const override;
        [[nodiscard]] std::vector<std::string> GetLoadedPluginFiles() const override;
        [[nodiscard]] std::vector<ComponentDetails> GetComponentsFromPlugin(const std::string& plugin_path) const override;

    private:
        // --- 内部核心逻辑 ---
        [[nodiscard]] PluginPtr<IComponent> CreateInstanceImpl(const ClassId& clsid);
        [[nodiscard]] PluginPtr<IComponent> GetServiceImpl(const ClassId& clsid);
        [[nodiscard]] std::optional<ClassId> GetClsidFromAlias(const std::string& alias) const;
        [[nodiscard]] std::optional<ClassId> GetDefaultClsidImpl(InterfaceId iid);

        // 访问 Pimpl 的辅助函数
        PluginManagerPimpl* GetImpl() { return pimpl_.get(); }
        const PluginManagerPimpl* GetImpl() const { return pimpl_.get(); }

        // 加载插件的底层实现
        [[nodiscard]] bool LoadPluginInternal(const std::filesystem::path& file_path,
            const std::string& init_func_name,
            std::string& out_error_message);
        [[nodiscard]] bool IsPluginFile(const std::filesystem::path& path) const;

        // --- 异步循环与 GC ---
        void EventLoop(); // 工作线程入口
        void ScheduleGC(EventId event_id); // 调度 GC
        void PerformGC(EventId event_id);  // 执行 GC

        void RollbackRegistrations(const std::vector<ClassId>& clsid_list); // 加载失败回滚
        void ClearAllRegistries(); // 彻底清理

        // --- 平台抽象层 (PAL) ---
        // 隔离 Windows/Linux API 差异
        [[nodiscard]] LibHandle PlatformLoadLibrary(const std::filesystem::path& path);
        [[nodiscard]] void* PlatformGetFunction(LibHandle handle, const char* func_name);
        void PlatformUnloadLibrary(LibHandle handle);
        void PlatformSpecificLibraryUnload();

    public:
        // --- 模板便捷 API (User Friendly) ---
        // 这些函数在头文件中实现，主要做类型转换，方便用户调用

        // 通过别名创建组件实例
        template <typename T>
        [[nodiscard]] PluginPtr<T> CreateInstance(const std::string& alias) {
            std::optional<ClassId> clsid = GetClsidFromAlias(alias);
            if (!clsid) throw PluginException(InstanceError::kErrorAliasNotFound, "Alias '" + alias + "' not found.");
            return CreateInstance<T>(*clsid);
        }

        // 通过 CLSID 创建组件实例
        template <typename T>
        [[nodiscard]] PluginPtr<T> CreateInstance(const ClassId& clsid) {
            auto base_obj = CreateInstanceImpl(clsid);
            InstanceError cast_result = InstanceError::kSuccess;
            PluginPtr<T> out_ptr = PluginCast<T>(base_obj, cast_result);
            if (cast_result != InstanceError::kSuccess) throw PluginException(cast_result, "PluginCast failed.");
            return out_ptr;
        }

        // 通过别名获取单例服务
        template <typename T>
        [[nodiscard]] PluginPtr<T> GetService(const std::string& alias) {
            std::optional<ClassId> clsid = GetClsidFromAlias(alias);
            if (!clsid) throw PluginException(InstanceError::kErrorAliasNotFound, "Alias '" + alias + "' not found.");
            return GetService<T>(*clsid);
        }

        // 通过 CLSID 获取单例服务
        template <typename T>
        [[nodiscard]] PluginPtr<T> GetService(const ClassId& clsid) {
            auto base_obj = GetServiceImpl(clsid);
            InstanceError cast_result = InstanceError::kSuccess;
            PluginPtr<T> out_ptr = PluginCast<T>(base_obj, cast_result);
            if (cast_result != InstanceError::kSuccess) throw PluginException(cast_result, "PluginCast failed for cached service.");
            return out_ptr;
        }

        // 获取默认的单例服务
        template <typename T>
        [[nodiscard]] PluginPtr<T> GetDefaultService() {
            static_assert(std::is_base_of_v<IComponent, T>, "T must derive from IComponent");
            std::optional<ClassId> default_clsid = GetDefaultClsidImpl(T::kIid);
            if (!default_clsid) throw PluginException(InstanceError::kErrorAliasNotFound, "No default impl");
            return GetService<T>(*default_clsid);
        }

        // 创建默认的组件实例
        template <typename T>
        [[nodiscard]] PluginPtr<T> CreateDefaultInstance() {
            static_assert(std::is_base_of_v<IComponent, T>, "T must derive from IComponent");
            std::optional<ClassId> default_clsid = GetDefaultClsidImpl(T::kIid);
            if (!default_clsid) throw PluginException(InstanceError::kErrorAliasNotFound, "No default impl");
            return CreateInstance<T>(*default_clsid);
        }
    };

}  // namespace z3y

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#endif  // Z3Y_FRAMEWORK_PLUGIN_MANAGER_H_