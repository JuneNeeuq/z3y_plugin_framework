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
 * @file plugin_manager_pimpl.h
 * @brief [私有] z3y::PluginManager 的 Pimpl (私有实现) 头文件。
 * @author Yue Liu
 * @date 2025-07-05
 * @copyright Copyright (c) 2025 Yue Liu
 *
 * @details
 * [受众：框架维护者]
 *
 * [设计思想：Pimpl 模式]
 * Pimpl ("Pointer to Implementation"，指向实现的指针)是一种 C++设计模式，
 * 它将一个类的所有私有成员变量和私有函数（即“实现”）
 * 隐藏在一个单独的结构体中，
 * 并通过一个 `std::unique_ptr` 指针在公共头文件 (`plugin_manager.h`) 中引用。
 *
 * **优点 (为什么使用 Pimpl)：**
 * 1. **编译防火墙 (ABI稳定性)：**
 * 只要 `plugin_manager.h`中的公共 API 不变， 我们可以 *任意* 修改此 `plugin_manager_pimpl.h`
 * 文件 (例如添加/删除成员变量)，
 * 而 *无需* 重新编译使用了 `plugin_manager.h` 的所有代码 (宿主、插件)。
 * 这对于 SDK 的二进制兼容性和编译速度至关重要。
 *
 * 2. **隐藏实现细节：**
 * 公共头文件 (`plugin_manager.h`) 只需包含 C++
 * 标准库的 `filesystem`, `string`, `vector` 等。
 * 而所有重量级的、平台特定的或内部的头文件(如 `Windows.h`, `dlfcn.h`,
 * `shared_mutex`, `map`, `thread`)
 * 都可以只被包含在 `.cpp` 和这个私有 `.h`文件中，
 * 保持了公共 API 的整洁。
 *
 * @warning 此文件是 `z3y_plugin_manager`
 * 库的内部文件，*绝不* 应该被宿主 (Host) 或插件 (Plugin) 包含。
 */

#pragma once

#ifndef Z3Y_SRC_PLUGIN_MANAGER_PIMPL_H_
#define Z3Y_SRC_PLUGIN_MANAGER_PIMPL_H_

#include <algorithm>
#include <condition_variable>
#include <exception>   // 用于 std::exception_ptr
#include <filesystem>  // C++17
#include <map>
#include <mutex>
#include <optional>     // C++17
#include <queue>
#include <set>
#include <shared_mutex>  // C++17 (读写锁)
#include <sstream>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <utility>  // 用于 std::pair
#include <vector>
 // [核心] 包含定义此 Pimpl 类的公共头文件
#include "framework/plugin_manager.h"

// 包含平台特定的动态库头文件
#ifdef _WIN32
#include <Windows.h>  // 用于 HMODULE, LoadLibrary 等
#else
#include <dlfcn.h>  // 用于 dlopen, dlsym 等
#endif

namespace z3y {

    /**
     * @struct PluginManagerPimpl
     * @brief [私有] PluginManager 的所有私有成员变量的集合。
     * @details
     * [受众：框架维护者]
     * 此结构体中的所有成员都可以从 `PluginManager`
     * 的成员函数中通过 `pimpl_->member`访问。
     */
    struct PluginManagerPimpl {
    public:
        /**
         * @struct ComponentInfo
         * @brief [内部]
         * 用于存储一个已注册组件的 *所有* 元数据。
         */
        struct ComponentInfo {
            FactoryFunction factory;  //!< 用于创建实例的工厂 lambda
            bool is_singleton;        //!< 是服务 (true) 还是组件 (false)
            std::string alias;        //!< 别名
            std::string source_plugin_path;  //!< 来源 DLL/SO 路径
            std::vector<InterfaceDetails> implemented_interfaces;  //!< 实现的接口列表
            bool is_default_registration;  //!< 是否为默认实现
        };

        /**
         * @struct SingletonHolder
         * @brief [内部]
         * 用于线程安全地管理单例服务的创建。
         *
         * [设计思想：线程安全的单例初始化]
         * 此结构体用于解决 `GetService`中的竞态条件。
         *
         * 1. `flag` (`std::once_flag`): 保证 `factory()`
         * 在所有线程中 *仅被执行一次*。
         * 2. `factory`: 构造函数 (从 `ComponentInfo`复制而来)。
         * 3. `instance`: 成功创建后的 `PluginPtr` 实例。
         * 4. `e_ptr` (`std::exception_ptr`): 如果 `factory()` 或 `Initialize()`
         * 抛出异常，
         * 该异常将被捕获并存储在此处。
         * 随后所有对 `GetService` 的调用都将 *重新抛出*
         * 此异常，实现“快速失败” (Fail-Fast)。
         */
        struct SingletonHolder {
            std::once_flag flag;          //!< C++11 "Call Once" 标志
            FactoryFunction factory;      //!< 构造实例的工厂
            PluginPtr<IComponent> instance;  //!< 缓存的实例指针
            std::exception_ptr e_ptr;     //!< 缓存的构造异常
        };

        /**
         * @struct Subscription
         * @brief [内部] 用于存储一个事件订阅的 *所有* 信息。
         */
        struct Subscription {
            std::weak_ptr<void> subscriber_id;  //!< 订阅者 (类型擦除的 weak_ptr)
            std::weak_ptr<void> sender_id;    //!< 发布者 (如果是 Sender 订阅)
            std::function<void(const Event&)> callback;  //!< 类型擦除的回调函数
            ConnectionType connection_type;  //!< kDirect 或 kQueued
        };

        // --- [受众：框架维护者] 类型别名 (用于事件总线) ---

        //! 事件 ID -> 该事件的所有订阅
        using EventCallbackList = std::vector<Subscription>;
        using EventMap = std::unordered_map<EventId, EventCallbackList>;
        /**
         * @brief [内部] 发布者 -> 事件 Map
         *
         * [设计思想：`std::map` 与 `std::owner_less`]
         *
         * 必须使用 `std::map` (红黑树) 而不是 `unordered_map` (哈希表)，
         * 因为 `std::weak_ptr` 没有标准的哈希函数 (`std::hash`)。
         *
         * 我们使用 `std::owner_less` 作为比较器，
         * 它比较的是 `weak_ptr` 指向的 *控制块* 的地址，
         * 这种比较方式即使在对象已销毁后仍然是稳定和有效的。
         */
        using SenderMap =
            std::map<std::weak_ptr<void>, EventMap,
            std::owner_less<std::weak_ptr<void>>>;

        //! 异步事件循环的任务队列 (一个 `void()` lambda)
        using EventTask = std::function<void()>;

        //! [内部] 用于 GC：订阅者 -> 它订阅的所有全局事件
        using SubscriberLookupMapG =
            std::map<std::weak_ptr<void>, std::set<EventId>,
            std::owner_less<std::weak_ptr<void>>>;

        //! [内部] 用于 GC：订阅者 -> 它订阅的所有 (发布者,事件) 对
        using SenderLookupKey = std::pair<std::weak_ptr<void>, EventId>;
        struct SenderLookupKeyLess {
            bool operator()(const SenderLookupKey& lhs,
                const SenderLookupKey& rhs) const {
                if (std::owner_less<std::weak_ptr<void>>()(lhs.first, rhs.first))
                    return true;
                if (std::owner_less<std::weak_ptr<void>>()(rhs.first, lhs.first))
                    return false;
                return lhs.second < rhs.second;
            }
        };
        using SubscriberLookupMapS =
            std::map<std::weak_ptr<void>, std::set<SenderLookupKey, SenderLookupKeyLess>,
            std::owner_less<std::weak_ptr<void>>>;

        // --- [受众：框架维护者] 核心成员变量 (组件注册表) ---

        /**
         * @brief [核心锁] 保护所有注册表 (components_, singletons_,
         * alias_map_ 等)。
         *
         * [设计思想：读写锁]
         * 这是一个 `std::shared_mutex` (读写锁)，用于优化性能：
         * - `GetService` / `CreateInstance` (高频) 只需要 **读锁**
         * (`std::shared_lock`)。
         * - `RegisterComponent` / `LoadPlugin` (低频) 需要 **写锁**
         * (`std::unique_lock`)。
         * 这允许多个线程 *同时* 调用 `GetService` 而不会相互阻塞。
         */
        std::shared_mutex registry_mutex_;

        //! CLSID -> ComponentInfo
        //! (框架中所有已注册组件的“主数据库”)
        std::unordered_map<ClassId, ComponentInfo> components_;
        //! CLSID -> SingletonHolder
        //! (单例实例的线程安全缓存)
        std::unordered_map<ClassId, SingletonHolder> singletons_;
        //! (使用 vector 保证 LIFO 卸载顺序)
        //! 插件路径 -> 已加载的库句柄 (HMODULE 或 void*)
        std::vector<std::pair<std::string, PluginManager::LibHandle>> loaded_libs_;
        //! 别名 (String) -> CLSID (用于快速查找)
        std::unordered_map<std::string, ClassId> alias_map_;
        //! 接口 IID -> CLSID (用于 `GetDefaultService`)
        std::unordered_map<InterfaceId, ClassId> default_map_;

        // --- [受众：框架维护者] 内省索引 ---
        //! 接口 IID -> 实现该接口的 CLSID 列表 (用于
        //! FindComponentsImplementing)
        std::unordered_map<InterfaceId, std::vector<ClassId>> interface_index_;
        //! 插件路径 -> 该插件注册的 CLSID 列表 (用于
        //! GetComponentsFromPlugin)
        std::unordered_map<std::string, std::vector<ClassId>> plugin_path_index_;

        // --- [受众：框架维护者] 插件加载事务 ---
        //! (临时变量) `LoadPlugin` 期间，指向当前正在加载的插件路径
        std::string current_loading_plugin_path_;
        //! (临时变量) `LoadPlugin` 期间，
        //! 指向一个 vector，用于收集此会话中添加的 CLSID (以便在失败时回滚)
        std::vector<ClassId>* current_added_components_ = nullptr;

        // --- [受众：框架维护者] 事件总线成员 ---
        /**
         * @brief [核心锁] 保护所有事件总线订阅列表。
         *
         * [设计思想：读写锁]
         * `std::shared_mutex` (读写锁)：
         * - `Fire...`: [高频] 获取 **读锁** (`std::shared_lock`)
         * 来复制回调列表。(注：`event_bus_impl.cpp`* 中实际使用了 *写锁*，以支持 Lazy GC)。
         * - `Subscribe...` / `Unsubscribe...`: [低频] 需要 **写锁**
         * (`std::unique_lock`) 来修改列表。
         */
        std::shared_mutex event_mutex_;

        //! 全局事件订阅
        EventMap global_subscribers_;
        //! 特定发布者事件订阅
        SenderMap sender_subscribers_;
        //! GC 反向查找表 (全局)
        SubscriberLookupMapG global_sub_lookup_;
        //! GC 反向查找表 (特定发布者)
        SubscriberLookupMapS sender_sub_lookup_;

        // --- [受众：框架维护者] 异步事件总线成员 ---
        std::thread event_loop_thread_;       //!< 事件循环工作线程
        std::queue<EventTask> event_queue_;   //!< 异步任务队列
        std::mutex queue_mutex_;              //!< *仅* 保护 `event_queue_` 和 `running_`
        std::condition_variable queue_cv_;    //!< 用于唤醒事件循环线程
        bool running_ = true;                 //!< `false` 时线程将退出
        //! 垃圾回收 (GC) 队列，用于解耦 `Unsubscribe` 和反向查找表的清理
        std::queue<std::weak_ptr<void>> gc_queue_;

        // --- [受众：框架维护者] 钩子 (Hooks) ---
        //! 事件追踪钩子 (用于调试)
        EventTraceHook event_trace_hook_ = nullptr;
        //! (OOB) 异常处理器
        using ExceptionCallback = std::function<void(const std::exception&)>;
        //! (使用 shared_ptr 确保回调在析构时仍然安全)
        std::shared_ptr<ExceptionCallback> exception_handler_ = nullptr;
        //! 保护 `exception_handler_` 的锁
        std::mutex exception_handler_mutex_;
    };

}  // namespace z3y

#endif  // Z3Y_SRC_PLUGIN_MANAGER_PIMPL_H_