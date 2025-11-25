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

#include "framework/z3y_utils.h"
#include <cstdio> // for _wfopen, fwrite
#include <io.h>   // for _commit

namespace z3y {
    namespace utils {

        std::filesystem::path Utf8ToPath(const std::string& utf8_path) {
            if (utf8_path.empty()) return std::filesystem::path();
            int size_needed = ::MultiByteToWideChar(CP_UTF8, 0, &utf8_path[0], (int)utf8_path.size(), NULL, 0);
            if (size_needed <= 0) return std::filesystem::path();
            std::wstring wstr(size_needed, 0);
            ::MultiByteToWideChar(CP_UTF8, 0, &utf8_path[0], (int)utf8_path.size(), &wstr[0], size_needed);
            return std::filesystem::path(wstr);
        }

        std::string PathToUtf8(const std::filesystem::path& path) {
            // 1. 获取宽字符路径 (Native on Windows)
            std::wstring wstr = path.wstring();
            if (wstr.empty()) return "";

            // 2. 计算需要的 UTF-8 缓冲区大小
            int size_needed = ::WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
            if (size_needed <= 0) return "";

            // 3. 转换
            std::string str(size_needed, 0);
            ::WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &str[0], size_needed, NULL, NULL);
            return str;
        }

        std::filesystem::path GetExecutablePath() {
            // 初始缓冲区大小 (MAX_PATH 可能不够长路径使用)
            std::vector<wchar_t> buffer(MAX_PATH);

            while (true) {
                DWORD length = ::GetModuleFileNameW(NULL, buffer.data(), static_cast<DWORD>(buffer.size()));

                if (length == 0) {
                    // 失败
                    return std::filesystem::path();
                }

                if (length < buffer.size()) {
                    // 成功
                    return std::filesystem::path(buffer.data());
                }

                // 缓冲区太小，扩容重试 (Windows XP 之后行为)
                // 如果 length == size，说明可能被截断了(或者正好满了)，扩容以防万一
                buffer.resize(buffer.size() * 2);
            }
        }

        const char* GetSharedLibraryExtension() {
            return ".dll";
        }

        void EnableUtf8Console() {
            // 设置控制台输入输出代码页为 UTF-8
            ::SetConsoleOutputCP(CP_UTF8);
            ::SetConsoleCP(CP_UTF8);
        }

        std::string GetLastSystemError() {
            DWORD error_id = ::GetLastError();
            if (error_id == 0) return "No error";

            LPWSTR buffer = nullptr;
            size_t size = ::FormatMessageW(
                FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                NULL, error_id, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                (LPWSTR)&buffer, 0, NULL);

            if (size == 0) return "Unknown error (Code: " + std::to_string(error_id) + ")";

            std::wstring w_msg(buffer, size);
            ::LocalFree(buffer);

            // 移除末尾的换行符
            while (!w_msg.empty() && (w_msg.back() == L'\r' || w_msg.back() == L'\n')) {
                w_msg.pop_back();
            }

            // 复用 PathToUtf8 的转换逻辑 (wstring -> string)
            // 也可以直接在这里写 WideCharToMultiByte
            int size_needed = ::WideCharToMultiByte(CP_UTF8, 0, w_msg.c_str(), (int)w_msg.size(), NULL, 0, NULL, NULL);
            std::string msg(size_needed, 0);
            ::WideCharToMultiByte(CP_UTF8, 0, w_msg.c_str(), (int)w_msg.size(), &msg[0], size_needed, NULL, NULL);

            return msg;
        }

        bool AtomicWriteFile(const std::filesystem::path& path, const std::string& content) {
            std::filesystem::path tmp_path = path;
            tmp_path += ".tmp";

            FILE* fp = _wfopen(tmp_path.c_str(), L"wb");
            if (!fp) return false;

            size_t written = fwrite(content.data(), 1, content.size(), fp);
            if (written != content.size()) {
                fclose(fp);
                std::filesystem::remove(tmp_path);
                return false;
            }

            // 强制刷盘
            fflush(fp);
            _commit(_fileno(fp));
            fclose(fp);

            // 原子移动 (Windows)
            // MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH
            if (::MoveFileExW(tmp_path.c_str(), path.c_str(), 0x1 | 0x8)) {
                return true;
            }

            std::filesystem::remove(tmp_path);
            return false;
        }

    } // namespace utils
} // namespace z3y

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

}  // namespace z3y

#endif  // _WIN32