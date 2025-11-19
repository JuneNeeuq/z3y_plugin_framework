# z3y C++ 插件框架 (z3y_plugin_framework)

`z3y_plugin_framework` 是一个现代、轻量级、跨平台、易于使用的 C++17 插件框架。

它的核心设计目标是提供**稳定的应用程序二进制接口 (ABI)**、**极简的插件开发体验**和**强大的功能** (如服务定位、事件总线和内省)。

![语言](https://img.shields.io/badge/language-C%2B%2B17-blue.svg)
![构建](https://img.shields.io/badge/build-CMake-brightgreen.svg)
![平台](https://img.shields.io/badge/platform-Windows%20%7C%20Linux%20%7C%20macOS-orange.svg)
![许可](https://img.shields.io/badge/license-Apache%202.0-blue.svg) 本项目采用 [Apache 2.0 许可证](LICENSE) 发布。

有关详细信息，请参阅 [LICENSE](LICENSE) 文件和 [NOTICE](NOTICE) 文件。

---

## 核心特性

本框架从 COM、现代 C++ 和其他框架中汲取灵感，提供了一套强大的特性：

* ✨ **极简的开发体验**：
    * **自动注册**: 插件开发者只需使用 `Z3Y_AUTO_REGISTER_SERVICE` 或 `Z3Y_AUTO_REGISTER_COMPONENT` 宏，无需手动编写 `z3yPluginInit` 入口函数。
    * **自动实现接口**: 使用 CRTP 基类 `z3y::PluginImpl`，可自动实现 `QueryInterfaceRaw` 和接口元数据收集，开发者只需专注于业务逻辑。
* 📦 **ABI 稳定性 (编译防火墙)**：
    * 核心 `PluginManager` 使用 **Pimpl 模式**，允许在不破坏 ABI 的情况下修改框架的内部实现，宿主和插件无需重新编译。
* 🧩 **COM 风格的接口查询**：
    * 所有插件对象均继承自 `IComponent`。
    * 使用 `z3y::PluginCast` 进行跨 DLL 边界的安全类型转换，替代 `dynamic_cast`。
    * **接口版本控制**: `Z3Y_DEFINE_INTERFACE` 宏允许定义主/次版本号，`PluginCast` 会在转换时自动检查版本兼容性，防止 API 误用。
* 🎯 **服务定位器 (Service Locator)**：
    * **服务 (Services)**: 单例对象，通过 `z3y::GetService` 获取 (例如 `IDemoLogger`)。
    * **组件 (Components)**: 瞬态对象（每次都创建新实例），通过 `z3y::CreateInstance` 获取 (例如 `IDemoSimple`)。
    * **Try... API**: 提供 `z3y::TryGetService` 等 `noexcept` 版本，用于在析构或 `Shutdown` 等不能抛出异常的上下文中使用。
* 🚀 **强大的事件总线 (IEventBus)**：
    * **两种订阅模式**: 全局广播 (`FireGlobal`) 和特定发布者 (`FireToSender`)。
    * **两种连接类型**: 同步 (`kDirect`) 和异步/队列 (`kQueued`)。
    * **自动生命周期管理**: 使用 `std::weak_ptr` 管理订阅者，并提供 `z3y::ScopedConnection` 实现 RAII 自动取消订阅。
    * **异步异常安全**: 宿主可通过 `SetExceptionHandler` 捕获 `kQueued` 回调中抛出的异常，防止工作线程崩溃。
* 🔍 **内省 (Introspection)**：
    * 提供 `IPluginQuery`核心服务，允许在运行时查询：
        * 所有已加载的插件。
        * 所有已注册的组件/服务。
        * 某个接口 (IID) 的所有实现。
* 🛡️ **生命周期管理**：
    * `IComponent` 提供 `Initialize()` 和 `Shutdown()` 虚函数。
    * 框架保证在卸载插件前，按**LIFO (后进先出)**顺序调用所有单例的 `Shutdown()` 方法，允许插件安全地释放依赖关系。
* 🛠️ **现代 CMake 构建**：
    * 使用 `target_...` 命令和 `INTERFACE` 库（如 `interfaces_demo`）管理依赖。
    * 使用 `install(EXPORT ...)` 生成 CMake 配置文件，使框架本身可以作为一个 SDK 被其他项目通过 `find_package()` 轻松集成。

## 项目结构

```
z3y_plugin_framework/
├── framework/                # [SDK] 框架的公共头文件 (API)
├── src/
│   ├── z3y_plugin_manager/   # 框架核心实现 (PluginManager, EventBus)
│   ├── host_console_demo/    # [示例] 宿主 (Host) 应用程序
│   ├── interfaces_demo/      # [示例] 演示用的插件接口 (IDemoLogger, IDemoSimple...)
│   ├── plugin_demo_core_services/ # [示例] 提供基础服务 (LoggerService)
│   ├── plugin_demo_runner/   # [示例] 演示模块执行器 (IDemoRunner)
│   └── plugin_demo_module... # [示例] 各种演示模块
├── CMakeLists.txt            # 根 CMake
└── CMakeSettings.json        # VS Code / VS 2022 配置
```

## 如何构建

### 依赖
* C++17 编译器
* CMake (3.15 或更高版本)

### 构建步骤

本项目使用 CMake。

1.  **配置 (Configure)**:
    ```bash
    # 在项目根目录创建并进入一个构建目录
    cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
    ```

2.  **构建 (Build)**:
    ```bash
    cmake --build build
    ```
    (或者在 Visual Studio / VS Code 中直接按 F5 运行 `host_console_demo`)

3.  **运行 (Run)**:
    可执行文件和插件 DLL/SO 将被输出到 `build/bin` 目录。
    ```bash
    ./build/bin/host_console_demo
    ```

4.  **安装 (Install) (可选)**:
    如果您想将框架安装为一个 SDK（例如安装到 `build/sdk`）：
    ```bash
    cmake --install build
    ```

## 快速开始

使用本框架分为三个步骤：

### 1. 定义一个接口 (Interface)

接口是一个纯抽象类，它继承自 `z3y::IComponent` 并使用 `Z3Y_DEFINE_INTERFACE` 宏。

```cpp
// src/interfaces_demo/i_demo_logger.h
#pragma once
#include "framework/z3y_define_interface.h" // 包含 IComponent 和 Z3Y_DEFINE_INTERFACE
#include <string>

namespace z3y {
namespace demo {

/**
 * @class IDemoLogger
 * @brief 示例“服务”接口 (日志服务)。
 */
class IDemoLogger : public virtual IComponent {
public:
    //! [插件开发者核心]
    //! 定义接口的元数据 (IID, Name, Version)
    Z3Y_DEFINE_INTERFACE(IDemoLogger, "z3y-demo-IDemoLogger-IID-B1B542F8", 1, 0);

    /**
    * @brief 记录一条消息。
    * @param[in] message 要记录的字符串消息。
    */
    virtual void Log(const std::string& message) = 0;
};

} // namespace demo
} // namespace z3y
```

### 2. 实现一个插件 (Plugin)

插件是一个 `.dll` 或 `.so`，它包含一个或多个接口的实现。

**`demo_logger_service.h`**
```cpp
// src/plugin_demo_core_services/demo_logger_service.h
#pragma once
#include <mutex>  // 用于 std::mutex
#include "framework/z3y_define_impl.h"    // 包含 PluginImpl, Z3Y_DEFINE_COMPONENT_ID
#include "interfaces_demo/i_demo_logger.h"  // 包含 IDemoLogger 接口

namespace z3y {
namespace demo {

/**
 * @class DemoLoggerService
 * @brief IDemoLogger 接口的默认实现。
 */
class DemoLoggerService : public PluginImpl<DemoLoggerService, IDemoLogger> {
public:
    //! [插件开发者核心]
    //! 定义组件的唯一 ClassId
    Z3Y_DEFINE_COMPONENT_ID("z3y-demo-DemoLoggerService-UUID-C50A10B4");

    DemoLoggerService();
    virtual ~DemoLoggerService();

    /**
     * @brief [实现] IDemoLogger::Log 接口。
     * @param[in] message 要打印的消息。
     */
    void Log(const std::string& message) override;

private:
    std::mutex mutex_;
};

} // namespace demo
} // namespace z3y
```

**`demo_logger_service.cpp`**
```cpp
// src/plugin_demo_core_services/demo_logger_service.cpp
#include "demo_logger_service.h"
#include <iostream>  // 用于 std::cout
#include <mutex>     // 用于 std::lock_guard
#include "framework/z3y_define_impl.h"  // 包含 Z3Y_AUTO_REGISTER_SERVICE

// [!! 核心 !!]
// 自动注册：将 DemoLoggerService 注册为一个单例服务 (Service)
// - 别名: "Demo.Logger.Default"
// - 默认: true (允许 z3y::GetDefaultService<IDemoLogger>() 找到它)
Z3Y_AUTO_REGISTER_SERVICE(z3y::demo::DemoLoggerService, "Demo.Logger.Default", true);

namespace z3y {
namespace demo {
    
    DemoLoggerService::DemoLoggerService() {
        std::lock_guard lock(mutex_);
        std::cout << "  [DemoLoggerService] Service Created (Constructor)." << std::endl;
    }

    DemoLoggerService::~DemoLoggerService() {
        std::lock_guard lock(mutex_);
        std::cout << "  [DemoLoggerService] Service Destroyed (Destructor)."
            << std::endl;
    }

    void DemoLoggerService::Log(const std::string& message) {
        std::lock_guard lock(mutex_);
        std::cout << "  [DemoLoggerService] " << message << std::endl;
    }

} // namespace demo
} // namespace z3y
```

**`plugin_entry.cpp`**
每个插件（DLL/SO）**必须**有一个 `.cpp` 文件包含 `Z3Y_DEFINE_PLUGIN_ENTRY` 宏。
```cpp
// src/plugin_demo_core_services/plugin_entry.cpp
#include "framework/z3y_define_impl.h"

// [!! 核心 !!]
// 自动定义 z3yPluginInit 函数。
// 它会自动执行本插件中所有的 Z3Y_AUTO_REGISTER_... 任务。
Z3Y_DEFINE_PLUGIN_ENTRY;
```

### 3. 在宿主 (Host) 中使用

宿主应用程序 (EXE) 负责创建 `PluginManager`、加载插件并使用服务。

```cpp
// src/host_console_demo/main.cpp
#include "framework/z3y_framework.h"       // 包含宿主所需的一切
#include "interfaces_demo/i_demo_runner.h" // 宿主依赖的业务接口
#include "interfaces_demo/i_demo_logger.h" // 宿主依赖的业务接口
#include <iostream>
#include <filesystem>

int main(int argc, char* argv[]) {
    try {
        // 1. 创建 PluginManager
        z3y::PluginPtr<z3y::PluginManager> manager = z3y::PluginManager::Create();

        // 2. 确定插件目录 (exe 所在目录)
        std::filesystem::path exe_dir = ".";
        if (argc > 0 && argv[0]) {
            exe_dir = std::filesystem::path(argv[0]).parent_path();
        }
        std::cout << "\n[Host] Loading all plugins from: " << exe_dir.string() << std::endl;

        // 3. [核心] 加载目录中的所有插件 (DLL/SO)
        manager->LoadPluginsFromDirectory(exe_dir, true);

        // 4. [核心] 获取服务
        // 宿主不关心 DemoLoggerService，只关心 IDemoLogger 接口
        auto logger = z3y::GetDefaultService<z3y::demo::IDemoLogger>();
        logger->Log("Host acquired demo logger service!");
        
        // 5. [核心] 获取另一个服务并执行业务逻辑
        auto demo_runner = z3y::GetDefaultService<z3y::demo::IDemoRunner>();
        demo_runner->RunAllDemos();

        // 9. 安全清理
        demo_runner.reset();
        logger.reset();
        
        // 10. 卸载所有插件 (这将安全调用所有服务的 Shutdown())
        manager->UnloadAllPlugins();
        
        // 11. 销毁管理器
        manager.reset();

    } catch (const z3y::PluginException& e) {
        std::cerr << "[Host] [FATAL] PluginException: " << e.what() << std::endl;
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "[Host] [FATAL] std::exception: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
```

## 高级功能

### 事件总线 (Event Bus)

**订阅事件 (RAII 方式):**
```cpp
#include "framework/z3y_define_impl.h"
#include "framework/connection.h"
#include "interfaces_demo/i_demo_logger.h" // 假设需要日志

// 假设 MyDemoEvent 已在 "demo_events.h" 中定义
// struct MyDemoEvent : public z3y::Event { ... };

class MySubscriber : public z3y::PluginImpl<MySubscriber, ...> {
    z3y::ScopedConnection m_conn; // [核心] 使用 ScopedConnection
    z3y::PluginPtr<z3y::demo::IDemoLogger> m_logger;

    void Initialize() override {
        // [核心] 使用全局辅助函数订阅
        m_conn = z3y::SubscribeGlobalEvent<MyDemoEvent>(
            shared_from_this(), 
            &MySubscriber::OnMyEvent,
            z3y::ConnectionType::kQueued // 异步接收
        );
        
        // (懒加载 logger)
        try {
            m_logger = z3y::GetDefaultService<z3y::demo::IDemoLogger>();
        } catch (...) { /* 忽略可选依赖 */ }
    }
    
    void OnMyEvent(const MyDemoEvent& e) {
        if(m_logger) {
            m_logger->Log("Event received!");
        }
    }
    
    // 当 MySubscriber 实例析构时，m_conn 会自动取消订阅
};
```

**发布事件:**
```cpp
// 在任何地方调用 (假设 MyDemoEvent 构造函数需要一个字符串)
z3y::FireGlobalEvent<MyDemoEvent>("Hello!");
```

### 内省 (Introspection)

```cpp
#include "framework/z3y_define_impl.h"
#include "interfaces_demo/i_demo_logger.h"
#include <iostream>

void PrintAllLoggers() {
    try {
        // 1. 获取内省服务
        auto query = z3y::GetService<z3y::IPluginQuery>(z3y::clsid::kPluginQuery);
        
        // 2. [核心] 查找所有实现了 IDemoLogger 接口的组件
        auto details_list = query->FindComponentsImplementing(z3y::demo::IDemoLogger::kIid);

        std::cout << "Found " << details_list.size() << " loggers:" << std::endl;
        for (const auto& details : details_list) {
            std::cout << " - Alias: " << details.alias << std::endl;
            std::cout << "   Source: " << details.source_plugin_path << std::endl;
        }
    } catch (const z3y::PluginException& e) {
        std::cerr << "Failed to query loggers: " << e.what() << std::endl;
    }
}
```

## 贡献

欢迎提交 Pull Requests。

## 许可证

本项目采用 [Apache 2.0 许可证](LICENSE) 发布。

有关详细信息，请参阅 [LICENSE](LICENSE) 文件和 [NOTICE](NOTICE) 文件。