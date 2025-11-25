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
 * @brief PluginManager 实现：注册表、工厂、平台加载。
 */

#include "plugin_manager_pimpl.h"
#include <exception>
#include <iostream>
#include <optional>
#include <shared_mutex>
#include <system_error>
#include "framework/z3y_utils.h"

#ifndef _WIN32
#include <dlfcn.h>
#endif

namespace z3y {

    // ... (辅助函数 ReportException/ReportUnknownException，代码保持不变，已在其他地方定义过) ...
    void ReportException(PluginManagerPimpl* pimpl, const std::exception& e) {
        std::shared_ptr<PluginManagerPimpl::ExceptionCallback> handler_copy;
        {
            std::lock_guard lock(pimpl->exception_handler_mutex_);
            handler_copy = pimpl->exception_handler_;
        }
        if (handler_copy) {
            try { (*handler_copy)(e); } catch (...) { std::cerr << "[z3y FW] CRITICAL: Exception handler threw." << std::endl; }
        } else {
            std::cerr << "[z3y FW] Unhandled plugin exception: " << e.what() << std::endl;
        }
    }

    void ReportUnknownException(PluginManagerPimpl* pimpl) {
        static std::runtime_error unknown_err("[z3y FW] Unknown non-std exception caught.");
        ReportException(pimpl, unknown_err);
    }

    // --- 单例管理 ---

    namespace {
        // 静态单例指针
        PluginPtr<PluginManager>& GetStaticInstancePtr() {
            static PluginPtr<PluginManager> s_instance = nullptr;
            return s_instance;
        }
        // 保护单例创建销毁的锁
        std::mutex& GetStaticMutex() {
            static std::mutex s_mutex;
            return s_mutex;
        }
    }

    PluginPtr<PluginManager> PluginManager::GetActiveInstance() {
        std::lock_guard lock(GetStaticMutex());
        return GetStaticInstancePtr();
    }

    using PluginInitFunc = void(IPluginRegistry*);

    PluginManager::PluginManager() : pimpl_(std::make_unique<PluginManagerPimpl>()) {
        pimpl_->running_ = true;
        // 初始化各钩子为空
        pimpl_->current_added_components_ = nullptr;
        pimpl_->event_trace_hook_ = nullptr;
        pimpl_->exception_handler_ = nullptr;
    }

    PluginManager::~PluginManager() {
        // 1. 停止事件循环
        {
            std::lock_guard<std::mutex> lock(pimpl_->queue_mutex_);
            pimpl_->running_ = false;
        }
        pimpl_->queue_cv_.notify_one();
        if (pimpl_->event_loop_thread_.joinable()) {
            pimpl_->event_loop_thread_.join();
        }
        // 2. 清理所有插件
        ClearAllRegistries();
    }

    /**
     * @brief 创建单例。
     */
    PluginPtr<PluginManager> PluginManager::Create() {
        // 辅助类，用于 make_shared 访问私有构造函数
        struct MakeSharedEnabler : public PluginManager { MakeSharedEnabler() : PluginManager() {} };
        PluginPtr<PluginManager> manager = std::make_shared<MakeSharedEnabler>();

        {
            std::lock_guard lock(GetStaticMutex());
            if (GetStaticInstancePtr()) throw std::runtime_error("Double Create() detected.");
            GetStaticInstancePtr() = manager;
        }

        // 注册内置服务 (EventBus, PluginQuery, PluginManager自身)
        auto factory = []() -> PluginPtr<IComponent> {
            if (auto m = PluginManager::GetActiveInstance()) {
                InstanceError err; return PluginCast<IComponent>(m, err);
            }
            return nullptr;
            };
        auto iids = PluginManager::GetInterfaceDetails();

        manager->RegisterComponent(clsid::kEventBus, factory, true, "z3y.core.eventbus", iids, true);
        manager->RegisterComponent(clsid::kPluginQuery, factory, true, "z3y.core.pluginquery", iids, false);
        manager->RegisterComponent(PluginManager::kClsid, factory, true, "z3y.core.manager", iids, false);

        // 启动工作线程
        manager->pimpl_->event_loop_thread_ = std::thread(&PluginManager::EventLoop, manager.get());

        // 广播核心事件：框架已就绪
        try {
            auto bus = manager->GetService<IEventBus>(clsid::kEventBus);
            if (bus) bus->FireGlobal<event::ComponentRegisterEvent>(clsid::kEventBus, "z3y.core.eventbus", "internal.core", true);
        } catch (...) {}

        return manager;
    }

    void PluginManager::Destroy() {
        PluginPtr<PluginManager> temp;
        {
            std::lock_guard lock(GetStaticMutex());
            temp = std::move(GetStaticInstancePtr());
        }
        temp.reset(); // 触发析构函数
    }

    // ... (SetEventTraceHook, SetExceptionHandler 实现略，基本是简单的赋值) ...
    void PluginManager::SetEventTraceHook(EventTraceHook hook) {
        std::lock_guard<std::mutex> lock(pimpl_->subscriber_map_mutex_);
        pimpl_->event_trace_hook_ = std::move(hook);
    }

    void PluginManager::SetExceptionHandler(ExceptionCallback handler) {
        std::lock_guard lock(pimpl_->exception_handler_mutex_);
        pimpl_->exception_handler_ = std::make_shared<ExceptionCallback>(std::move(handler));
    }

    void PluginManager::ClearAllRegistries() {
        // 1. 先 Shutdown 所有单例 (逆序)
        std::vector<PluginPtr<IComponent>> shutdown_list;
        {
            std::shared_lock lock(pimpl_->registry_mutex_);
            // 逆序遍历加载的库，模拟 LIFO
            for (auto lib_it = pimpl_->loaded_libs_.rbegin(); lib_it != pimpl_->loaded_libs_.rend(); ++lib_it) {
                auto plugin_comps_it = pimpl_->plugin_path_index_.find(lib_it->first);
                if (plugin_comps_it == pimpl_->plugin_path_index_.end()) continue;
                for (const ClassId& clsid : plugin_comps_it->second) {
                    auto singleton_it = pimpl_->singletons_.find(clsid);
                    if (singleton_it != pimpl_->singletons_.end() && singleton_it->second.instance) {
                        shutdown_list.push_back(singleton_it->second.instance);
                    }
                }
            }
        }

        for (const auto& instance : shutdown_list) {
            try { instance->Shutdown(); } catch (const std::exception& e) { ReportException(pimpl_.get(), e); } catch (...) { ReportUnknownException(pimpl_.get()); }
        }

        // 2. 清空数据并卸载库
        {
            std::scoped_lock lock(
                pimpl_->registry_mutex_,
                pimpl_->subscriber_map_mutex_,
                pimpl_->queue_mutex_,
                pimpl_->gc_status_mutex_
            );

            // 重置所有容器
            pimpl_->event_queue_ = {};
            pimpl_->pending_gc_events_.clear();
            pimpl_->sender_subscribers_.clear();
            pimpl_->global_subscribers_.clear();
            pimpl_->global_sub_lookup_.clear();
            pimpl_->sender_sub_lookup_.clear();

            shutdown_list.clear(); // 释放单例引用
            pimpl_->singletons_.clear();
            pimpl_->components_.clear();
            pimpl_->alias_map_.clear();
            pimpl_->default_map_.clear();
            pimpl_->current_loading_plugin_path_.clear();
            pimpl_->current_added_components_ = nullptr;
            pimpl_->interface_index_.clear();
            pimpl_->plugin_path_index_.clear();

            pimpl_->event_trace_hook_ = nullptr;
            pimpl_->exception_handler_ = nullptr;

            PlatformSpecificLibraryUnload(); // 调用 FreeLibrary / dlclose
            pimpl_->loaded_libs_.clear();
        }
    }

    // ... (RegisterComponent, CreateInstanceImpl 等逻辑保持不变，省略部分重复代码) ...
    void PluginManager::RegisterComponent(ClassId clsid, FactoryFunction factory, bool is_singleton, const std::string& alias, std::vector<InterfaceDetails> implemented_interfaces, bool is_default) {
        PluginPtr<IEventBus> bus;
        {
            std::unique_lock lock(pimpl_->registry_mutex_);
            if (pimpl_->components_.count(clsid)) throw std::runtime_error("ClassId already registered.");
            if (is_default) {
                for (const auto& iface : implemented_interfaces) {
                    if (iface.iid == IComponent::kIid) continue;
                    if (pimpl_->default_map_.count(iface.iid)) throw std::runtime_error("Default conflict: " + iface.name);
                    pimpl_->default_map_[iface.iid] = clsid;
                }
            }
            for (const auto& iface : implemented_interfaces) pimpl_->interface_index_[iface.iid].push_back(clsid);
            pimpl_->plugin_path_index_[pimpl_->current_loading_plugin_path_].push_back(clsid);
            pimpl_->components_[clsid] = { factory, is_singleton, alias, pimpl_->current_loading_plugin_path_, std::move(implemented_interfaces), is_default };
            if (is_singleton) pimpl_->singletons_[clsid].factory = pimpl_->components_[clsid].factory;
            if (pimpl_->current_added_components_) pimpl_->current_added_components_->push_back(clsid);
            if (!alias.empty()) pimpl_->alias_map_[alias] = clsid;
            if (pimpl_->running_) { InstanceError err; bus = PluginCast<IEventBus>(shared_from_this(), err); }
        }
        if (bus) bus->FireGlobal<event::ComponentRegisterEvent>(clsid, alias, pimpl_->current_loading_plugin_path_, is_singleton);
    }

    PluginPtr<IComponent> PluginManager::CreateInstanceImpl(const ClassId& clsid) {
        FactoryFunction factory;
        {
            std::shared_lock lock(pimpl_->registry_mutex_);
            auto it = pimpl_->components_.find(clsid);
            if (it == pimpl_->components_.end()) throw PluginException(InstanceError::kErrorClsidNotFound);
            if (it->second.is_singleton) throw PluginException(InstanceError::kErrorNotAComponent);
            factory = it->second.factory;
        }
        auto obj = factory();
        if (!obj) throw PluginException(InstanceError::kErrorFactoryFailed);
        obj->Initialize();
        return obj;
    }

    PluginPtr<IComponent> PluginManager::GetServiceImpl(const ClassId& clsid) {
        PluginManagerPimpl::SingletonHolder* holder = nullptr;
        {
            std::shared_lock lock(pimpl_->registry_mutex_);
            auto it = pimpl_->singletons_.find(clsid);
            if (it == pimpl_->singletons_.end()) {
                if (pimpl_->components_.count(clsid)) throw PluginException(InstanceError::kErrorNotAService);
                throw PluginException(InstanceError::kErrorClsidNotFound);
            }
            holder = &(it->second);
        }
        std::call_once(holder->flag, [this, holder]() {
            try {
                holder->instance = holder->factory();
                if (!holder->instance) throw PluginException(InstanceError::kErrorFactoryFailed);
                holder->instance->Initialize();
            } catch (...) {
                holder->e_ptr = std::current_exception();
                holder->instance.reset();
            }
            });
        if (holder->e_ptr) std::rethrow_exception(holder->e_ptr);
        return holder->instance;
    }

    std::optional<ClassId> PluginManager::GetDefaultClsidImpl(InterfaceId iid) {
        std::shared_lock lock(pimpl_->registry_mutex_);
        auto it = pimpl_->default_map_.find(iid);
        if (it == pimpl_->default_map_.end()) return std::nullopt;
        return it->second;
    }

    std::optional<ClassId> PluginManager::GetClsidFromAlias(const std::string& alias) const {
        std::shared_lock lock(pimpl_->registry_mutex_);
        auto it = pimpl_->alias_map_.find(alias);
        if (it != pimpl_->alias_map_.end()) return it->second;
        return std::nullopt;
    }

    void PluginManager::RollbackRegistrations(const std::vector<ClassId>& clsid_list) {
        std::unique_lock lock(pimpl_->registry_mutex_);
        for (const ClassId clsid : clsid_list) {
            auto it = pimpl_->components_.find(clsid);
            if (it == pimpl_->components_.end()) continue;
            const auto& info = it->second;
            if (!info.alias.empty()) pimpl_->alias_map_.erase(info.alias);
            if (info.is_default_registration) {
                for (const auto& iface : info.implemented_interfaces) {
                    auto dit = pimpl_->default_map_.find(iface.iid);
                    if (dit != pimpl_->default_map_.end() && dit->second == clsid) pimpl_->default_map_.erase(dit);
                }
            }
            for (const auto& iface : info.implemented_interfaces) {
                auto& vec = pimpl_->interface_index_[iface.iid];
                vec.erase(std::remove(vec.begin(), vec.end(), clsid), vec.end());
            }
            auto& pvec = pimpl_->plugin_path_index_[info.source_plugin_path];
            pvec.erase(std::remove(pvec.begin(), pvec.end(), clsid), pvec.end());
            pimpl_->singletons_.erase(clsid);
            pimpl_->components_.erase(it);
        }
    }

    bool PluginManager::LoadPluginInternal(const std::filesystem::path& file_path, const std::string& init_func_name, std::string& out_error_message) {
        PluginPtr<IEventBus> bus;
        try { InstanceError err; bus = PluginCast<IEventBus>(shared_from_this(), err); } catch (...) {}
        std::string path_str = z3y::utils::PathToUtf8(file_path);
        {
            std::unique_lock lock(pimpl_->registry_mutex_);
            for (const auto& pair : pimpl_->loaded_libs_) {
                if (pair.first == path_str) return true;
            }
        }
        LibHandle handle = PlatformLoadLibrary(file_path);
        if (!handle) {
            out_error_message = "LoadLibrary failed: " + z3y::utils::GetLastSystemError();
            if (bus) bus->FireGlobal<event::PluginLoadFailureEvent>(path_str, out_error_message);
            return false;
        }
        PluginInitFunc* init_func = (PluginInitFunc*)PlatformGetFunction(handle, init_func_name.c_str());
        if (!init_func) {
            out_error_message = "Entry point not found: " + init_func_name;
            if (bus) bus->FireGlobal<event::PluginLoadFailureEvent>(path_str, out_error_message);
            PlatformUnloadLibrary(handle);
            return false;
        }
        std::vector<ClassId> added;
        try {
            {
                std::unique_lock lock(pimpl_->registry_mutex_);
                pimpl_->current_loading_plugin_path_ = path_str;
                pimpl_->current_added_components_ = &added;
            }
            init_func(this);
            {
                std::unique_lock lock(pimpl_->registry_mutex_);
                pimpl_->current_loading_plugin_path_ = "";
                pimpl_->current_added_components_ = nullptr;
                pimpl_->loaded_libs_.push_back({ path_str, handle });
            }
            if (bus) bus->FireGlobal<event::PluginLoadSuccessEvent>(path_str);
            return true;
        } catch (const std::exception& e) {
            {
                std::unique_lock lock(pimpl_->registry_mutex_);
                pimpl_->current_loading_plugin_path_ = "";
                pimpl_->current_added_components_ = nullptr;
            }
            RollbackRegistrations(added);
            out_error_message = "Init exception: " + std::string(e.what());
            if (bus) bus->FireGlobal<event::PluginLoadFailureEvent>(path_str, out_error_message);
            PlatformUnloadLibrary(handle);
            return false;
        } catch (...) {
            {
                std::unique_lock lock(pimpl_->registry_mutex_);
                pimpl_->current_loading_plugin_path_ = "";
                pimpl_->current_added_components_ = nullptr;
            }
            RollbackRegistrations(added);
            out_error_message = "Init unknown exception";
            if (bus) bus->FireGlobal<event::PluginLoadFailureEvent>(path_str, out_error_message);
            PlatformUnloadLibrary(handle);
            return false;
        }
    }

    bool PluginManager::IsPluginFile(const std::filesystem::path& path) const {
        return std::filesystem::is_regular_file(path) && path.extension() == z3y::utils::GetSharedLibraryExtension();
    }

    std::vector<std::string> PluginManager::LoadPluginsFromDirectory(const std::filesystem::path& dir, bool recursive, const std::string& init_func_name) {
        std::vector<std::string> failures;
        if (!std::filesystem::exists(dir) || !std::filesystem::is_directory(dir)) {
            failures.push_back("Invalid dir: " + z3y::utils::PathToUtf8(dir));
            return failures;
        }
        auto try_load = [&](const std::filesystem::path& p) {
            if (IsPluginFile(p)) {
                std::string err;
                if (!LoadPluginInternal(p, init_func_name, err)) failures.push_back(z3y::utils::PathToUtf8(p) + ": " + err);
            }
            };
        if (recursive) {
            for (const auto& e : std::filesystem::recursive_directory_iterator(dir)) try_load(e.path());
        } else {
            for (const auto& e : std::filesystem::directory_iterator(dir)) try_load(e.path());
        }
        return failures;
    }

    bool PluginManager::LoadPlugin(const std::filesystem::path& file_path, std::string& out_error_message, const std::string& init_func_name) {
        if (!IsPluginFile(file_path)) {
            out_error_message = "Not a valid plugin file";
            return false;
        }
        return LoadPluginInternal(file_path, init_func_name, out_error_message);
    }

    void PluginManager::UnloadAllPlugins() {
        ClearAllRegistries();
        auto factory = []() -> PluginPtr<IComponent> {
            if (auto m = PluginManager::GetActiveInstance()) {
                InstanceError err; return PluginCast<IComponent>(m, err);
            }
            return nullptr;
            };
        auto iids = PluginManager::GetInterfaceDetails();
        RegisterComponent(clsid::kEventBus, factory, true, "z3y.core.eventbus", iids, true);
        RegisterComponent(clsid::kPluginQuery, factory, true, "z3y.core.pluginquery", iids, false);
        RegisterComponent(PluginManager::kClsid, factory, true, "z3y.core.manager", iids, false);
        try {
            auto bus = GetService<IEventBus>(clsid::kEventBus);
            if (bus) bus->FireGlobal<event::ComponentRegisterEvent>(clsid::kEventBus, "z3y.core.eventbus", "internal.core", true);
        } catch (...) {}
    }

    // ... (GetAllComponents 等查询接口实现保持不变，此处为完整性应包含) ...
    std::vector<ComponentDetails> PluginManager::GetAllComponents() const {
        std::shared_lock lock(GetImpl()->registry_mutex_);
        std::vector<ComponentDetails> ret;
        ret.reserve(GetImpl()->components_.size());
        for (const auto& kv : GetImpl()->components_) {
            ret.push_back({ kv.first, kv.second.alias, kv.second.is_singleton, kv.second.source_plugin_path, kv.second.is_default_registration, kv.second.implemented_interfaces });
        }
        return ret;
    }

    bool PluginManager::GetComponentDetails(ClassId clsid, ComponentDetails& out) const {
        std::shared_lock lock(GetImpl()->registry_mutex_);
        auto it = GetImpl()->components_.find(clsid);
        if (it == GetImpl()->components_.end()) return false;
        out = { it->first, it->second.alias, it->second.is_singleton, it->second.source_plugin_path, it->second.is_default_registration, it->second.implemented_interfaces };
        return true;
    }

    bool PluginManager::GetComponentDetailsByAlias(const std::string& alias, ComponentDetails& out) const {
        auto clsid = GetClsidFromAlias(alias);
        if (!clsid) return false;
        return GetComponentDetails(*clsid, out);
    }

    std::vector<ComponentDetails> PluginManager::FindComponentsImplementing(InterfaceId iid) const {
        std::shared_lock lock(GetImpl()->registry_mutex_);
        std::vector<ComponentDetails> ret;
        auto it = GetImpl()->interface_index_.find(iid);
        if (it != GetImpl()->interface_index_.end()) {
            for (auto id : it->second) {
                auto cit = GetImpl()->components_.find(id);
                if (cit != GetImpl()->components_.end()) {
                    ret.push_back({ cit->first, cit->second.alias, cit->second.is_singleton, cit->second.source_plugin_path, cit->second.is_default_registration, cit->second.implemented_interfaces });
                }
            }
        }
        return ret;
    }

    std::vector<std::string> PluginManager::GetLoadedPluginFiles() const {
        std::shared_lock lock(GetImpl()->registry_mutex_);
        std::vector<std::string> ret;
        for (const auto& p : GetImpl()->loaded_libs_) ret.push_back(p.first);
        return ret;
    }

    std::vector<ComponentDetails> PluginManager::GetComponentsFromPlugin(const std::string& path) const {
        std::shared_lock lock(GetImpl()->registry_mutex_);
        std::vector<ComponentDetails> ret;
        auto it = GetImpl()->plugin_path_index_.find(path);
        if (it != GetImpl()->plugin_path_index_.end()) {
            for (auto id : it->second) {
                auto cit = GetImpl()->components_.find(id);
                if (cit != GetImpl()->components_.end()) {
                    ret.push_back({ cit->first, cit->second.alias, cit->second.is_singleton, cit->second.source_plugin_path, cit->second.is_default_registration, cit->second.implemented_interfaces });
                }
            }
        }
        return ret;
    }

} // namespace z3y