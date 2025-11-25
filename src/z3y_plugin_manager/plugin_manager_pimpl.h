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
 * @brief [私有头文件] PluginManager 的内部数据结构定义。
 * @details
 * **设计思想 (Pimpl Idiom):**
 * 这个文件 *不* 会被暴露给插件开发者，只在框架内部编译。
 * 这样我们就可以随意修改 `PluginManagerPimpl` 里的 `std::map`、`std::vector` 等成员，
 * 而不需要担心破坏 ABI（二进制接口）兼容性，也不需要用户重新编译他们的插件。
 */

#pragma once

#ifndef Z3Y_SRC_PLUGIN_MANAGER_PIMPL_H_
#define Z3Y_SRC_PLUGIN_MANAGER_PIMPL_H_

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <exception>
#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
#include <queue>
#include <set>
#include <shared_mutex> 
#include <unordered_map>
#include <unordered_set> 
#include <utility>
#include <vector>

#include "framework/plugin_manager.h"

#ifdef _WIN32
#include <Windows.h>
#else
#include <dlfcn.h>
#endif

namespace z3y {

    /**
     * @struct PluginManagerPimpl
     * @brief PluginManager 的“肚子”。存放所有实际的数据。
     */
    struct PluginManagerPimpl {
    public:
        // --- 内部类型定义 ---

        /**
         * @brief 组件注册信息。
         * 存储了从 `RegisterComponent` 传入的所有元数据。
         */
        struct ComponentInfo {
            FactoryFunction factory;            //!< 创建对象的工厂函数 (lambda)
            bool is_singleton;                  //!< true=单例服务, false=瞬态组件
            std::string alias;                  //!< 别名 (如 "Demo.Logger")
            std::string source_plugin_path;     //!< 来源 DLL 的路径
            std::vector<InterfaceDetails> implemented_interfaces; //!< 实现了哪些接口
            bool is_default_registration;       //!< 是否是默认实现
        };

        /**
         * @brief 单例持有者。
         * 用于管理单例服务的生命周期。
         */
        struct SingletonHolder {
            std::once_flag flag;                //!< 保证初始化只执行一次 (std::call_once)
            FactoryFunction factory;            //!< 构造函数
            PluginPtr<IComponent> instance;     //!< 缓存的单例指针
            std::exception_ptr e_ptr;           //!< 如果构造失败，捕获异常以便再次抛出
        };

        /**
         * @brief 订阅条目。
         * 代表一个有效的事件订阅关系。
         */
        struct Subscription {
            std::weak_ptr<void> subscriber_id;  //!< 订阅者 ID (弱引用，防止循环引用)
            std::weak_ptr<void> sender_id;      //!< 关注的发送者 (可选)
            std::function<void(const Event&)> callback; //!< 回调闭包
            ConnectionType connection_type;     //!< 同步还是异步
            std::shared_ptr<std::atomic<bool>> active_token; //!< 原子票据 (核心机制)

            Subscription(std::weak_ptr<void> sub, std::weak_ptr<void> snd,
                std::function<void(const Event&)> cb, ConnectionType type,
                std::shared_ptr<std::atomic<bool>> token)
                : subscriber_id(std::move(sub)), sender_id(std::move(snd)),
                callback(std::move(cb)), connection_type(type),
                active_token(std::move(token)) {
            }
        };

        // 订阅列表。使用 shared_ptr 包装，为了实现 COW (Copy-On-Write)。
        // 当遍历列表发布事件时，如果有人修改列表，我们修改的是新副本，不影响遍历。
        using SubList = std::vector<Subscription>;
        using SubListPtr = std::shared_ptr<const SubList>;

        // 事件 ID -> 订阅列表
        using EventMap = std::unordered_map<EventId, SubListPtr>;
        using SenderEventMap = std::unordered_map<EventId, SubListPtr>;
        // 发送者 ID -> (事件 ID -> 订阅列表)
        // 使用 owner_less 来比较 weak_ptr
        using SenderMap = std::map<std::weak_ptr<void>, SenderEventMap, std::owner_less<std::weak_ptr<void>>>;

        /**
         * @brief 异步任务包。
         */
        struct EventTask {
            std::function<void()> func; //!< 要在工作线程执行的闭包
            EventId event_id = 0;       //!< 调试用的事件 ID
        };

        // 反向查找表类型定义 (用于快速 Unsubscribe)
        using SubscriberLookupMapG = std::map<std::weak_ptr<void>, std::set<EventId>, std::owner_less<std::weak_ptr<void>>>;
        using SenderLookupKey = std::pair<std::weak_ptr<void>, EventId>;
        struct SenderLookupKeyLess {
            bool operator()(const SenderLookupKey& lhs, const SenderLookupKey& rhs) const {
                if (std::owner_less<std::weak_ptr<void>>()(lhs.first, rhs.first)) return true;
                if (std::owner_less<std::weak_ptr<void>>()(rhs.first, lhs.first)) return false;
                return lhs.second < rhs.second;
            }
        };
        using SubscriberLookupMapS = std::map<std::weak_ptr<void>, std::set<SenderLookupKey, SenderLookupKeyLess>, std::owner_less<std::weak_ptr<void>>>;

        // --- 成员变量 (Data) ---

        /** @brief 注册表读写锁。保护 components_, singletons_ 等。 */
        mutable std::shared_mutex registry_mutex_;

        /** @brief 组件信息表 (ClassId -> Info)。 */
        std::unordered_map<ClassId, ComponentInfo> components_;
        /** @brief 单例缓存表 (ClassId -> Holder)。 */
        std::unordered_map<ClassId, SingletonHolder> singletons_;
        /** @brief 已加载的 DLL 列表 (路径 -> 句柄)。 */
        std::vector<std::pair<std::string, PluginManager::LibHandle>> loaded_libs_;
        /** @brief 别名索引 (Alias -> ClassId)。 */
        std::unordered_map<std::string, ClassId> alias_map_;
        /** @brief 默认实现索引 (InterfaceId -> ClassId)。 */
        std::unordered_map<InterfaceId, ClassId> default_map_;
        /** @brief 接口反查表 (InterfaceId -> [ClassId, ClassId...])。 */
        std::unordered_map<InterfaceId, std::vector<ClassId>> interface_index_;
        /** @brief 插件来源索引 (DLL路径 -> [ClassId...])。用于卸载时清理。 */
        std::unordered_map<std::string, std::vector<ClassId>> plugin_path_index_;

        /** @brief 加载上下文变量：当前正在加载哪个 DLL。 */
        std::string current_loading_plugin_path_;
        /** @brief 加载上下文变量：当前 DLL 注册了哪些组件 (失败回滚用)。 */
        std::vector<ClassId>* current_added_components_ = nullptr;

        // --- 事件总线数据 ---

        /** @brief 订阅表互斥锁。保护 global_subscribers_ 等。 */
        mutable std::mutex subscriber_map_mutex_;

        EventMap global_subscribers_;   //!< 全局订阅表
        SenderMap sender_subscribers_;  //!< 特定发送者订阅表
        SubscriberLookupMapG global_sub_lookup_; //!< 反查表：谁订阅了什么全局事件
        SubscriberLookupMapS sender_sub_lookup_; //!< 反查表：谁订阅了什么 Sender 事件

        // --- 异步线程与 GC ---
        std::thread event_loop_thread_; //!< 事件循环线程
        std::queue<EventTask> event_queue_; //!< 任务队列
        mutable std::mutex queue_mutex_; //!< 保护队列的锁
        std::condition_variable queue_cv_; //!< 条件变量，用于唤醒线程
        bool running_ = true; //!< 线程运行标志

        // GC 状态
        mutable std::mutex gc_status_mutex_;
        std::unordered_set<EventId> pending_gc_events_; //!< 哪些事件需要进行垃圾回收

        // Hooks
        EventTraceHook event_trace_hook_ = nullptr;
        using ExceptionCallback = std::function<void(const std::exception&)>;
        std::shared_ptr<ExceptionCallback> exception_handler_ = nullptr;
        mutable std::mutex exception_handler_mutex_;
    };

}  // namespace z3y

#endif  // Z3Y_SRC_PLUGIN_MANAGER_PIMPL_H_