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
 * @file connection_type.h
 * @brief 定义事件总线的连接类型枚举 z3y::ConnectionType。
 * @author Yue Liu
 * @date 2025-06-14
 * @copyright Copyright (c) 2025 Yue Liu
 *
 * @details
 * [受众：插件开发者]
 * 此文件定义了在订阅事件时必须选择的回调执行方式。
 *
 * @see IEventBus::SubscribeGlobal
 */

#pragma once

#ifndef Z3Y_FRAMEWORK_CONNECTION_TYPE_H_
#define Z3Y_FRAMEWORK_CONNECTION_TYPE_H_

namespace z3y {

    /**
     * @enum ConnectionType
     * @brief 指定事件订阅的回调方式。
     */
    enum class ConnectionType {
        /**
         * @brief 直接连接 (同步)。
         *
         * 回调函数将在 `FireGlobal` 或 `FireToSender` 被调用的 *同一线程*
         * 上立即执行。
         *
         * [使用时机]
         * - 优点：低延迟，立即响应。
         * - 缺点：如果回调函数阻塞，将导致发布者（调用 `Fire` 的线程）阻塞。
         *
         * **[禁忌]**
         * 绝对不要在 `kDirect` 回调中执行任何耗时操作、
         * 获取锁、或调用可能触发更多事件的复杂逻辑。
         * 仅用于快速的状态更新（例如设置一个 `bool` 标志）。
         */
        kDirect,

        /**
         * @brief 队列连接 (异步)。
         *
         * 回调函数将被放入 `PluginManager` 的内部事件队列中，
         * 由框架的事件循环工作线程在 *稍后* 的某个时间点执行。
         *
         * [使用时机]
         * - **这是默认和推荐的选项。**
         * - 优点：线程安全。`Fire` 调用立即返回，不会阻塞发布者。
         * 适合处理需要几毫秒或可能与其他插件交互的回调 (例如日志记录)。
         * - 缺点：有轻微的延迟 (取决于队列负载)。
         */
        kQueued
    };

}  // namespace z3y

#endif  // Z3Y_FRAMEWORK_CONNECTION_TYPE_H_