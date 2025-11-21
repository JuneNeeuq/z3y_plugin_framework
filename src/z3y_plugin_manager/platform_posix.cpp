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
 * @file platform_posix.cpp
 * @brief z3y::PluginManager 的 POSIX (Linux/macOS) 平台特定实现。
 * @author Yue Liu
 * @date 2025-07-12
 * @copyright Copyright (c) 2025 Yue Liu
 *
 * @details
 * [受众：框架维护者]
 *
 * [设计思想：平台抽象层 (PAL)]
 * 此文件封装了所有 POSIX 特定的 API 调用 (`dlfcn.h`)。
 * 它 *只* 在非 Windows 平台 (检测到 `!defined(_WIN32)`) 时才被编译。
 *
 * `plugin_manager.cpp`
 * 通过调用 `PlatformLoadLibrary`, `PlatformGetFunction`
 * 等抽象函数来执行平台相关操作，
 * 而无需知道底层的 `dlopen` 或 `dlsym`。
 */

 // 仅在非 Windows 平台上编译此文件
#if !defined(_WIN32)

#include "plugin_manager_pimpl.h"  // Pimpl 私有头文件
#include <codecvt>  // 用于编码转换
#include <locale>
#include <string> 

namespace z3y {

    namespace {
        /**
         * @brief [内部] 将系统本地编码 (dlerror 返回的) 转换为 UTF-8。
         *
         * [设计思想]
         * `dlerror()` 返回一个 `char*`，
         * 其编码依赖于用户的 `LC_ALL` 环境变量。
         * 框架内部希望统一使用 UTF-8
         * (与 `std::filesystem::path` 保持一致)，
         * 因此需要此转换函数。
         *
         * @param locale_str `dlerror()` 返回的 C 风格字符串。
         * @return UTF-8 编码的 `std::string`。
         */
        std::string LocaleToUtf8(const char* locale_str) {
            try {
                // 1.
                // 将本地多字节字符串 (char*) 转换为宽字符串 (wchar_t)
                // (使用 C++ 标准库的 codecvt， 而不是 C 的 mbstowcs)
                std::wstring_convert<std::codecvt_byname<wchar_t, char, std::mbstate_t>>
                    converter(new std::codecvt_byname<wchar_t, char, std::mbstate_t>(""));

                std::wstring wide_str = converter.from_bytes(locale_str);

                // 2. 将宽字符串 (wchar_t) 转换为 UTF-8 (std::string)
#if __cplusplus >= 201703L
                // C++17 中 wstring_convert
                // 被弃用，但没有标准替代品
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
                std::wstring_convert<std::codecvt_utf8<wchar_t>> utf8_converter;
                return utf8_converter.to_bytes(wide_str);
#if __cplusplus >= 201703L
#pragma GCC diagnostic pop
#endif
            } catch (const std::exception& e) {
                // 如果转换失败（例如 locale 设置不正确），
                // 返回原始字符串，并附带警告
                return std::string(locale_str) + " (locale-to-UTF8 conversion failed)";
            }
        }
    }  // namespace

    // --- 平台抽象层实现 (委托给 Pimpl) ---

    /**
     * @brief [平台实现-POSIX] 卸载所有已加载的 .so/.dylib。
     *
     * [锁]
     * 此函数必须在 `ClearAllRegistries` 的 **写锁** 保护下调用。
     */
    void PluginManager::PlatformSpecificLibraryUnload() {
        // [设计] 按 LIFO (后进先出) 顺序卸载
        for (auto it = pimpl_->loaded_libs_.rbegin();
            it != pimpl_->loaded_libs_.rend(); ++it) {
            ::dlclose(it->second);
        }
    }

    /**
     * @brief [平台实现-POSIX]
     * 检查文件是否是一个有效的插件 (即 .so 或 .dylib 文件)。
     * @param[in] path 文件路径。
     * @return `true` 如果是 .so 或 .dylib 文件，`false` 则不是。
     */
    bool PluginManager::PlatformIsPluginFile(const std::filesystem::path& path) {
        if (!std::filesystem::is_regular_file(path)) {
            return false;
        }
        const auto extension = path.extension();
        return (extension == ".so" ||  // Linux
            extension == ".dylib");  // macOS
    }

    /**
     * @brief [平台实现-POSIX] 加载一个 .so/.dylib。
     * @param[in] path 库的路径 (`std::filesystem::path`
     * 自动处理 UTF-8)。
     * @return `void*` 句柄 (作为 `LibHandle`)。
     */
    PluginManager::LibHandle PluginManager::PlatformLoadLibrary(
        const std::filesystem::path& path) {

        // 在调用 dlopen 之前清除旧的 dlerror 状态
        (void)dlerror();

        // [设计]
        // `path.string().c_str()` 在 POSIX
        // 上返回 UTF-8 编码的 `const char*`。
        //
        // RTLD_NOW: 立即解析所有符号 (失败时立即返回)。
        // RTLD_LOCAL: 符号不暴露给其他库 (模拟 Windows 行为)。
        return ::dlopen(path.string().c_str(), RTLD_NOW | RTLD_LOCAL);
    }

    /**
     * @brief [平台实现-POSIX] 从库中获取函数地址。
     * @param[in] handle `PlatformLoadLibrary` 返回的 `void*` 句柄。
     * @param[in] func_name 要查找的 C 风格函数名 (ASCII)。
     * @return 函数指针 (作为 `void*`)。
     */
    void* PluginManager::PlatformGetFunction(LibHandle handle,
        const char* func_name) {

        // 在调用 dlsym 之前清除旧的 dlerror 状态
        (void)dlerror();
        return ::dlsym(handle, func_name);
    }

    /**
     * @brief [平台实现-POSIX] 卸载一个库。
     * @param[in] handle `void*` 句柄。
     */
    void PluginManager::PlatformUnloadLibrary(LibHandle handle) { ::dlclose(handle); }

    /**
     * @brief [平台实现-POSIX] 获取 `dlerror()` 返回的错误信息，并将其转换为 UTF-8。
     *
     * @return UTF-8 编码的 `std::string` 错误信息。
     */
    std::string PluginManager::PlatformGetError() {
        const char* err_str = ::dlerror();
        if (err_str) {
            // 返回 UTF-8 编码的字符串， 而不是原始的 locale 字符串
            return LocaleToUtf8(err_str);
        }
        return "Unknown POSIX dynamic library error";
    }

}  // namespace z3y

#endif  // !defined(_WIN32)