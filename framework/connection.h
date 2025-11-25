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
 * @file connection.h
 * @brief [核心头文件] 定义事件系统的“连接句柄”及生命周期管理器。
 * @author Yue Liu
 * @date 2025
 *
 * @details
 * **文件作用：**
 * 此文件定义了 `Connection` 和 `ScopedConnection` 两个类。
 * 它们是用户与事件系统交互的凭证。当你订阅一个事件时，系统不会直接返回指针，
 * 而是返回一个 `Connection` 对象。你必须持有它，才能维持订阅，或者用它来取消订阅。
 *
 * **核心设计挑战：**
 * 在多线程环境中，最可怕的 Bug 是 "Use-after-free"（释放后使用）。
 * 比如：你销毁了一个对象（取消了订阅），但在微秒级的时间差内，后台线程正好要执行这个对象的事件回调。
 * 结果就是：程序崩溃。
 *
 * **解决方案：原子票据 (Atomic Ticket)**
 * 本文件中的 `Connection` 类实现了一种“原子票据”机制。
 * 只有持有有效票据的连接，回调才会被允许执行。断开连接等于“撕毁票据”，
 * 这是一个原子操作，瞬间对所有线程可见，从而彻底杜绝上述竞争条件。
 */

#pragma once

#ifndef Z3Y_FRAMEWORK_CONNECTION_H_
#define Z3Y_FRAMEWORK_CONNECTION_H_

#include <memory>       // 用于 std::shared_ptr, std::weak_ptr
#include <atomic>       // 用于 std::atomic (核心线程安全机制)
#include "framework/class_id.h"
#include "framework/z3y_framework_api.h"

 // [MSVC 特定] 压制 STL 模板类导出的警告 (C4251)，这是常规操作
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4251)
#endif

namespace z3y {

    class IEventBus;

    /**
     * @class Connection
     * @brief 事件订阅的“句柄” (Handle)。代表一个活动的事件监听关系。
     *
     * @details
     * **设计思想：**
     * 这是一个“轻量级”对象，它持有底层订阅关系的引用。
     * 它被设计为 **Move-Only** (可移动，不可拷贝)。
     * 这意味着一个订阅关系的所有权是唯一的，不能被复制，只能转移。
     *
     * **如何使用：**
     * 1. 当你调用 `SubscribeGlobal` 时，会得到这个对象。
     * 2. 如果你想取消订阅，调用 `Disconnect()`。
     * 3. 更多时候，我们会把它交给 `ScopedConnection` 来自动管理。
     */
    class Z3Y_FRAMEWORK_API Connection {
    public:
        /**
         * @brief 默认构造函数。
         * @details 创建一个空的、未连接的 Connection 对象。
         * 主要用于占位，例如在类的成员变量初始化时。
         */
        Connection() = default;

        /**
         * @brief [核心功能] 主动断开连接。
         *
         * @details
         * **作用：**
         * 立即停止接收该订阅的事件。
         *
         * **设计思想 (原子撕票)：**
         * 这是一个线程安全的操作。它会做两件事：
         * 1. **逻辑断开**：将内部共享的 `active_token_` (原子布尔值) 设置为 false。
         * 这是一个极快原子操作。一旦执行，即使 EventBus 那个线程正好拿到了回调函数准备执行，
         * 它在执行前检查这个 Token 时也会失败，从而放弃执行。
         * 2. **物理清理**：通知 EventBus 从内部的 `std::vector` 或 `std::map` 中移除这个记录，释放内存。
         */
        void Disconnect();

        /**
         * @brief 查询当前连接是否有效。
         * @return true 表示连接正常；false 表示已断开。
         */
        bool IsConnected() const;

        // --------------------------------------------------------
        // 移动语义 (Move Semantics)
        // --------------------------------------------------------
        // 为什么禁用拷贝？因为连接代表唯一的资源所有权。
        // 如果允许拷贝，Disconnect 时该断开谁？这会导致逻辑混乱。

        Connection(const Connection&) = delete;
        Connection& operator=(const Connection&) = delete;

        /**
         * @brief 移动构造函数。
         * @details 也就是“接管”。把 `other` 的连接权拿过来，让 `other` 变为空。
         */
        Connection(Connection&& other) noexcept;

        /**
         * @brief 移动赋值函数。
         * @details 如果我自己当前已经连着别的事件，先断开旧的，再接管新的。
         */
        Connection& operator=(Connection&& other) noexcept;

    private:
        // 友元声明：只允许 EventBus 和 PluginManager 创建有效的 Connection
        friend class IEventBus;
        friend class PluginManager;

        /**
         * @brief 私有构造函数 (用户无法调用)。
         *
         * @param bus 事件总线的弱引用 (防止 Connection 反向持有 Bus 导致循环引用)。
         * @param subscriber 订阅者对象的弱引用 (用于校验订阅者是否还活着)。
         * @param event_id 订阅的事件 ID。
         * @param sender_key (可选) 关注的发送者 ID。
         * @param active_token [关键] 共享的原子票据。
         */
        Connection(std::weak_ptr<IEventBus> bus, std::weak_ptr<void> subscriber,
            EventId event_id, std::weak_ptr<void> sender_key,
            std::shared_ptr<std::atomic<bool>> active_token);

        /** @brief 保存 EventBus 的弱引用，以便在 Disconnect 时通知它。 */
        std::weak_ptr<IEventBus> bus_;

        /** @brief 保存订阅者的弱引用。 */
        std::weak_ptr<void> subscriber_;

        /** @brief 记录订阅的是哪个事件，用于在 map 中查找。 */
        EventId event_id_ = 0;

        /** @brief 记录关注的是哪个发送者 (如果是 Sender 模式)。 */
        std::weak_ptr<void> sender_key_;

        /** @brief 本地标记，表示此 Connection 对象是否持有连接。 */
        std::atomic<bool> is_connected_{ false };

        /**
         * @brief [核心] 共享原子票据。
         * @details
         * 这是一个 `shared_ptr`，指向堆上的一个 `atomic<bool>`。
         * - Connection 持有一份。
         * - EventBus 内部的订阅列表中也持有一份。
         *
         * 当我们需要断开时，只要把这个 bool 设为 false。
         * 因为是共享内存，EventBus 那边看到的值也会立马变成 false。
         * 这就是解决多线程竞争的神器。
         */
        std::shared_ptr<std::atomic<bool>> active_token_;
    };

    /**
     * @class ScopedConnection
     * @brief [推荐使用] Connection 的 RAII 封装（自动断开器）。
     *
     * @details
     * **设计思想 (RAII):**
     * RAII (Resource Acquisition Is Initialization) 是 C++ 的核心哲学。
     * 它的含义是：资源的生命周期应该绑定到对象的生命周期。
     *
     * `ScopedConnection` 的作用就像 `std::lock_guard` 或 `std::unique_ptr`。
     * 当这个对象离开作用域（比如函数返回、包含它的类被销毁）时，
     * 它的 **析构函数** 会自动被调用。
     * 而我们在析构函数里写了 `Disconnect()`。
     *
     * **意义：**
     * 它可以防止开发者忘记调用 Disconnect，从而避免崩溃。
     * 强烈建议在你的类成员变量中使用 `ScopedConnection` 而不是 `Connection`。
     */
    class ScopedConnection {
    public:
        ScopedConnection() = default;

        /** @brief 构造函数：接管一个 Connection。 */
        ScopedConnection(Connection connection) : conn_(std::move(connection)) {}

        /** @brief [重点] 析构函数：自动断开连接。 */
        ~ScopedConnection() { conn_.Disconnect(); }

        // 同样禁用拷贝，只许移动
        ScopedConnection(const ScopedConnection&) = delete;
        ScopedConnection& operator=(const ScopedConnection&) = delete;

        ScopedConnection(ScopedConnection&& other) noexcept : conn_(std::move(other.conn_)) {}

        ScopedConnection& operator=(ScopedConnection&& other) noexcept {
            if (this != &other) {
                conn_.Disconnect(); // 接管新连接前，先断开旧的
                conn_ = std::move(other.conn_);
            }
            return *this;
        }

        /** @brief 允许手动提前断开。 */
        void Disconnect() { conn_.Disconnect(); }

        /**
         * @brief 检查底层连接是否依然有效。
         * @details
         * 即使 ScopedConnection 对象本身还存在，底层连接也可能因为
         * EventBus 销毁或 Sender 销毁而变为无效。
         */
        bool IsConnected() const {
            return conn_.IsConnected();
        }

    private:
        Connection conn_;
    };

}  // namespace z3y

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#endif  // Z3Y_FRAMEWORK_CONNECTION_H_