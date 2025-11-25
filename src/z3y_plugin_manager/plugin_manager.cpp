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
 * @file plugin_manager.cpp
 * @brief z3y::PluginManager 类的 Pimpl 核心实现。
 * @author Yue Liu
 * @date 2025-07-06
 * @copyright Copyright (c) 2025 Yue Liu
 *
 * @details
 * [受众：框架维护者]
 *
 * 此文件包含了 `PluginManager` 的核心逻辑，
 * 主要涉及：
 * 1. 构造、析构、Create() 工厂。
 * 2. `IPluginRegistry` 接口的实现 (注册组件)。
 * 3. `IPluginQuery` 接口的实现 (内省查询)。
 * 4. 插件加载/卸载 (`LoadPluginInternal`, `UnloadAllPlugins`)。
 * 5. Get/Create 服务的非模板化实现 (`GetServiceImpl`, `CreateInstanceImpl`)。
 *
 * `IEventBus` 相关的实现被分离到了 `event_bus_impl.cpp` 中。
 */

#include "plugin_manager_pimpl.h"  // Pimpl 私有头文件
#include <exception>   // 用于 std::exception_ptr, std::current_exception
#include <iostream>    // 用于 ReportException 的 std::cerr
#include <optional>    // C++17
#include <shared_mutex>  // C++17
#include <system_error>  // (用于 std::call_once)
#include "framework/z3y_utils.h"

 // 平台特定的 dlopen/dlerror
#ifndef _WIN32
#include <dlfcn.h>
#endif

namespace z3y {

    /**
     * @brief [内部] (OOB) 带锁的异常报告辅助函数。
     */
    void ReportException(PluginManagerPimpl* pimpl,
        const std::exception& e) {
        std::shared_ptr<PluginManagerPimpl::ExceptionCallback> handler_copy;
        {
            // 锁住异常处理器
            std::lock_guard lock(pimpl->exception_handler_mutex_);
            handler_copy = pimpl->exception_handler_;
        }
        if (handler_copy) {
            try {
                // 调用宿主提供的处理器
                (*handler_copy)(e);
            } catch (...) {
                // 异常处理器自己抛出了异常，这是严重错误
                std::cerr
                    << "[z3y FW] CRITICAL: Exception handler itself threw an exception."
                    << std::endl;
            }
        } else {
            // 宿主未提供处理器，打印到 stderr
            std::cerr << "[z3y FW] Unhandled plugin exception: " << e.what()
                << std::endl;
        }
    }

    /**
     * @brief [内部] (OOB)
     * 带锁的异常报告 (未知异常 ...)。
     */
    void ReportUnknownException(PluginManagerPimpl* pimpl) {
        static std::runtime_error unknown_err(
            "[z3y FW] Unknown non-std exception caught.");
        ReportException(pimpl, unknown_err);
    }

    // --- [Meyers Singleton] 静态实例管理 ---
    // [设计思想]
    // 这是 PluginManager 自身的单例实现。
    // 使用静态局部变量 (Meyers Singleton) 来保证 `s_instance`
    // 和 `s_mutex` 的初始化是线程安全的。
    namespace {
        PluginPtr<PluginManager>& GetStaticInstancePtr() {
            static PluginPtr<PluginManager> s_instance = nullptr;
            return s_instance;
        }
        std::mutex& GetStaticMutex() {
            static std::mutex s_mutex;
            return s_mutex;
        }
    }  // namespace

    // --- 静态函数实现 ---
    PluginPtr<PluginManager> PluginManager::GetActiveInstance() {
        std::lock_guard lock(GetStaticMutex());
        return GetStaticInstancePtr();
    }

    // 插件入口点 C 函数的类型别名
    using PluginInitFunc = void(IPluginRegistry*);

    // --- 构造函数 / 析构函数 (Pimpl 关键) ---

    /**
     * @brief [Pimpl] 构造函数
     */
    PluginManager::PluginManager()
        : pimpl_(std::make_unique<PluginManagerPimpl>()) {
        // 初始化 Pimpl 结构体
        pimpl_->running_ = true;
        pimpl_->current_added_components_ = nullptr;
        pimpl_->event_trace_hook_ = nullptr;
        pimpl_->exception_handler_ = nullptr;
    }

    /**
     * @brief [Pimpl] 析构函数 (必须在 .cpp 中实现)
     */
    PluginManager::~PluginManager() {
        // [阶段 1] 停止事件循环线程
        {
            std::lock_guard<std::mutex> lock(pimpl_->queue_mutex_);
            pimpl_->running_ = false;
        }
        pimpl_->queue_cv_.notify_one();
        if (pimpl_->event_loop_thread_.joinable()) {
            pimpl_->event_loop_thread_.join();
        }

        // [阶段 2] 清理所有资源
        ClearAllRegistries();

        // [变更说明]
        // 以前这里会检查 GetStaticInstancePtr() 并 reset。
        // 现在我们移除了这段逻辑。因为：
        // 1. 如果是通过 Destroy() 调用的析构，静态变量在 Destroy() 中已经被置空了。
        // 2. 如果是通过程序退出（静态析构）调用的，此时再访问静态变量属于 UB (Undefined Behavior)。
        // 结论：析构函数只负责清理 *自己* 的资源 (pimpl_)，不应干涉 *全局* 状态。
    }

    // --- 工厂函数 Create() ---
    PluginPtr<PluginManager> PluginManager::Create() {
        // [设计思想]
        // 构造函数是 protected 的，不能直接 std::make_shared。
        // 我们使用一个 "Enabler"
        // 结构体来绕过这个限制，同时确保对象在 shared_ptr 中创建。
        struct MakeSharedEnabler : public PluginManager {
            MakeSharedEnabler() : PluginManager() {}
        };
        PluginPtr<PluginManager> manager = std::make_shared<MakeSharedEnabler>();

        // 1. 注册为活动单例
        {
            std::lock_guard lock(GetStaticMutex());
            if (GetStaticInstancePtr()) {
                throw std::runtime_error(
                    "Attempted to set a second active PluginManager instance.");
            }
            GetStaticInstancePtr() = manager;
        }

        // 2. [关键] 注册框架的核心服务 (EventBus, PluginQuery)
        //    它们都 *指向* manager 自身。
        auto factory = []() -> PluginPtr<IComponent> {
            // 工厂函数简单地返回 manager
            // 实例
            if (auto strong_manager = PluginManager::GetActiveInstance()) {
                InstanceError dummy_error;
                // (需要 PluginCast 转换回 IComponent 基类)
                return PluginCast<IComponent>(strong_manager, dummy_error);
            }
            return nullptr;
            };
        auto iids = PluginManager::GetInterfaceDetails();

        // 注册 EventBus, PluginQuery, 和 Manager 自身
        manager->RegisterComponent(clsid::kEventBus, factory, true,
            "z3y.core.eventbus", iids, true);
        manager->RegisterComponent(clsid::kPluginQuery, factory, true,
            "z3y.core.pluginquery", iids, false);
        manager->RegisterComponent(PluginManager::kClsid, factory, true,
            "z3y.core.manager", iids, false);

        // 3. 启动事件循环 (异步) 线程
        manager->pimpl_->event_loop_thread_ =
            std::thread(&PluginManager::EventLoop, manager.get());

        // 4. 触发事件 (通知宿主 EventBus 已注册)
        try {
            auto bus = manager->GetService<IEventBus>(clsid::kEventBus);
            if (bus) {
                bus->FireGlobal<event::ComponentRegisterEvent>(
                    clsid::kEventBus, "z3y.core.eventbus", "internal.core", true);
            }
        } catch (const PluginException&) { /* 忽略 (不应发生) */
        }

        return manager;
    }

    // [核心实现] Destroy
// 作用：线程安全地、强制地销毁单例，重置环境。
    void PluginManager::Destroy() {
        PluginPtr<PluginManager> temp_holder;
        {
            // 1. 获取锁，防止与其他 Create/Destroy 竞争
            std::lock_guard<std::mutex> lock(GetStaticMutex());

            // 2. [关键技巧] 所有权转移 (Move Ownership)
            // 将智能指针的所有权从静态变量移动到局部变量 temp_holder。
            // 此时，GetStaticInstancePtr() 立即变为 nullptr。
            // 这保证了如果有其他线程此时调用 Create()，可以成功创建新实例，而不会抛出"重复创建"异常。
            temp_holder = std::move(GetStaticInstancePtr());
        }
        // 3. 释放锁
        // 必须在释放锁 *之后* 再销毁对象。
        // 因为 PluginManager 的析构函数可能会做很多事情（如停止线程），
        // 如果我们在持有锁的情况下析构，而析构过程又试图获取这把锁（虽然不应该，但要防万一），就会死锁。

        // 4. 触发析构
        // temp_holder 离开作用域自动析构，或者显式 reset。
        // 这会触发 PluginManager::~PluginManager()。
        temp_holder.reset();
    }

    // --- 公共 API 实现 (委托给 Pimpl) ---

    void PluginManager::SetEventTraceHook(EventTraceHook hook) {
        std::lock_guard lock(pimpl_->event_mutex_);
        pimpl_->event_trace_hook_ = std::move(hook);
    }

    void PluginManager::SetExceptionHandler(ExceptionCallback handler) {
        std::lock_guard lock(pimpl_->exception_handler_mutex_);
        pimpl_->exception_handler_ =
            std::make_shared<ExceptionCallback>(std::move(handler));
    }

    /**
     * @brief [内部]
     * 清理所有注册表和插件实例 (两阶段关闭)。
     */
    void PluginManager::ClearAllRegistries() {
        // --- [阶段 1] 收集并执行 Shutdown() ---
        //
        // [设计思想：两阶段关闭]
        // 这是安全卸载的核心。
        // 1. 阶段 1：调用 `Shutdown()`。
        //    我们必须先调用 *所有* 服务的 `Shutdown()` 函数， 
        //    此时所有服务实例 *仍然存活*，
        //    允许它们安全地释放对其他服务的引用。
        //
        // 2. 阶段 2：销毁 (析构)。
        //    在 `Shutdown()` 全部完成后，
        //    我们才开始销毁实例 (通过清空 `singletons_` map)。
        //
        // 3. 阶段 3：卸载 DLL。
        //    在所有 C++ 对象都销毁后，
        //    我们才能安全地调用 `FreeLibrary` / `dlclose`。
        //

        // (此 `shutdown_list` 持有所有单例的强引用)
        std::vector<PluginPtr<IComponent>> shutdown_list;

        {
            // [读锁]
            std::shared_lock lock(pimpl_->registry_mutex_);
            // [关键：LIFO 卸载]
            // (按 *相反* 的加载顺序遍历库)
            for (auto lib_it = pimpl_->loaded_libs_.rbegin();
                lib_it != pimpl_->loaded_libs_.rend(); ++lib_it) {
                const std::string& plugin_path = lib_it->first;
                // 查找此 DLL 注册了哪些组件
                auto plugin_comps_it = pimpl_->plugin_path_index_.find(plugin_path);
                if (plugin_comps_it == pimpl_->plugin_path_index_.end()) {
                    continue;
                }
                const auto& clsid_list = plugin_comps_it->second;
                // 遍历这些组件
                for (const ClassId& clsid : clsid_list) {
                    auto singleton_it = pimpl_->singletons_.find(clsid);
                    if (singleton_it == pimpl_->singletons_.end()) {
                        continue;  // 不是单例
                    }
                    PluginManagerPimpl::SingletonHolder& holder = singleton_it->second;
                    if (holder.instance) {
                        // [强引用]
                        // 添加到列表，确保在阶段 1 期间它不会被销毁
                        shutdown_list.push_back(holder.instance);
                    }
                }
            }
        }  // [读锁释放]

        // (无锁) 按 LIFO 顺序调用 Shutdown()
        for (const auto& instance : shutdown_list) {
            try {
                instance->Shutdown();
            } catch (const std::exception& e) {
                ReportException(pimpl_.get(), e);
            } catch (...) {
                static std::runtime_error shutdown_err(
                    "Unknown exception thrown from a plugin's Shutdown() method.");
                ReportException(pimpl_.get(), shutdown_err);
            }
        }
        // --- [阶段 1 结束] ---

        // --- [阶段 2] 销毁所有内部状态 ---
        {
            // [写锁] 获取所有锁，停止一切活动
            std::scoped_lock lock(pimpl_->registry_mutex_, pimpl_->event_mutex_,
                pimpl_->queue_mutex_);

            // 2a. 清空事件系统 (maps)
            pimpl_->event_queue_ = {};
            pimpl_->gc_queue_ = {};
            pimpl_->sender_subscribers_.clear();
            pimpl_->global_subscribers_.clear();
            pimpl_->global_sub_lookup_.clear();
            pimpl_->sender_sub_lookup_.clear();

            // 2b. [核心] 销毁单例实例
            //
            // 1. 清空 `shutdown_list` (局部变量)。
            //    `PluginPtr` 引用计数从 2 降为 1。
            shutdown_list.clear();

            // 2. 清空 `singletons_` map。
            //    `PluginPtr` 引用计数从 1 降为 0。
            //    **此时触发 C++ 析构函数**。
            //    **DLL 此时仍在内存中**。
            pimpl_->singletons_.clear();

            // 3. 清空其余注册表
            pimpl_->components_.clear();
            pimpl_->alias_map_.clear();
            pimpl_->default_map_.clear();
            pimpl_->current_loading_plugin_path_.clear();
            pimpl_->current_added_components_ = nullptr;
            pimpl_->interface_index_.clear();
            pimpl_->plugin_path_index_.clear();

            // 2c. 清空 Hook
            pimpl_->event_trace_hook_ = nullptr;
            pimpl_->exception_handler_ = nullptr;

            // 2d. [阶段 3] 卸载 DLL
            // **现在**，所有 C++ 对象都已安全销毁，
            // 我们可以卸载 DLL/SO。
            PlatformSpecificLibraryUnload();
            pimpl_->loaded_libs_.clear();
        }
        // --- [阶段 2/3 结束] ---
    }

    // --- IPluginRegistry 实现 ---
    void PluginManager::RegisterComponent(
        ClassId clsid, FactoryFunction factory, bool is_singleton,
        const std::string& alias,
        std::vector<InterfaceDetails> implemented_interfaces, bool is_default) {

        PluginPtr<IEventBus> bus; // 用于在锁外触发事件
        {
            // [写锁] 注册是一个写操作
            std::unique_lock lock(pimpl_->registry_mutex_);

            // 检查冲突
            if (pimpl_->components_.count(clsid)) {
                throw std::runtime_error("ClassId already registered.");
            }
            if (is_default) {
                // 检查默认实现冲突
                for (const auto& iface : implemented_interfaces) {
                    if (iface.iid == IComponent::kIid)
                        continue;
                    auto it = pimpl_->default_map_.find(iface.iid);
                    if (it != pimpl_->default_map_.end()) {
                        throw std::runtime_error("Default implementation conflict for interface " + iface.name);
                    }
                    // 注册为默认
                    pimpl_->default_map_[iface.iid] = clsid;
                }
            }
            // 1. 更新内省索引
            for (const auto& iface : implemented_interfaces) {
                pimpl_->interface_index_[iface.iid].push_back(clsid);
            }
            // 2. 更新路径索引
            pimpl_->plugin_path_index_[pimpl_->current_loading_plugin_path_]
                .push_back(clsid);

            // 3. 添加到主数据库
            pimpl_->components_[clsid] = { factory,
                                          is_singleton,
                                          alias,
                                          pimpl_->current_loading_plugin_path_,
                                          std::move(implemented_interfaces),
                                          is_default };
            // 4. 如果是单例，创建 SingletonHolder
            if (is_singleton) {
                pimpl_->singletons_[clsid].factory = pimpl_->components_[clsid].factory;
            }
            // 5. [事务] 添加到回滚列表
            if (pimpl_->current_added_components_) {
                pimpl_->current_added_components_->push_back(clsid);
            }
            // 6. 添加别名
            if (!alias.empty()) {
                pimpl_->alias_map_[alias] = clsid;
            }

            // 7. 获取 EventBus (用于在锁外触发)
            if (pimpl_->running_) {
                InstanceError dummy_error;
                bus = PluginCast<IEventBus>(this->shared_from_this(), dummy_error);
            }
        } // [写锁释放]

        // 8. (无锁) 触发注册事件
        if (bus) {
            bus->FireGlobal<event::ComponentRegisterEvent>(
                clsid, alias, pimpl_->current_loading_plugin_path_, is_singleton);
        }
    }

    // --- Get/Create 实现 ---

    /**
     * @brief [内部] CreateInstance 的非模板化核心
     */
    PluginPtr<IComponent> PluginManager::CreateInstanceImpl(const ClassId& clsid) {
        FactoryFunction factory;
        {
            // [读锁] 这是一个读操作
            std::shared_lock lock(pimpl_->registry_mutex_);
            auto it = pimpl_->components_.find(clsid);
            if (it == pimpl_->components_.end()) {
                throw PluginException(InstanceError::kErrorClsidNotFound);
            }
            if (it->second.is_singleton) {
                // 类型不匹配
                throw PluginException(InstanceError::kErrorNotAComponent,
                    "CLSID is a service, use GetService() instead.");
            }
            factory = it->second.factory;
        } // [读锁释放]

        // [无锁] 执行工厂
        auto base_obj = factory();
        if (!base_obj) {
            throw PluginException(InstanceError::kErrorFactoryFailed);
        }
        // [无锁] 调用生命周期钩子
        base_obj->Initialize();
        return base_obj;
    }

    /**
     * @brief [内部] GetService 的非模板化核心
     */
    PluginPtr<IComponent> PluginManager::GetServiceImpl(const ClassId& clsid) {
        PluginManagerPimpl::SingletonHolder* holder = nullptr;
        {
            // [读锁] 查找 holder 是一个读操作
            std::shared_lock lock(pimpl_->registry_mutex_);
            auto it = pimpl_->singletons_.find(clsid);
            if (it == pimpl_->singletons_.end()) {
                // 未找到。 检查它是否是一个组件。
                auto comp_it = pimpl_->components_.find(clsid);
                if (comp_it == pimpl_->components_.end()) {
                    throw PluginException(InstanceError::kErrorClsidNotFound);
                }
                if (!comp_it->second.is_singleton) {
                    throw PluginException(InstanceError::kErrorNotAService,
                        "CLSID is a component, use CreateInstance() "
                        "instead.");
                }
                // 这是一个内部错误（`components_` 和 `singletons_` 不一致）
                throw PluginException(InstanceError::kErrorInternal,
                    "Singleton registry inconsistent.");
            }
            holder = &(it->second);
        } // [读锁释放]

        // [核心：线程安全初始化]
        // `std::call_once` 保证了 `holder->flag`
        // 上的 lambda 仅被执行一次。
        //
        // [情景]
        // 1. 线程 A, B 同时调用 GetServiceImpl(clsid)。
        // 2. 它们都获取了 *同一个* `holder` 指针。
        // 3. 它们都调用 `std::call_once`。
        // 4. `std::call_once` (内部有锁) 保证只有一个线程（例如 A）会执行 lambda。
        // 5. 线程 B 将阻塞在 `std::call_once`， 直到线程 A 完成 lambda。
        // 6. 线程 A 执行 lambda， 创建实例，调用 `Initialize()`，并存储 `holder->instance`。
        // 7. 线程 A 返回。
        // 8. 线程 B 从 `std::call_once`
        // 返回（不执行 lambda），并继续。
        //
        std::call_once(holder->flag, [this, holder]() {
            try {
                // [Lambda 内部] 只有一个线程会执行这里
                holder->instance = holder->factory();
                if (!holder->instance) {
                    throw PluginException(InstanceError::kErrorFactoryFailed);
                }
                holder->instance->Initialize();
            } catch (...) {
                // [关键] 捕获构造或 Initialize()
                // 期间的异常
                holder->e_ptr = std::current_exception();
                holder->instance.reset();
            }
            });

        // [异常重抛]
        // 如果 lambda 失败了，`e_ptr` 会被设置。
        // 所有线程（包括第一个失败的线程和所有后续线程）
        // 都会在这里重新抛出该异常。
        if (holder->e_ptr) {
            std::rethrow_exception(holder->e_ptr);
        }
        return holder->instance;
    }

    std::optional<ClassId> PluginManager::GetDefaultClsidImpl(InterfaceId iid) {
        std::shared_lock lock(pimpl_->registry_mutex_);
        auto it = pimpl_->default_map_.find(iid);
        if (it == pimpl_->default_map_.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    std::optional<ClassId> PluginManager::GetClsidFromAlias(
        const std::string& alias) const {
        std::shared_lock lock(pimpl_->registry_mutex_);
        auto it = pimpl_->alias_map_.find(alias);
        if (it != pimpl_->alias_map_.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    // --- 插件加载/卸载实现 ---

    /**
     * @brief [内部] 回滚失败的插件加载
     */
    void PluginManager::RollbackRegistrations(
        const std::vector<ClassId>& clsid_list) {

        // [写锁]
        std::unique_lock lock(pimpl_->registry_mutex_);

        for (const ClassId clsid : clsid_list) {
            auto it = pimpl_->components_.find(clsid);
            if (it == pimpl_->components_.end())
                continue;

            const auto& info = it->second;
            // 1. 移除别名
            if (!info.alias.empty())
                pimpl_->alias_map_.erase(info.alias);
            // 2. 移除默认映射
            if (info.is_default_registration) {
                for (const auto& iface : info.implemented_interfaces) {
                    auto default_it = pimpl_->default_map_.find(iface.iid);
                    if (default_it != pimpl_->default_map_.end() &&
                        default_it->second == clsid) {
                        pimpl_->default_map_.erase(default_it);
                    }
                }
            }
            // 3. 移除接口索引
            for (const auto& iface : info.implemented_interfaces) {
                auto index_it = pimpl_->interface_index_.find(iface.iid);
                if (index_it != pimpl_->interface_index_.end()) {
                    auto& vec = index_it->second;
                    vec.erase(std::remove(vec.begin(), vec.end(), clsid), vec.end());
                }
            }
            // 4. 移除路径索引
            auto path_index_it =
                pimpl_->plugin_path_index_.find(info.source_plugin_path);
            if (path_index_it != pimpl_->plugin_path_index_.end()) {
                auto& vec = path_index_it->second;
                vec.erase(std::remove(vec.begin(), vec.end(), clsid), vec.end());
            }
            // 5. 移除单例（如果存在）
            pimpl_->singletons_.erase(clsid);
            // 6. 移除主数据库条目
            pimpl_->components_.erase(it);
        }
    }

    /**
     * @brief [内部] 加载单个插件的核心逻辑（事务性）
     */
    bool PluginManager::LoadPluginInternal(const std::filesystem::path& file_path,
        const std::string& init_func_name,
        std::string& out_error_message) {

        PluginPtr<IEventBus> bus;
        try {
            InstanceError dummy_error;
            bus = PluginCast<IEventBus>(this->shared_from_this(), dummy_error);
        } catch (const PluginException&) { /* 忽略 */
        }

        std::string path_str = z3y::utils::PathToUtf8(file_path);
        {
            // 检查是否已加载
            std::unique_lock lock(pimpl_->registry_mutex_);
            auto it = std::find_if(pimpl_->loaded_libs_.begin(),
                pimpl_->loaded_libs_.end(),
                [&path_str](const auto& pair) {
                    return pair.first == path_str;
                });
            if (it != pimpl_->loaded_libs_.end()) {
                return true; // 幂等性：已加载 = 成功
            }
        }

        // 1. [平台] 加载库
        LibHandle lib_handle = nullptr;
#ifdef _WIN32
        lib_handle = PlatformLoadLibrary(file_path);
        DWORD error_id = lib_handle ? 0 : ::GetLastError();
#else
        (void)dlerror(); // 清除旧的 dlerror 状态
        lib_handle = PlatformLoadLibrary(file_path);
#endif

        if (!lib_handle) {
            std::string platform_error = z3y::utils::GetLastSystemError();
            out_error_message = "PlatformLoadLibrary failed: " + platform_error;
            if (bus)
                bus->FireGlobal<event::PluginLoadFailureEvent>(path_str,
                    out_error_message);
            return false;
        }

        // 2. [平台] 查找入口点
        PluginInitFunc* init_func = reinterpret_cast<PluginInitFunc*>(
            PlatformGetFunction(lib_handle, init_func_name.c_str()));

        if (!init_func) {
            out_error_message = "PlatformGetFunction failed (Function '" + init_func_name +
                "' not found in plugin)";
            if (bus)
                bus->FireGlobal<event::PluginLoadFailureEvent>(path_str,
                    out_error_message);
            PlatformUnloadLibrary(lib_handle);
            return false;
        }

        // 3. [事务] 执行插件的注册
        std::vector<ClassId> added_components_this_session;
        try {
            {
                // 设置事务上下文
                std::unique_lock lock(pimpl_->registry_mutex_);
                pimpl_->current_loading_plugin_path_ = path_str;
                pimpl_->current_added_components_ = &added_components_this_session;
            }

            // [核心] 调用插件的 `z3yPluginInit(this)`
            init_func(this);

            {
                // 提交事务
                std::unique_lock lock(pimpl_->registry_mutex_);
                pimpl_->current_loading_plugin_path_ = "";
                pimpl_->current_added_components_ = nullptr;
                // 添加到已加载列表
                pimpl_->loaded_libs_.push_back({ path_str, lib_handle });
            }
            if (bus)
                bus->FireGlobal<event::PluginLoadSuccessEvent>(path_str);
            return true;
        } catch (const std::exception& e) {
            // [回滚] 捕获 `init_func` 中的异常
            {
                // 清理事务上下文
                std::unique_lock lock(pimpl_->registry_mutex_);
                pimpl_->current_loading_plugin_path_ = "";
                pimpl_->current_added_components_ = nullptr;
            }
            // 回滚此会话中添加的所有组件
            RollbackRegistrations(added_components_this_session);
            out_error_message =
                "Plugin init function threw exception: " + std::string(e.what());
            if (bus)
                bus->FireGlobal<event::PluginLoadFailureEvent>(path_str,
                    out_error_message);
            PlatformUnloadLibrary(lib_handle);
            return false;
        } catch (...) {
            // [回滚] 捕获未知异常
            {
                std::unique_lock lock(pimpl_->registry_mutex_);
                pimpl_->current_loading_plugin_path_ = "";
                pimpl_->current_added_components_ = nullptr;
            }
            RollbackRegistrations(added_components_this_session);
            out_error_message = "Unknown exception during plugin init function.";
            if (bus)
                bus->FireGlobal<event::PluginLoadFailureEvent>(path_str,
                    out_error_message);
            PlatformUnloadLibrary(lib_handle);
            return false;
        }
    }

    // 统一的文件检查实现
    bool PluginManager::IsPluginFile(const std::filesystem::path& path) const {
        // 1. 必须是普通文件
        if (!std::filesystem::is_regular_file(path)) {
            return false;
        }
        // 2. 检查后缀 (利用工具库获取当前平台的后缀)
        return path.extension() == z3y::utils::GetSharedLibraryExtension();
    }

    std::vector<std::string> PluginManager::LoadPluginsFromDirectory(
        const std::filesystem::path& dir, bool recursive,
        const std::string& init_func_name) {

        std::vector<std::string> load_failures;
        if (!std::filesystem::exists(dir) || !std::filesystem::is_directory(dir)) {
            load_failures.push_back("Directory not found or is not a directory: " +
                z3y::utils::PathToUtf8(dir));
            return load_failures;
        }

        std::string error_message;
        if (recursive) {
            // 递归迭代
            for (const auto& entry :
                std::filesystem::recursive_directory_iterator(dir)) {
                if (IsPluginFile(entry.path())) {
                    error_message.clear();
                    if (!LoadPluginInternal(entry.path(), init_func_name, error_message)) {
                        load_failures.push_back("Failed to load '" + z3y::utils::PathToUtf8(entry.path()) +
                            "': " + error_message);
                    }
                }
            }
        } else {
            // 非递归迭代
            for (const auto& entry : std::filesystem::directory_iterator(dir)) {
                if (IsPluginFile(entry.path())) {
                    error_message.clear();
                    if (!LoadPluginInternal(entry.path(), init_func_name, error_message)) {
                        load_failures.push_back("Failed to load '" + z3y::utils::PathToUtf8(entry.path()) +
                            "': " + error_message);
                    }
                }
            }
        }
        return load_failures;
    }

    bool PluginManager::LoadPlugin(const std::filesystem::path& file_path,
        std::string& out_error_message,
        const std::string& init_func_name) {

        if (!IsPluginFile(file_path)) {
            out_error_message =
                "File is not a valid plugin file (e.g., wrong extension, or not a "
                "file).";
            return false;
        }
        return LoadPluginInternal(file_path, init_func_name, out_error_message);
    }

    void PluginManager::UnloadAllPlugins() {
        // 1. 清理所有注册表 (执行两阶段关闭)
        ClearAllRegistries();

        // 2. [关键]
        //    重新注册框架的核心服务，使框架返回到“干净”的初始状态。
        auto factory = []() -> PluginPtr<IComponent> {
            if (auto strong_manager = PluginManager::GetActiveInstance()) {
                InstanceError dummy_error;
                return PluginCast<IComponent>(strong_manager, dummy_error);
            }
            return nullptr;
            };
        auto iids = PluginManager::GetInterfaceDetails();
        RegisterComponent(clsid::kEventBus, factory, true, "z3y.core.eventbus", iids,
            true);
        RegisterComponent(clsid::kPluginQuery, factory, true, "z3y.core.pluginquery",
            iids, false);
        RegisterComponent(PluginManager::kClsid, std::move(factory), true,
            "z3y.core.manager", iids, false);

        // 3. 触发事件
        try {
            auto bus = GetService<IEventBus>(clsid::kEventBus);
            if (bus) {
                bus->FireGlobal<event::ComponentRegisterEvent>(
                    clsid::kEventBus, "z3y.core.eventbus", "internal.core", true);
            }
        } catch (const PluginException&) { /* 忽略 */
        }
    }

    // --- IPluginQuery 实现 ---
    // [设计思想]
    // 以下所有 `IPluginQuery`
    // 函数都只执行读操作。 它们使用 `std_shared_lock`
    // (读锁) 来保护 `pimpl_` 数据结构，允许高并发的只读访问。
    //

    std::vector<ComponentDetails> PluginManager::GetAllComponents() const {
        std::shared_lock lock(pimpl_->registry_mutex_);
        std::vector<ComponentDetails> details_list;
        details_list.reserve(pimpl_->components_.size());
        for (const auto& [clsid, info] : pimpl_->components_) {
            details_list.push_back(
                { clsid, info.alias, info.is_singleton, info.source_plugin_path,
                 info.is_default_registration, info.implemented_interfaces });
        }
        return details_list;
    }

    bool PluginManager::GetComponentDetails(ClassId clsid,
        ComponentDetails& out_details) const {
        std::shared_lock lock(pimpl_->registry_mutex_);
        auto it = pimpl_->components_.find(clsid);
        if (it == pimpl_->components_.end())
            return false;
        out_details = { it->first,
                       it->second.alias,
                       it->second.is_singleton,
                       it->second.source_plugin_path,
                       it->second.is_default_registration,
                       it->second.implemented_interfaces };
        return true;
    }

    bool PluginManager::GetComponentDetailsByAlias(
        const std::string& alias, ComponentDetails& out_details) const {
        // (GetClsidFromAlias 内部有自己的锁)
        std::optional<ClassId> clsid = GetClsidFromAlias(alias);
        if (!clsid)
            return false;
        return GetComponentDetails(*clsid, out_details);
    }

    std::vector<ComponentDetails> PluginManager::FindComponentsImplementing(
        InterfaceId iid) const {

        std::shared_lock lock(pimpl_->registry_mutex_);

        // [设计] 使用 `interface_index_` 进行 O(1) 查找
        auto index_it = pimpl_->interface_index_.find(iid);
        if (index_it == pimpl_->interface_index_.end()) {
            return {};
        }

        std::vector<ComponentDetails> details_list;
        const auto& clsid_list = index_it->second;
        details_list.reserve(clsid_list.size());

        // 从 CLSID 列表转换回 ComponentDetails 列表
        for (const ClassId& clsid : clsid_list) {
            auto it = pimpl_->components_.find(clsid);
            if (it != pimpl_->components_.end()) {
                details_list.push_back({ it->first,
                                        it->second.alias,
                                        it->second.is_singleton,
                                        it->second.source_plugin_path,
                                        it->second.is_default_registration,
                                        it->second.implemented_interfaces });
            }
        }
        return details_list;
    }

    std::vector<std::string> PluginManager::GetLoadedPluginFiles() const {
        std::shared_lock lock(pimpl_->registry_mutex_);
        std::vector<std::string> paths;
        paths.reserve(pimpl_->loaded_libs_.size());
        for (const auto& [path, handle] : pimpl_->loaded_libs_) {
            paths.push_back(path);
        }
        return paths;
    }

    std::vector<ComponentDetails> PluginManager::GetComponentsFromPlugin(
        const std::string& plugin_path) const {

        std::shared_lock lock(pimpl_->registry_mutex_);

        // [设计] 使用 `plugin_path_index_` 进行 O(1) 查找
        auto index_it = pimpl_->plugin_path_index_.find(plugin_path);
        if (index_it == pimpl_->plugin_path_index_.end()) {
            return {};
        }

        std::vector<ComponentDetails> details_list;
        const auto& clsid_list = index_it->second;
        details_list.reserve(clsid_list.size());

        // 从 CLSID 列表转换回 ComponentDetails 列表
        for (const ClassId& clsid : clsid_list) {
            auto it = pimpl_->components_.find(clsid);
            if (it != pimpl_->components_.end()) {
                details_list.push_back({ it->first,
                                        it->second.alias,
                                        it->second.is_singleton,
                                        it->second.source_plugin_path,
                                        it->second.is_default_registration,
                                        it->second.implemented_interfaces });
            }
        }
        return details_list;
    }

}  // namespace z3y