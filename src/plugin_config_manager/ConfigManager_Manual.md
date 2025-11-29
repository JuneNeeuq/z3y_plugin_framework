# 📘 Plugin Config Manager 完全使用手册 (v2.2)

> **核心定位**：一个基于 `nlohmann/json` 的工业级配置管理系统。
> **能力概览**：类型安全绑定、**多文件管理**、**单变量读写**、热重载、原子落盘。

---

## 1. 🚀 快速集成 (Integration)

### 1.1 CMake 依赖
```cmake
# CMakeLists.txt
find_package(z3y_plugin_framework REQUIRED)

# [关键] 必须链接 nlohmann_json
target_link_libraries(my_plugin 
    PRIVATE 
    z3y_plugin_framework::interfaces_core 
    nlohmann_json::nlohmann_json
)
```

### 1.2 宿主初始化 (Host Only)
在 `main.cpp` 中初始化服务。

```cpp
#include "framework/z3y_framework.h"
#include "interfaces_core/i_config_service.h"

void InitSystem() {
    auto manager = z3y::PluginManager::GetActiveInstance();
    manager->LoadPlugin("plugin_config_manager");

    auto config_svc = z3y::GetDefaultService<z3y::interfaces::core::IConfigManagerService>();
    
    // [设置根目录] 所有的 .json 文件都会生成在这个目录下
    // 必须是 UTF-8 编码路径
    if (!config_svc->InitializeService("./configs")) {
        std::cerr << "配置服务初始化失败 (路径不可写或磁盘满)" << std::endl;
    }
}
```

---

## 2. 📂 核心概念：多文件管理 (Domains)

配置服务天然支持将配置分散存储在多个文件中。
**核心规则**：`LoadConfig` 的第一个参数 `domain` = **文件名** (不带后缀)。

### 2.1 场景：模块化配置
假设你的程序有两个模块：**网络模块**和**渲染模块**。

```cpp
void Initialize() {
    auto svc = z3y::GetDefaultService<z3y::interfaces::core::IConfigManagerService>();

    // 1. 操作 configs/network.json
    NetworkConfig net_cfg;
    svc->LoadConfig("network", "Server", net_cfg);

    // 2. 操作 configs/graphics.json
    GraphicsConfig gfx_cfg;
    svc->LoadConfig("graphics", "Resolution", gfx_cfg);
}
```

### 2.2 场景：保存所有更改
框架会追踪哪些文件被修改了（Dirty Flag）。

```cpp
// 仅将 configs/network.json 写入磁盘
svc->Save("network");

// 将所有有变动的文件 (network.json, graphics.json) 全部写入磁盘
svc->SaveAll();
```

---

## 3. 📝 模式 A：结构体绑定 (推荐)

适用于管理复杂的业务配置。

### 3.1 定义结构体
**MyConfig.h**
```cpp
#pragma once
#include <string>
#include <vector>
#include <nlohmann/json.hpp> // [必选] 宏依赖此头文件

struct MyConfig {
    // 1. 定义成员并给默认值 (兜底值)
    int port = 8080;
    std::string ip = "127.0.0.1";

    // 2. 绑定宏 (自动生成序列化代码)
    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(MyConfig, port, ip);
    
    // 3. [可选] 自校验逻辑
    bool Validate(std::string& err) {
        if (port <= 0) { err = "Port must be > 0"; return false; }
        return true;
    }
};
```

### 3.2 加载与使用
```cpp
// MyPlugin.cpp
void Initialize() {
    auto svc = z3y::GetDefaultService<z3y::interfaces::core::IConfigManagerService>();
    
    MyConfig cfg;
    // 从 "app_main.json" 的 "Server" 节点加载
    auto status = svc->LoadConfig("app_main", "Server", cfg);

    if (status == z3y::interfaces::core::ConfigStatus::Success) {
        // 加载成功
    }
}
```

---

## 4. 🎯 模式 B：单变量/容器读写 (灵活)

不想定义结构体？或者只想临时读取某个 `int`？可以直接操作基础类型。

### 4.1 关键语法：JSON Pointer
* **普通 Key**: `"Server"` -> 查找根对象下的 `Server` 字段。
* **路径 Key**: `"/Server/Log/Level"` -> 以 `/` 开头，查找深层嵌套节点。

### 4.2 读写 int/string/bool
假设 `app.json` 内容为：`{ "UI": { "Window": { "Width": 1920 } } }`

```cpp
void ResizeWindow() {
    auto svc = z3y::GetDefaultService<z3y::interfaces::core::IConfigManagerService>();

    // [读取]
    int width = 800; // 默认值
    // 模板参数 <int> 通常可省略，编译器会自动推导
    svc->LoadConfig("app", "/UI/Window/Width", width);
    
    // [修改]
    // 直接写入深层节点。如果中间节点(UI, Window)不存在，会自动创建！
    svc->SetConfig("app", "/UI/Window/Width", 1024);
    
    // [保存]
    svc->Save("app");
}
```

### 4.3 读写 STL 容器 (Vector, Map)
直接把 JSON 数组映射为 `std::vector`，把对象映射为 `std::map`。

```cpp
void UpdateWhitelist() {
    // 默认白名单
    std::vector<std::string> ips = {"127.0.0.1"};
    
    // 直接加载到 vector
    // 对应 JSON: { "Firewall": { "Whitelist": ["127.0.0.1", "192.168.1.1"] } }
    svc->LoadConfig("security", "/Firewall/Whitelist", ips);
    
    // 修改并保存
    ips.push_back("10.0.0.1");
    svc->SetConfig("security", "/Firewall/Whitelist", ips);
    
    svc->Save("security");
}
```

---

## 5. ⚙️ API 行为详解 (Reference)

### 5.1 读取 API (`LoadConfig`)

```cpp
ConfigStatus LoadConfig<T>(string domain, string key, T& out_val);
```

* **返回值**: 
    * `Success`: 文件存在且读取成功。
    * `CreatedDefault`: 文件不存在或 Key 不存在。`out_val` **保持默认值**，且内存中已创建该节点（**注意：此时磁盘上还没文件，需调用 Save**）。
    * `Error`: 文件损坏 (JSON 语法错误) 或 类型不匹配。`out_val` **保持原值不被污染**。
* **参数 `out_val`**: 
    * 这是一个**引用参数 (Reference)**。
    * 你必须先在 C++ 中给它赋好初值（即默认值）。
    * 如果加载成功，它的值会被覆盖；如果失败，它保持原样。

### 5.2 写入 API (`SetConfig` & `Save`)

* `SetConfig(domain, key, val)`: 
    * **纯内存操作**。极快，线程安全。
    * 它会将 `domain` 标记为 Dirty。
* `Save(domain)`: 
    * **磁盘 IO 操作**。
    * 只有调用它，内存里的修改才会落盘。
    * **原子写入**：使用 `tmp` 文件 + `rename` 机制，断电不会导致文件损坏。

### 5.3 运维 API

* `Reload(domain)`: 丢弃内存修改，强制重读磁盘文件。触发 `ConfigurationReloadedEvent`。
* `ResetConfig(domain)`: **物理删除**磁盘文件，清空内存。触发 `ConfigurationReloadedEvent`。

---

## 6. 🚫 避坑指南 (禁忌事项)

1.  ❌ **禁止手动读写文件**: 严禁使用 `fstream` 私自操作 `configs/` 目录下的文件。这会破坏文件锁，导致数据被覆盖。
2.  ❌ **不要在循环中 Save**: `Save()` 是磁盘操作。如果你在 `Update()` 循环里每秒调用 60 次 `Save`，性能会爆炸。请仅在关键数据变更时保存。
3.  ✅ **路径必须 UTF-8**: `LoadConfig` 和 `InitializeService` 的字符串参数在 Windows 上必须是 UTF-8 编码。不要传 GBK 字符串。
4.  ✅ **默认值必填**: `LoadConfig` 依赖你传入的变量的初始值作为默认值。
    * 错误：`int port; svc->LoadConfig(..., port);` // port 是随机值！
    * 正确：`int port = 8080; svc->LoadConfig(..., port);`

---

## 7. 🔔 高级技巧：监听热重载

当运维人员手动修改了 JSON 文件，插件如何立即响应？

```cpp
void Initialize() {
    // 订阅配置重载事件
    z3y::SubscribeGlobalEvent<z3y::interfaces::core::ConfigurationReloadedEvent>(
        shared_from_this(), &MyPlugin::OnConfigChanged
    );
}

void OnConfigChanged(const z3y::interfaces::core::ConfigurationReloadedEvent& e) {
    // 过滤：只处理我关心的文件
    if (e.domain == "app_main") {
        Z3Y_LOG_INFO(logger, "检测到配置变更，正在刷新...");
        
        // 重新加载配置
        MyConfig new_cfg;
        svc->LoadConfig("app_main", "Server", new_cfg);
        
        // 应用新配置
        ApplySettings(new_cfg);
    }
}
```