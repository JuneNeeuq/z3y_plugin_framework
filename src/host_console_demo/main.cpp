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
 * @file main.cpp
 * @brief [宿主] 宿主程序 (Host) 的控制台示例入口点。
 * @author Yue Liu
 * @date 2025-08-23
 * @copyright Copyright (c) 2025 Yue Liu
 *
 * @details
 * [受众：框架使用者 (宿主开发者)]
 *
 * [设计思想：宿主 (Host) 的职责]
 * 这是插件框架的 *使用者* (也称为“宿主”或“主机”) 的主程序。
 *
 * 它的职责非常清晰和线性化：
 *
 * 1. **(Try-Catch)**
 * 将所有操作包裹在一个顶层 `try...catch` 块中，
 * 以捕获任何 `PluginException` 或 `std::exception`。
 *
 * 2. **`PluginManager::Create()`**
 * [启动] 创建框架的核心 `PluginManager` 实例。
 *
 * 3. **`SetExceptionHandler()`**
 * [!! 关键 !!]
 * 注册一个“带外”(Out-of-Band, OOB) 异常处理器
 * (`HostExceptionHandler`)。
 *
 * [!! 为什么 !!]
 * 如果插件的 `kQueued` (异步) 事件回调在 *工作线程*
 * 中抛出异常， 此处理器将被调用。
 * 如果 *不* 注册此处理器， 异步异常将导致 `std::terminate` (程序崩溃)。
 *
 * 4. **`HostEventListener`**
 * [可选]
 * 创建并订阅宿主自己的事件监听器，
 * 用于在控制台打印插件加载/注册信息。
 *
 * 5. **`LoadPluginsFromDirectory()`**
 * [核心]
 * 确定插件目录 (通常是 `exe` 所在的目录)，
 * 并告诉 `PluginManager` 加载该目录下的所有 DLL/SO。
 *
 * 6. **`z3y::GetDefaultService<IDemoRunner>()`**
 * [核心]
 * 宿主 *不* 关心加载了哪些具体插件。
 * 它只关心一个高级业务接口：`IDemoRunner`。
 * 它向框架请求此接口的“默认” 服务。
 *
 * 7. **`demo_runner->RunAllDemos()`**
 * [核心]
 * 调用 `IDemoRunner` 的方法，
 * 将“执行业务逻辑” 的职责 *委托* 给插件 (`plugin_demo_runner`)。
 *
 * 8. **(Cleanup)**
 * [!! 关键：安全清理 !!]
 * 按 *严格* 的相反顺序清理 (LIFO - 后进先出)：
 *
 * a. `demo_runner.reset()` (释放对 `IDemoRunner` 服务的 `PluginPtr` 引用)
 * b. `logger.reset()` (释放对 `IDemoLogger` 服务的 `PluginPtr` 引用)
 * c. `host_listener.reset()` (释放对监听器的 `PluginPtr` 引用)
 *
 * [!! 为什么 !!]
 * 必须在调用 `UnloadAllPlugins` *之前*
 * 释放所有从插件获取的 `PluginPtr`。
 * 否则，宿主将持有指向已被卸载 DLL 内存的悬空指针，
 * 导致在 `PluginPtr` 析构时崩溃。
 *
 * d. `manager->UnloadAllPlugins()`
 * (安全调用所有插件的 `Shutdown()` 并卸载 DLLs)
 * e. `manager.reset()`
 * (销毁`PluginManager` 自身，停止 `EventLoop` 线程)
 *
 *
 * [设计总结]
 * 这种设计使得 `main.cpp` 非常简洁，
 * 并且与所有具体的“功能” 插件
 * (如 `plugin_demo_module_core` 等) 完全解耦。
 */

 // 1. 包含框架宿主 SDK (All-in-One 头文件)
#include "framework/z3y_framework.h"

// 2. 包含宿主 *唯一* 依赖的业务接口
#include "interfaces_demo/i_demo_runner.h"
#include "interfaces_demo/i_demo_logger.h"
// 3. 包含宿主自己的事件监听器
#include "host_event_listener.h"

// 4. C++ StdLib
#include <filesystem>  // [C++17]
#include <iostream>
#include <mutex>   // 用于 HostExceptionHandler
#include <string> 
#include <vector>

// 5. 平台特定 (用于设置 UTF-8 控制台)
#ifdef _WIN32
#include <Windows.h>
#endif

/**
 * @brief [OOB 异常处理器]
 *
 * [受众：框架使用者 (宿主开发者)]
 *
 * 这是一个静态函数，
 * 将被传递给 `PluginManager::SetExceptionHandler`。
 * 它在 `EventLoop`工作线程上被调用（如果异步回调抛出异常）。
 *
 * @warning
 * [线程安全]
 * 此函数可能在 *任何* 线程上被调用，必须是线程安全的。
 * 我们使用一个全局 `g_cerr_mutex` 来保护 `std::cerr`的并发访问。
 */
static std::mutex g_cerr_mutex;
void HostExceptionHandler(const std::exception& e) {
    std::lock_guard lock(g_cerr_mutex);
    std::cerr << "\n=======================================================\n"
        << "[Host OOB Handler] CAUGHT PLUGIN EXCEPTION (std::exception):\n"
        << "  " << e.what() << "\n"
        << "=======================================================\n"
        << std::endl;
}

/**
 * @brief [宿主] 主程序入口点。
 */
int main(int argc, char* argv[]) {
#ifdef _WIN32
    // 设置控制台输出为 UTF-8，
    // 解决中文乱码
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
    std::cout << "--- z3y C++ Plugin Framework Host Demo (Modular) ---"
        << std::endl;

    // 1. [核心] 顶层 try...catch 块
    try {
        // 2. [启动] 创建 PluginManager
        z3y::PluginPtr<z3y::PluginManager> manager = z3y::PluginManager::Create();

        // 3. [!! 关键 !!] 注册 OOB 异常处理器
        std::cout << "[Host] Registering Out-of-Band exception handler..."
            << std::endl;
        manager->SetExceptionHandler(HostExceptionHandler);

        // 4. [可选] 创建并订阅宿主的核心事件监听器
        auto host_listener = std::make_shared<z3y::demo::HostEventListener>();
        host_listener->SubscribeToFrameworkEvents();

        // 5. 确定插件目录 (exe 所在目录)
        std::filesystem::path exe_dir = ".";
        if (argc > 0 && argv[0]) {
            exe_dir = std::filesystem::path(argv[0]).parent_path();
        }
        std::cout << "\n[Host] Loading all plugins from: " << exe_dir.string()
            << std::endl;

        // 6. [核心] 加载 *所有* 插件
        std::vector<std::string> load_errors =
            manager->LoadPluginsFromDirectory(exe_dir, true);

        // (报告加载失败，但演示继续)
        if (!load_errors.empty()) {
            std::cerr << "[Host] [!! WARNING !!] One or more plugins failed to load:"
                << std::endl;
            for (const auto& err : load_errors) {
                std::cerr << "       - " << err << std::endl;
            }
        }

        std::cout << "[Host] All plugins loaded." << std::endl;

        // 7. [核心] 获取“Demo日志”服务
        std::cout << "\n[Host] Getting Demo Logger Service..." << std::endl;
        auto logger = z3y::GetDefaultService<z3y::demo::IDemoLogger>();
        logger->Log("Host acquired demo logger service!");

        // 8. [核心] 获取“Demo执行”服务
        std::cout << "\n[Host] Getting Demo Runner Service..." << std::endl;
        z3y::PluginPtr<z3y::demo::IDemoRunner> demo_runner;

        try {
            // [设计]
            // 宿主只依赖 `IDemoRunner` 的“默认” 实现。
            demo_runner = z3y::GetDefaultService<z3y::demo::IDemoRunner>();
        }
        catch (const z3y::PluginException& e) {
            // [健壮性]
            // 如果 `IDemoRunner` (核心业务) 都获取失败，
            // 这是一个致命错误，程序无法继续。
            logger->Log("[Host] [FATAL] Failed to get required IDemoRunner service: " + std::string(e.what()));
            manager.reset();  // 触发析构
            return 1;
        }

        // 9. [核心] 执行所有模块化测试
        std::cout << "\n[Host] Telling Demo Runner to execute all demo modules..."
            << std::endl;
        demo_runner->RunAllDemos();
        std::cout << "[Host] Demo Runner finished." << std::endl;

        // 10. [!! 关键：安全清理 !!]
        std::cout << "\n[Host] Releasing services..." << std::endl;
        // a. 释放对插件服务的`PluginPtr` 引用 (LIFO 顺序)
        demo_runner.reset();
        logger.reset();
        // b. 释放监听器
        host_listener.reset();

        // 11. [!! 关键：安全清理 !!] 卸载所有插件
        std::cout << "[Host] Unloading all plugins..." << std::endl;
        // (这将调用所有 `Shutdown()` -> 析构 -> FreeLibrary/dlclose)
        manager->UnloadAllPlugins();
        std::cout << "[Host] Unload complete." << std::endl;

        // 12. 退出
        std::cout << "\n--- Demo Finished ---" << std::endl;
        // c. 销毁 PluginManager 自身 (这将停止 EventLoop 线程)
        manager.reset();

    }
    catch (const z3y::PluginException& e) {
        // [健壮性]
        // 捕获在 `Create` 或 `GetDefaultService`(步骤 7, 8)
        // 中 发生的 *致命* 异常
        std::cerr
            << "\n[Host] [!! FATAL !!] A plugin exception was caught at the top "
            "level: "
            << e.what() << std::endl;
        return 1;
    }
    catch (const std::exception& e) {
        std::cerr << "\n[Host] [!! FATAL !!] A standard exception was caught: "
            << e.what() << std::endl;
        return 1;
    }
    catch (...) {
        std::cerr << "\n[Host] [!! FATAL !!] An unknown exception was caught."
            << std::endl;
        return 1;
    }

    return 0;
}