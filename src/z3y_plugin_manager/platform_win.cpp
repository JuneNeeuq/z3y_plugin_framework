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
 * @file platform_win.cpp
 * @brief z3y::PluginManager 的 Windows 平台特定实现。
 * @author Yue Liu
 * @date 2025-07-12
 * @copyright Copyright (c) 2025 Yue Liu
 *
 * @details
 * [受众：框架维护者]
 *
 * [设计思想：平台抽象层 (PAL)]
 * 此文件封装了所有 Windows 特定的 API 调用 (Win32 API)。
 * 它 *只* 在 Windows 平台 (检测到 `_WIN32`) 时才被编译。
 *
 * `plugin_manager.cpp`
 * 通过调用 `PlatformLoadLibrary`, `PlatformGetFunction`
 * 等抽象函数来执行平台相关操作，
 * 而无需知道底层的 `LoadLibraryW` 或 `GetProcAddress`。
 */

 // 仅在 Windows 平台上编译此文件
#ifdef _WIN32

#include "plugin_manager_pimpl.h"  // Pimpl 私有头文件

namespace z3y {

    /**
     * @brief [平台实现-Win] 卸载所有已加载的 DLL。
     *
     * [锁]
     * 此函数必须在 `ClearAllRegistries` 的 **写锁** 保护下调用。
     */
    void PluginManager::PlatformSpecificLibraryUnload() {
        // [设计] 按 LIFO (后进先出) 顺序卸载
        for (auto it = pimpl_->loaded_libs_.rbegin();
            it != pimpl_->loaded_libs_.rend(); ++it) {
            ::FreeLibrary(static_cast<HMODULE>(it->second));
        }
    }

    /**
     * @brief [平台实现-Win]
     * 检查文件是否是一个有效的插件 (即 .dll 文件)。
     * @param[in] path 文件路径。
     * @return `true` 如果是 .dll 文件，`false` 则不是。
     */
    bool PluginManager::PlatformIsPluginFile(const std::filesystem::path& path) {
        return std::filesystem::is_regular_file(path) && path.extension() == ".dll";
    }

    /**
     * @brief [平台实现-Win] 加载一个 DLL。
     * @param[in] path DLL 的路径 (`std::filesystem::path`
     * 自动处理 UTF-8 到 UTF-16)。
     * @return `HMODULE` (作为 `LibHandle`)。
     */
    PluginManager::LibHandle PluginManager::PlatformLoadLibrary(
        const std::filesystem::path& path) {
        // [设计]
        // 使用 LoadLibraryW 来正确处理宽字符 (UTF-16) 路径，
        // `path.c_str()` (C++17) 自动提供了 `const wchar_t*`。
        return ::LoadLibraryW(path.c_str());
    }

    /**
     * @brief [平台实现-Win]
     * 从 DLL 中获取函数地址。
     * @param[in] handle `PlatformLoadLibrary` 返回的 `HMODULE`。
     * @param[in] func_name 要查找的 C 风格函数名 (ASCII)。
     * @return 函数指针 (作为 `void*`)。
     */
    void* PluginManager::PlatformGetFunction(LibHandle handle,
        const char* func_name) {
        return ::GetProcAddress(static_cast<HMODULE>(handle), func_name);
    }

    /**
     * @brief [平台实现-Win] 卸载一个 DLL。
     * @param[in] handle `HMODULE` 句柄。
     */
    void PluginManager::PlatformUnloadLibrary(LibHandle handle) {
        ::FreeLibrary(static_cast<HMODULE>(handle));
    }

    /**
     * @brief [平台实现-Win]
     * 将 `GetLastError()` 返回的 `DWORD` 错误码格式化为 UTF-8 字符串。
     *
     * @param[in] error_id `::GetLastError()` 返回的错误码。
     * @return UTF-8 编码的 `std::string` 错误信息。
     */
    std::string PluginManager::PlatformFormatError(DWORD error_id) {
        if (error_id == 0) {
            return "No error (GetLastError() returned 0)";
        }

        // 1. 将错误码转换为系统本地的宽字符串 (UTF-16)
        LPWSTR buffer = nullptr;
        size_t size = FormatMessageW(
            FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
            NULL, error_id, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            (LPWSTR)&buffer, 0, NULL);

        if (size == 0) {
            std::stringstream ss;
            ss << "Unknown error code " << error_id << " (and FormatMessage failed)";
            return ss.str();
        }

        std::wstring w_msg(buffer, size);
        LocalFree(buffer); // 释放 FormatMessageW 分配的缓冲区

        // 2. 将宽字符串 (UTF-16) 转换为 UTF-8
        int out_size =
            WideCharToMultiByte(CP_UTF8, 0, w_msg.c_str(), (int)w_msg.length(),
                NULL, 0, NULL, NULL);
        if (out_size == 0) {
            return "Failed to convert error message to UTF-8";
        }

        std::string msg(out_size, 0);
        WideCharToMultiByte(CP_UTF8, 0, w_msg.c_str(), (int)w_msg.length(), &msg[0],
            out_size, NULL, NULL);

        return msg;
    }

}  // namespace z3y

#endif  // _WIN32