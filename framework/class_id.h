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
 * @file class_id.h
 * @brief 定义框架中各种唯一标识符 (ID) 的类型别名和编译期哈希函数。
 * @author Yue Liu
 * @date 2025-06-07
 * @copyright Copyright (c) 2025 Yue Liu
 *
 * @details
 * [受众：所有人]
 *
 * 此文件定义了框架中用于唯一标识事物的 `typedef` 和 `constexpr` 哈希函数。
 * 框架的“零运行时开销”和“类型安全”特性依赖于此文件。
 *
 * [设计思想：框架维护者]
 * 1. 所有的 ID (ClassId, InterfaceId, EventId)
 * 都只是 `uint64_t` 的类型别名。
 * 2. 它们的值是通过 FNV-1a
 * 哈希算法（`ConstexprHash`）在 *编译期* 从 UUID 字符串生成的。
 * 3.
 * 这使得所有比较（例如在 `QueryInterfaceRaw` 中）都只是整数比较，
 * 避免了运行时的 `strcmp` 或 `std::map` 查找，性能极高。
 */

#pragma once

#ifndef Z3Y_FRAMEWORK_CLASS_ID_H_
#define Z3Y_FRAMEWORK_CLASS_ID_H_

#include <cstdint>  // 用于 uint64_t, C++11

namespace z3y {

    /**
     * @typedef ClassId
     * @brief [受众：插件开发者]
     * 组件 *实现类* (Component Class) 的唯一标识符。
     *
     * 每个具体的插件 *实现类* (例如 `DemoLoggerService`)
     * 都必须有一个唯一的 `ClassId`。
     *
     * @see Z3Y_DEFINE_COMPONENT_ID
     */
    using ClassId = uint64_t;

    /**
     * @typedef InterfaceId
     * @brief [受众：插件开发者]
     * *接口类* (Interface) 的唯一标识符。
     *
     * 每个插件 *接口* (例如 `IDemoLogger`) 都必须有一个唯一的 `InterfaceId`。
     *
     * @see Z3Y_DEFINE_INTERFACE
     * @see IComponent::QueryInterfaceRaw
     */
    using InterfaceId = ClassId;

    /**
     * @typedef EventId
     * @brief [受众：插件开发者]
     * *事件* (Event) 的唯一标识符。
     *
     * 每种 *事件类型* (例如 `DemoGlobalEvent`) 都必须有一个唯一的 `EventId`。
     *
     * @see Z3Y_DEFINE_EVENT
     * @see IEventBus
     */
    using EventId = ClassId;

    // --- [受众：框架维护者] 编译期哈希 (Compile-Time Hashing) ---
    namespace internal {
        //! FNV-1a 算法的 64 位偏移基底
        constexpr uint64_t kFnvOffsetBasis = 0xcbf29ce484222325ULL;
        //! FNV-1a 算法的 64 位素数
        constexpr uint64_t kFnvPrime = 0x100000001b3ULL;

        /**
         * @brief [内部] 编译期 FNV-1a 哈希算法的递归实现 (C++14/17)。
         * @param[in] str C 风格字符串 (必须是编译期常量)。
         * @param[in] hash 当前递归的哈希值。
         * @return 64 位哈希结果。
         */
        constexpr uint64_t Fnv1aHashRt(const char* str,
            uint64_t hash = kFnvOffsetBasis) {
            return (*str == '\0')
                ? hash
                : Fnv1aHashRt(str + 1,
                    (hash ^ static_cast<uint64_t>(*str)) * kFnvPrime);
        }
    }  // namespace internal

    /**
     * @brief [框架核心工具] 在编译期将字符串 (通常是UUID) 转换为 64 位的 ID。
     *
     * [受众：插件开发者]
     * 这是 `Z3Y_DEFINE_...` 系列宏内部使用的函数。
     * 你 *不* 需要，也 *不应该* 直接调用此函数。
     *
     * [受众：框架维护者]
     * 这是框架实现"零开销"、"无运行时字符串比较"的ID系统的核心。
     * 推荐使用 UUID 字符串来保证 `str` 的全局唯一性。
     *
     * @param[in] str 用于哈希的 C 风格字符串字面量。
     * @return 64 位的哈希值 (ClassId, InterfaceId 或 EventId)。
     */
    constexpr ClassId ConstexprHash(const char* str) {
        return (str == nullptr || *str == '\0')
            ? 0  // 0 被保留为无效 ID
            : internal::Fnv1aHashRt(str);
    }

}  // namespace z3y

#endif  // Z3Y_FRAMEWORK_CLASS_ID_H_