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
 * @file z3y_utils.h
 * @brief [通用工具] 框架提供的跨平台辅助函数集合。
 * @details
 * 该文件包含在 z3y_define_impl.h 和 z3y_framework.h 中，
 * 开发者通常无需手动包含。
 */

#pragma once

#ifndef Z3Y_FRAMEWORK_UTILS_H_
#define Z3Y_FRAMEWORK_UTILS_H_

#include <string>
#include <filesystem>
#include "framework/z3y_framework_api.h" // 用于 DLL 导出

namespace z3y {
    namespace utils {

        // --- [编译期工具] ---

        /**
         * @brief [编译期] 从文件路径中提取文件名。
         * @details
         * 用于在编译阶段处理 `__FILE__` 宏，剥离路径前缀。
         * 例如：GetFileNameFromPath("C:/Projs/src/main.cpp") -> "main.cpp"
         * * [实现说明]
         * 必须在头文件中实现，以便编译器进行常量折叠 (Constant Folding)。
         */
        constexpr const char* GetFileNameFromPath(const char* path) {
            const char* file = path;
            while (*path) {
                if (*path == '/' || *path == '\\') {
                    file = path + 1;
                }
                path++;
            }
            return file;
        }

        /**
         * @brief [跨平台] 将 UTF-8 字符串转换为 filesystem::path。
         * * @details
         * - **Windows**: 执行 UTF-8 -> UTF-16 (WideChar) 转换。
         * - **POSIX**: 直接透传。
         */
        Z3Y_FRAMEWORK_API std::filesystem::path Utf8ToPath(const std::string& utf8_str);

        /**
         * @brief [跨平台] 将 filesystem::path 转换为 UTF-8 字符串。
         * * @details
         * - **Windows**: 执行 UTF-16 (WideChar) -> UTF-8 转换。
         * - **POSIX**: 直接返回 path.string()。
         * * @param path 文件系统路径对象。
         * @return UTF-8 编码的 std::string。
         */
        Z3Y_FRAMEWORK_API std::string PathToUtf8(const std::filesystem::path& path);

        /**
         * @brief [跨平台] 获取当前可执行文件的绝对路径。
         * @details
         * - **Windows**: 使用 GetModuleFileNameW。
         * - **Linux**: 读取 /proc/self/exe。
         * - **macOS**: 使用 _NSGetExecutablePath。
         * @return 可执行文件的完整路径 (包含文件名)。失败则返回空 path。
         */
        Z3Y_FRAMEWORK_API std::filesystem::path GetExecutablePath();

        /**
         * @brief [跨平台] 获取当前可执行文件所在的目录。
         * @details 便捷函数，等同于 GetExecutablePath().parent_path()。
         */
        inline std::filesystem::path GetExecutableDir() {
            return GetExecutablePath().parent_path();
        }

        /**
         * @brief [跨平台] 获取当前平台的动态库后缀名。
         * @return Windows: ".dll", Linux: ".so", macOS: ".dylib"
         */
        Z3Y_FRAMEWORK_API const char* GetSharedLibraryExtension();

        // --- 系统环境 ---

        /**
         * @brief [跨平台] 启用控制台的 UTF-8 输出支持。
         * @details
         * - Windows: 设置代码页为 CP_UTF8。
         * - POSIX: 通常无需操作，但在某些环境下可能设置 locale。
         */
        Z3Y_FRAMEWORK_API void EnableUtf8Console();

        /**
         * @brief [跨平台] 获取最近一次系统错误的描述信息。
         * @details 封装了 GetLastError (Win) 或 errno (Posix)，返回 UTF-8 字符串。
         */
        Z3Y_FRAMEWORK_API std::string GetLastSystemError();

        // --- 文件操作 ---

        /**
         * @brief [高可靠] 原子写入文件 (Atomic Write)。
         * @details
         * 1. 写入临时文件。
         * 2. 强制刷盘 (fsync/_commit)。
         * 3. 原子重命名/移动 (rename/MoveFileEx) 到目标路径。
         * * 保证即使在写入过程中断电或崩溃，目标文件要么是旧版本，要么是新版本，绝不会是损坏的“半个文件”。
         * * @param path 目标路径。
         * @param content 要写入的内容。
         * @return true 成功，false 失败。
         */
        Z3Y_FRAMEWORK_API bool AtomicWriteFile(const std::filesystem::path& path, const std::string& content);

    } // namespace utils
} // namespace z3y

#endif // Z3Y_FRAMEWORK_UTILS_H_