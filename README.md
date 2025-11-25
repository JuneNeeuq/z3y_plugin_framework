# z3y C++ Plugin Framework

![Standard](https://img.shields.io/badge/C%2B%2B-17-blue.svg)
![License](https://img.shields.io/badge/License-Apache%202.0-green.svg)
![Platform](https://img.shields.io/badge/Platform-Windows%20%7C%20Linux%20%7C%20macOS-lightgrey.svg)
![Build](https://img.shields.io/badge/Build-CMake-orange.svg)

**z3y_plugin_framework** 是一个为工业级应用打造的现代 C++17 模块化开发框架。

它的核心设计目标是打造一个**高精度**、**高可靠性**、**高易用性**的基础设施，解决大型 C++ 项目中常见的依赖地狱、启动死锁、跨模块 ABI 兼容性以及可观测性缺失等痛点。

---

## ✨ 核心特性

### 🚀 极致易用 (High Ease of Use)
* **零样板代码**: 插件开发者只需使用 `Z3Y_AUTO_REGISTER_SERVICE` 宏即可完成注册，无需手动编写 `dllmain` 或导出函数。
* **自动接口实现**: 基于 CRTP 的 `z3y::PluginImpl` 基类自动处理 `QueryInterface` 和元数据收集，开发者仅需关注业务逻辑。
* **现代化构建**: 提供完善的 CMake 集成 (`z3y_plugin_framework::interfaces_core`)，支持 `find_package` 导入，开箱即用。

### 🛡️ 高可靠性 (High Reliability)
* **ABI 稳定性**: 核心 `PluginManager` 采用 **Pimpl 模式**，接口层纯虚函数设计，严格隔离实现细节，确保宿主与插件间的二进制兼容性。
* **生命周期管理**: 框架保证 **LIFO (后进先出)** 的安全卸载顺序。`Shutdown()` 钩子配合 `TryGet...` (noexcept) API，杜绝析构期间的 "Use-after-free" 崩溃。
* **异常隔离**: 独有的 **Out-of-Band 异常处理**机制。异步事件回调中的异常会被捕获并路由至宿主注册的 Handler，防止单个插件崩溃导致主进程退出。
* **死锁防御**: 明确的 `Initialize` vs 构造函数职责划分，配合懒加载 (Lazy Loading) 最佳实践，规避静态初始化顺序导致的死锁。

### 🎯 高精度与高性能 (High Precision)
* **非侵入式性能分析 (Profiler)**: 内置纳秒级性能埋点工具。支持 Chrome Tracing (`trace.json`) 导出，可视化分析函数耗时、线程调度和跨线程流 (Flow)。
* **无锁服务定位**: 服务定位器 (`ServiceLocator`) 采用读写锁与原子缓存优化，在高并发下服务获取开销极低。
* **高效事件总线**: 支持**同步/异步**、**广播/单播**模式。异步队列采用批量处理策略，最大限度减少锁竞争。

---

## 📦 项目结构

```text
z3y_plugin_framework/
├── framework/                # [SDK] 核心头文件 (宿主和插件共用，零依赖)
├── src/
│   ├── z3y_plugin_manager/   # [Core] 框架核心引擎 (DLL/SO)
│   ├── interfaces_core/      # [API] 核心服务接口定义 (Logger, Config, Profiler)
│   ├── plugin_spdlog_logger/ # [Impl] 生产级日志插件 (基于 spdlog)
│   ├── plugin_config_manager/# [Impl] 配置管理插件 (基于 nlohmann/json)
│   ├── plugin_profiler/      # [Impl] 性能分析插件
│   ├── host_console_demo/    # [Example] 宿主程序入口示例
│   └── ...                   # 其他演示模块
├── tests/                    # GoogleTest 集成测试套件
└── CMakeLists.txt            # 构建脚本
```

---

## 🔨 快速开始

### 1. 环境准备
* C++17 编译器 (MSVC 2019+, GCC 9+, Clang 10+)
* CMake 3.15+

### 2. 构建项目
```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --config Release
# (可选) 运行集成测试
ctest -C Release
```

### 3. 编写插件三部曲

#### 第一步：定义接口
继承 `IComponent` 并使用 `Z3Y_DEFINE_INTERFACE` 定义元数据。

```cpp
// interfaces/i_calculator.h
#pragma once
#include "framework/z3y_define_interface.h"

class ICalculator : public virtual z3y::IComponent {
public:
    // UUID 必须全局唯一，版本号用于 ABI 检查
    Z3Y_DEFINE_INTERFACE(ICalculator, "com.example.ICalculator-UUID-1234", 1, 0);

    virtual int Add(int a, int b) = 0;
};
```

#### 第二步：实现插件
继承 `PluginImpl` 并使用 `Z3Y_AUTO_REGISTER_...` 宏。

```cpp
// src/my_plugin/calculator_impl.cpp
#include "interfaces/i_calculator.h"
#include "framework/z3y_define_impl.h"

class CalculatorImpl : public z3y::PluginImpl<CalculatorImpl, ICalculator> {
public:
    Z3Y_DEFINE_COMPONENT_ID("com.example.CalculatorImpl-UUID-5678");

    int Add(int a, int b) override { return a + b; }
};

// 自动注册为瞬态组件 (Component)，别名为 "Math.Calc"
Z3Y_AUTO_REGISTER_COMPONENT(CalculatorImpl, "Math.Calc", false);

// 生成插件入口点 (每个 DLL 只需要一个文件包含此宏)
Z3Y_DEFINE_PLUGIN_ENTRY;
```

#### 第三步：宿主调用
宿主程序只依赖接口，不依赖实现。

```cpp
// src/host/main.cpp
#include "framework/z3y_framework.h"
#include "interfaces/i_calculator.h"

int main() {
    // 1. 启动框架
    auto manager = z3y::PluginManager::Create();

    // 2. 加载插件 (自动扫描目录)
    manager->LoadPluginsFromDirectory("./");

    // 3. 创建实例 (依赖注入/服务定位)
    try {
        auto calc = z3y::CreateInstance<ICalculator>("Math.Calc");
        std::cout << "10 + 20 = " << calc->Add(10, 20) << std::endl;
    } catch (const z3y::PluginException& e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }

    // 4. 销毁框架
    z3y::PluginManager::Destroy();
    return 0;
}
```

---

## 🧩 核心模块详解

### 1. 事件总线 (Event Bus)
解耦模块间的通信。支持 RAII 自动取消订阅，防止野指针。

```cpp
// 定义事件
struct LoginEvent : public z3y::Event {
    Z3Y_DEFINE_EVENT(LoginEvent, "evt.user.login"); // 唯一 ID
    std::string username;
    LoginEvent(std::string u) : username(std::move(u)) {}
};

// 订阅 (使用 ScopedConnection 自动管理生命周期)
z3y::ScopedConnection conn = z3y::SubscribeGlobalEvent<LoginEvent>(
    shared_from_this(), 
    [](const LoginEvent& e) { std::cout << "User logged in: " << e.username; },
    z3y::ConnectionType::kQueued // 异步执行 (在工作线程回调)
);

// 发布
z3y::FireGlobalEvent<LoginEvent>("Alice");
```

### 2. 性能分析 (Profiler)
内置 `plugin_profiler`，提供类似 Unity Profiler 的代码级埋点能力。

```cpp
#include "interfaces_profiler/profiler_macros.h"

void ComplexAlgorithm() {
    // 自动记录函数名、耗时、线程ID
    Z3Y_PROFILE_FUNCTION(); 
    
    {
        // 自定义区间
        Z3Y_PROFILE_SCOPE("SubStep1");
        // ... heavy work ...
    }
    
    // 记录数值变化
    Z3Y_PROFILE_COUNTER("MemoryUsage", 1024);
}
```
*生成结果 `trace.json` 可直接拖入 Chrome 浏览器 (`chrome://tracing`) 查看火焰图。*

### 3. 配置管理 (Config)
内置 `plugin_config_manager`，支持类型安全的结构体绑定和自校验。

```cpp
struct MyConfig {
    int port = 8080;
    std::string host = "localhost";
    
    // 宏实现 JSON 序列化与默认值回填
    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(MyConfig, port, host);
    
    // 支持自校验逻辑
    bool Validate(std::string& err) {
        if (port <= 0) { err = "Invalid port"; return false; }
        return true;
    }
};

// 加载配置 (如果文件不存在，会自动生成默认文件)
MyConfig cfg;
config_svc->LoadConfig("plugin_net", "/server", cfg);
```

### 4. 内省 (Introspection)
框架支持自我诊断。

```cpp
auto query = z3y::GetService<z3y::IPluginQuery>(z3y::clsid::kPluginQuery);
// 查找所有实现了 IDemoLogger 的组件
auto details = query->FindComponentsImplementing(z3y::demo::IDemoLogger::kIid);
```

---

## 📄 许可证

本项目采用 [Apache 2.0 许可证](LICENSE) 发布。
您可以免费用于商业项目，但需保留版权声明。

Copyright © 2025 Yue Liu.