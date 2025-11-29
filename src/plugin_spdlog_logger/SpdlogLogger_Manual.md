# 生产级日志插件 (SpdlogLogger) 完全手册

**版本**: 2.0.0
**适用对象**: 研发工程师 (Dev), 运维工程师 (Ops), 现场实施人员
**最后更新**: 2025-11-21

---

## 📚 1. 核心架构与设计理念

本插件基于 `spdlog` 构建，专为高并发、高可靠的工业环境设计。在阅读配置文档前，请先理解以下核心机制：

1.  **异步写入架构 (Async Architecture)**:
    * 所有的日志调用（`Z3Y_LOG_INFO` 等）**不会**直接写磁盘。
    * 日志消息会被立即推入一个内存环形队列（RingBuffer）。
    * 由后台独立线程负责从队列取出消息并写入文件/控制台。
    * **优势**: 业务线程几乎零等待，性能极高。

2.  **生命周期豁免 (Crash Resilient)**:
    * 插件采用“只刷新、不关闭”的退出策略。即使主程序崩溃或在析构阶段发生异常，日志库仍尽力保证底层资源有效，防止二次崩溃。

3.  **全链路 UTF-8 (Encoding Safe)**:
    * 原生支持 Windows/Linux 跨平台中文路径，无需担心乱码导致文件创建失败。

---

## 📖 2. 场景化配置食谱 (Cookbook)

> **💡 核心逻辑**：日志去哪里，由 **Rules (规则)** 决定。Rules 将 **Logger (名字)** 和 **Sinks (输出地)** 绑定在一起。

### 场景 A：标准开发调试 (既看屏幕，又存文件)
**需求**：开发阶段，希望在控制台看到实时日志，同时保留文件记录以备查。
**配置方法**：在 `sinks` 中定义一个控制台 Sink 和一个文件 Sink，并在 `default_rule` 中同时引用它们。

```json
{
  "sinks": {
    "console_dev": { "type": "stdout_color_sink", "level": "Trace" },
    "file_debug":  { "type": "daily_file_sink", "base_name": "logs/debug.log", "level": "Debug" }
  },
  "default_rule": {
    "sinks": ["console_dev", "file_debug"]
  }
}
```

### 场景 B：高性能生产环境 (只存文件，不打印屏幕)
**需求**：正式运行环境，关闭控制台打印以提升性能（控制台IO非常慢），只保留轮转日志。
**配置方法**：只定义文件 Sink，且 `default_rule` 只指向文件。

```json
{
  "sinks": {
    "prod_file": { 
      "type": "rotating_file_sink", 
      "base_name": "logs/app.log", 
      "max_size": 10485760, // 10MB
      "max_files": 10,      // 保留10个
      "level": "Info"       // 生产环境通常只看 Info
    }
  },
  "default_rule": {
    "sinks": ["prod_file"]
  }
}
```

### 场景 C：模块隔离 (把特定模块日志存到独立文件)
**需求**：新增了一个名为 `Algorithm.Vision` 的视觉算法模块，数据量很大，想把它单独存到一个叫 `vision.log` 的文件里，不干扰主日志。
**配置方法**：
1.  **代码侧**：获取 Logger 时使用特定前缀 `log_service->GetLogger("Algorithm.Vision")`。
2.  **配置侧**：新增一条 Rule 专门匹配这个前缀。

```json
{
  "sinks": {
    "main_log":   { "type": "daily_file_sink", "base_name": "logs/system.log", "level": "Info" },
    "vision_log": { "type": "rotating_file_sink", "base_name": "logs/vision_data.log", "max_size": 52428800, "max_files": 3, "level": "Debug" }
  },
  "rules": [
    {
      "matcher": "Algorithm.Vision",   // <--- 匹配代码中的名字前缀
      "sinks": ["vision_log"]          // <--- 只流向 vision_log，不流向 main_log
    }
  ],
  "default_rule": {
    "sinks": ["main_log"]              // <--- 其他所有模块流向 system.log
  }
}
```

### 场景 D：详细调试格式 (显示文件名、行号、函数)
**需求**：调试崩溃问题，需要知道日志具体是哪一行代码打印的。
**配置方法**：修改 `global_settings` 中的 `format_pattern`。

```json
{
  "global_settings": {
    // %s:文件名, %#:行号, %!:函数名, %t:线程ID
    "format_pattern": "[%Y-%m-%d %H:%M:%S.%e] [%^%L%$] [%t] [%s:%#] [%!] %v"
  }
}
```
**输出效果**：
`[2023-11-21 10:00:01.123] [I] [1024] [main.cpp:42] [InitSystem] System started.`

---

## ⚙️ 3. 配置文件参数详解 (`logger_config.json`)

配置文件路径通常位于程序执行目录。

### 3.1 全局设置 (`global_settings`)

影响整个日志系统的行为。

#### 日志格式速查表 (`format_pattern`)

以下是常用的格式占位符，您可以自由组合。

| 占位符 | 含义 | 示例 |
| :--- | :--- | :--- |
| **基础信息** | | |
| `%v` | 实际的日志消息文本 | `User logged in` |
| `%t` | 线程 ID | `1234` |
| `%P` | 进程 ID | `8888` |
| `%n` | Logger 的名字 | `System.Network` |
| **日期与时间** | | |
| `%Y` | 年份 (4位) | `2023` |
| `%m` | 月份 (01-12) | `11` |
| `%d` | 日期 (01-31) | `21` |
| `%H` | 小时 (00-23) | `14` |
| `%M` | 分钟 (00-59) | `55` |
| `%S` | 秒 (00-59) | `02` |
| `%e` | 毫秒 (3位) | `456` |
| `%f` | 微秒 (6位) | `456789` |
| `%z` | 时区偏移 | `+08:00` |
| **日志级别** | | |
| `%l` | 级别全称 | `info`, `warning`, `error` |
| `%L` | 级别缩写 (大写) | `I`, `W`, `E` |
| **源代码定位** | *(需在代码中使用 Z3Y_LOG_... 宏才有效)* | |
| `%s` | 源文件名 (不含路径) | `main.cpp` |
| `%g` | 源文件名 (含全路径) | `/home/user/project/src/main.cpp` |
| `%#` | 行号 | `42` |
| `%!` | 函数名 | `InitSystem` |
| **颜色与对齐** | | |
| `%^` | 开始颜色范围 (仅控制台有效) | (无可见字符，影响后续颜色) |
| `%$` | 结束颜色范围 | (重置颜色) |

**配置参数说明**：

| 参数名 | 类型 | 默认值 | 详细说明与影响 |
| :--- | :--- | :--- | :--- |
| `format_pattern` | string | (见上表) | **日志格式模板**。决定了每行日志长什么样。 |
| `async_queue_size` | int | `8192` | **异步队列深度**。<br>**调优**：如果是海量日志场景（如每秒10万条），建议调大到 `65536` 或更多。<br>**注意**：必须是 2 的幂次方。占用内存 = `size * sizeof(LogMsg)`。 |
| `async_overflow_policy` | string | `"block"` | **队列满载策略**。<br>`"block"`: **阻塞业务线程**，直到队列有空位。保证不丢日志，但在磁盘IO慢时会卡顿业务。<br>`"overrun_oldest"`: **丢弃最旧日志**。保证业务流畅，但可能丢日志。**高实时性系统推荐此项**。 |
| `flush_interval_seconds` | int | `5` | **定期刷盘间隔**。<br>每隔多少秒强制将内存缓冲写入磁盘。防止程序突然断电导致最后几秒日志丢失。 |
| `flush_on_level` | string | `"error"` | **触发刷盘的最低等级**。<br>当遇到 `Error` 或 `Fatal` 日志时，立即执行刷盘。确保崩溃前的错误信息一定被记录。 |

### 3.2 输出目标 (`sinks`)

定义了日志的“目的地”。Key 是自定义的 Sink 名字（如 `file_main`），Value 是配置对象。

#### 通用参数 (所有 Sink 都有)
| 参数名 | 类型 | 必填 | 说明 |
| :--- | :--- | :--- | :--- |
| `type` | string | 是 | `stdout_color_sink` (控制台), `daily_file_sink` (按天), `rotating_file_sink` (按大小) |
| `level` | string | 否 | **Sink 级过滤**。只有 >= 此等级的日志才会被写入该 Sink。<br>例如：可以设置控制台只显示 `Info`，而文件记录 `Debug`。 |

#### 专用参数：按天轮转 (`daily_file_sink`)
*适用场景：服务器后端，运维习惯按日期归档日志。*

| 参数名 | 类型 | 说明 |
| :--- | :--- | :--- |
| `base_name` | string | **日志路径**。如 `logs/server.log`。系统会自动在每天 00:00 切割，旧文件重命名为 `server.log.2023-11-21`。 |

#### 专用参数：按大小轮转 (`rotating_file_sink`)
*适用场景：嵌入式设备、桌面软件，需要限制总磁盘占用。*

| 参数名 | 类型 | 说明 |
| :--- | :--- | :--- |
| `base_name` | string | **日志路径**。如 `logs/app.log`。 |
| `max_size` | int | **单文件最大字节数 (Bytes)**。<br>⚠️ **注意单位**！不要填成 5 (那只有5字节)。<br>推荐：`5242880` (5MB) 或 `10485760` (10MB)。 |
| `max_files` | int | **保留历史文件数量**。<br>总占用空间 ≈ `max_size * (max_files + 1)`。 |

### 3.3 路由规则 (`rules` & `default_rule`)

系统通过 **最长前缀匹配** 算法决定将日志分发给哪些 Sinks。

* **`rules` (列表)**:
    * `matcher`: 字符串前缀。例如 `"System.Net"` 会匹配 `"System.Net.Tcp"` 和 `"System.Net.Http"`。
    * `sinks`: 对应的 Sink 名字列表。
* **`default_rule` (对象)**:
    * 如果 logger 名字没有匹配到任何 `rules`，则使用此配置。

---

## 🔧 4. 运维指令 (运行时动态调整)

本插件支持在**不重启进程**的情况下，通过代码调用接口动态修改日志行为。通常配合 HTTP API 或调试控制台使用。

### 4.1 动态调整日志等级 (`SetLevel`)

当线上环境出现 Bug，但当前日志等级是 `Info` 看不到详细信息时：

```cpp
auto log_mgr = z3y::GetDefaultService<ILogManagerService>();

// 指令 1: 开启 "Network" 模块的 Debug 日志
log_mgr->SetLevel("Network", LogLevel::Debug);
// 效果: "Network.Tcp", "Network.Http" 变为 Debug，其他模块不变。

// 指令 2: 开启全局 Trace (慎用，数据量极大)
log_mgr->SetLevel("", LogLevel::Trace);
```

### 4.2 强制刷盘 (`Flush`)

在程序准备执行某些高危操作（如自升级、重启）前，手动调用：

```cpp
log_mgr->Flush();
```

---

## 💻 5. 开发者指南 (C++ Integration)

### 5.1 引入与依赖
* **头文件**: 包含 `interfaces_core/z3y_log_macros.h`。
* **链接**: 链接 `z3y_plugin_manager` 和 `interfaces_core`。

### 5.2 标准编码规范

1.  **初始化 (Main.cpp)**:
    ```cpp
    auto log_mgr = z3y::GetDefaultService<z3y::interfaces::core::ILogManagerService>();
    // 务必检查返回值，处理配置文件路径错误的情况
    if (!log_mgr->InitializeService("config/logger_config.json", "logs")) {
        // 回退逻辑...
    }
    ```

2.  **获取 Logger (MyPlugin.cpp)**:
    * **原则**: 在 `Initialize` 阶段获取，**严禁**在每帧循环中调用 `GetLogger`。
    * **命名**: 使用 **点号分隔** 的层级命名法。
    ```cpp
    void MyPlugin::Initialize() {
        // 推荐: 公司名.项目名.模块名
        m_logger = log_mgr->GetLogger("Z3Y.Demo.VideoModule");
    }
    ```

3.  **打印日志**:
    * 使用宏 `Z3Y_LOG_INFO` 而不是 `m_logger->Log`。宏会自动处理文件名行号，且在 LogLevel 不满足时零开销。
    * 使用 `{}` 占位符 (fmt 语法)，**不要**手动拼接字符串。
    ```cpp
    // ✅ 正确 (高效)
    Z3Y_LOG_INFO(m_logger, "User {} login from {}", user_id, ip_addr);

    // ❌ 错误 (低效，且易错)
    // Z3Y_LOG_INFO(m_logger, "User " + std::to_string(user_id) + " login...");
    ```

---

## 🚧 6. 常见问题与禁忌 (FAQ)

### Q1: `rotating_file_sink` 为什么文件大小没到 `max_size` 就切分了？
**A**: spdlog 的切分检查是在写入时进行的。如果你的程序在极短时间内崩溃或重启，可能会导致文件未满。另外，请再次检查配置的 `max_size` 单位是否为 **字节**。1MB = 1024*1024 = 1048576，而不是 1。

### Q2: 为什么我在 Windows 上看到的日志文件名是乱码？
**A**: 本插件已内置 `Utf8ToPath` 处理。请确保你的代码源文件 (`.cpp`) 是 **UTF-8** 编码，且传递给配置文件的路径字符串也是 UTF-8 编码。

### Q3: 为什么程序崩溃时最后几条日志丢了？
**A**: 这是异步日志的特性。日志在内存队列中，若进程被强制杀死 (`kill -9`) 或发生段错误 Crash，内存数据来不及落盘。
* **建议**: 在 catch 块中调用 `log_mgr->Flush()`。
* **配置**: 将 `flush_interval_seconds` 设小一点（如 1秒），或者确保关键错误使用 `Error` 级别（触发 `flush_on_level`）。

### Q4: 可以在析构函数里打印日志吗？
**A**: **可以，但有风险**。
* 插件已做防护，在 `Shutdown` 后不会关闭 spdlog，允许析构打印。
* **但是**：如果宿主程序已经完全退出了 `main` 函数，静态变量开始析构，此时 C++ 运行时环境可能已不稳定，打印日志可能无效。建议尽早在 `Shutdown` 阶段完成清理日志。