# 🚀 Plugin Profiler (性能分析器) 使用手册 v1.0

> **致新同事**：
> 欢迎加入项目组！
> 当你觉得程序“卡顿”、“慢”、“掉帧”或者“CPU占用过高”时，不要瞎猜，请使用本工具。
> 
> **一句话原理**：你在代码里埋点（打宏），程序运行时会自动记录时间戳，最后生成一个 `.json` 文件。你把它拖进 Chrome 浏览器，就能看到像电影剪辑轨道一样清晰的时间轴。

---

## 📚 目录

1.  [三分钟快速上手](#1-三分钟快速上手)
2.  [核心功能与宏详解 (这是重点!)](#2-核心功能与宏详解)
    * [2.1 基础函数计时 (Function)](#21-基础函数计时-function)
    * [2.2 自定义区间计时 (Scope)](#22-自定义区间计时-scope)
    * [2.3 附加额外信息 (Msg & Args)](#23-附加额外信息-msg--args)
    * [2.4 超时报警联动 (Log)](#24-超时报警联动-log)
    * [2.5 线性分段计时 (Linear)](#25-线性分段计时-linear)
    * [2.6 其他高级功能 (Counter/Thread/Flow)](#26-其他高级功能-counterthreadflow)
3.  [配置文件说明](#3-配置文件说明)
4.  [如何看结果 (可视化分析)](#4-如何看结果-可视化分析)
5.  [常见问题 (FAQ)](#5-常见问题-faq)

---

## 1. 三分钟快速上手

### 第一步：引入头文件
在需要分析的 `.cpp` 或 `.h` 文件顶部加入：
```cpp
#include "interfaces_profiler/profiler_macros.h"
```

### 第二步：埋点
在你怀疑慢的函数**第一行**加入 `Z3Y_PROFILE_FUNCTION();`。

```cpp
void MySlowFunction() {
    Z3Y_PROFILE_FUNCTION(); // <--- 加这一行就够了！
    
    // ... 你的业务代码 ...
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
}
```

### 第三步：运行与查看
1.  编译并运行你的程序。
2.  执行你的业务操作。
3.  关闭程序（或者在代码里手动调用 `Z3Y_PROFILE_FLUSH()`）。
4.  在程序的 `bin/logs/` 目录下找到 `trace.json` 文件。
5.  打开 Chrome (或 Edge) 浏览器，在地址栏输入：`chrome://tracing`。
6.  把 `trace.json` 文件拖进浏览器窗口。
7.  **完成！** 你现在能看到该函数的耗时条了（按 `W` 键放大查看）。

---

## 2. 核心功能与宏详解

所有宏都以 `Z3Y_PROFILE_` 开头。为了方便记忆，我们按功能分类。

### 2.1 基础函数计时 (Function)
最常用的功能，**懒人专用**。它会自动获取当前函数的名称作为标签。

* **宏**：`Z3Y_PROFILE_FUNCTION()`
* **位置**：放在函数体的第一行。

```cpp
void ProcessImage() {
    Z3Y_PROFILE_FUNCTION(); // 自动记录名为 "ProcessImage" 的条目
    // ... 业务逻辑 ...
}
```

### 2.2 自定义区间计时 (Scope)
如果你不想记录整个函数，只想监控函数内部的**某一个 `for` 循环**或某一段代码块。

* **宏**：`Z3Y_PROFILE_SCOPE("自定义名称")`
* **原理**：利用 C++ 作用域 (RAII) 机制，在当前大括号 `{}` 结束时停止计时。

```cpp
void ComplexLogic() {
    // ... 初始化代码 ...

    { // <--- 手动加个大括号，限制作用域
        Z3Y_PROFILE_SCOPE("CoreAlgorithm"); // 开始计时
        
        for(int i=0; i<1000; ++i) {
            DoMath(i);
        }
    } // <--- 出了这个大括号，"CoreAlgorithm" 计时结束

    // ... 清理代码 ...
}
```

### 2.3 附加额外信息 (Msg & Args)
有时候光知道慢还不够，还得知道是**处理哪个数据**时慢。我们提供了两种方式来记录数据。

#### 方式 A：文本消息 (`_MSG`)
适合给人看的描述性文字。支持 `fmt` 格式化。

* **宏**：`Z3Y_PROFILE_FUNCTION_MSG(fmt, ...)`
* **宏**：`Z3Y_PROFILE_SCOPE_MSG(name, fmt, ...)`

```cpp
void LoadFile(const std::string& filename) {
    // 在时间条上会显示详细参数：Args: { msg: "Loading: texture.png" }
    Z3Y_PROFILE_FUNCTION_MSG("Loading: {}", filename); 
    // ...
}
```

#### 方式 B：结构化参数 (`_ARGS`)
适合给程序分析的 JSON 键值对。**注意：格式字符串必须是合法的 JSON 片段（不要带外层大括号）。**

* **宏**：`Z3Y_PROFILE_SCOPE_ARGS(name, fmt, ...)`

```cpp
void Resize(int w, int h) {
    // 在 Chrome Tracing 里点击条目，详情面板会以表格形式显示：
    // width  | 1920
    // height | 1080
    Z3Y_PROFILE_SCOPE_ARGS("Resize", "\"width\": {}, \"height\": {}", w, h);
    // ...
}
```

### 2.4 超时报警联动 (Log)
不仅记录 Trace 文件，当耗时**超过阈值**（配置文件中 `alert_threshold_ms`）时，自动打印一条 Warning 日志。这对于线上监控非常有用。

**所有带 `_LOG` 后缀的宏**都需要传入一个 `ILogger` 指针。

* **宏**：`Z3Y_PROFILE_FUNCTION_LOG(logger)`
* **宏**：`Z3Y_PROFILE_SCOPE_LOG(logger, name)`
* **宏**：`Z3Y_PROFILE_SCOPE_MSG_LOG(logger, name, fmt, ...)`

```cpp
void NetworkRequest() {
    // 假设配置文件阈值是 30ms
    // 情况1: 耗时 5ms -> 只记录到 trace.json，不打印日志。
    // 情况2: 耗时 100ms -> 记录到 trace.json，并且 logger 会打印一条警告日志。
    Z3Y_PROFILE_FUNCTION_LOG(my_logger_); 
    // ...
}
```

### 2.5 线性分段计时 (Linear)
**痛点**：如果你有一个长函数，分为 Step1, Step2, Step3... 如果用 `SCOPE` 宏，你需要写很多层大括号，代码缩进会很难看。
**解法**：使用线性宏，扁平化记录。

* **开始**：`Z3Y_PROFILE_LINEAR_BEGIN("第一步名称")`
* **下一步**：`Z3Y_PROFILE_NEXT("下一步名称")`

```cpp
void LongPipeline() {
    // 开始第一步 "1.Init"
    Z3Y_PROFILE_LINEAR_BEGIN("1.Init"); 
    Init();

    // 自动结束 "1.Init"，开始 "2.Compute"
    Z3Y_PROFILE_NEXT("2.Compute"); 
    Compute();

    // 自动结束 "2.Compute"，开始 "3.Save"
    // 还可以带消息
    Z3Y_PROFILE_NEXT_MSG("3.Save", "File: {}", "a.txt");
    Save();
    
} // 函数结束，自动记录 "3.Save" 的耗时 以及 "TOTAL" 总耗时
```

### 2.6 其他高级功能 (Counter/Thread/Flow)

| 宏名称 | 功能描述 | 使用示例 | 视觉效果 |
| :--- | :--- | :--- | :--- |
| `Z3Y_PROFILE_COUNTER` | **计数器**。记录数值变化（如内存、FPS）。 | `Z3Y_PROFILE_COUNTER("Memory", 1024);` | 在时间轴上方显示一条名为 "Memory" 的折线图。 |
| `Z3Y_PROFILE_THREAD_NAME` | **线程命名**。给当前线程起个名字。 | `Z3Y_PROFILE_THREAD_NAME("RenderThread");` | 左侧列表不再显示 "Thread 1234"，而是显示 "RenderThread"。 |
| `Z3Y_PROFILE_MARK_FRAME` | **帧标记**。标记一帧的结束。 | `Z3Y_PROFILE_MARK_FRAME("VBlank");` | 在时间轴上画一个小竖条。 |
| `Z3Y_PROFILE_FLOW_START` | **流追踪**。关联跨线程的异步操作。 | `Z3Y_PROFILE_FLOW_START("Req", req_id);` | 画一条连接不同线程事件的箭头线。 |
| `Z3Y_PROFILE_FLUSH` | **强制刷新**。 | `Z3Y_PROFILE_FLUSH();` | 强制将当前线程缓存的数据写入文件。建议在短生命周期线程退出前调用。 |

---

## 3. 配置文件说明

配置文件路径：`bin/config/profiler_config.json`。
**支持热重载**：程序运行时修改此文件并保存，配置立即生效，无需重启！

```json
{
  "settings": {
    // [总开关] false = 关闭一切功能，性能损耗降为 0。生产环境如果没问题建议关掉。
    "global_enable": true,

    // [Trace开关] true = 生成 trace.json 文件。
    "enable_tracing": true,
    "trace_file": "logs/trace.json",

    // [报警阈值] 配合 _LOG 宏使用。耗时超过 30ms 的操作才会被打印到日志文件。
    "alert_threshold_ms": 30.0,

    // [控制台输出] 也就是 printf。极慢，严重影响性能，除非调试否则千万别开。
    "enable_console_log": false,

    // [高级规则] 可以针对特定模块设置不同的报警阈值
    "rules": [
      {
        "matcher": "Algo.DeepLearning", // 匹配 Category 前缀
        "threshold_ms": 200.0           // 深度学习模块放宽到 200ms 才报警
      }
    ]
  }
}
```

---

## 4. 如何看结果 (可视化分析)

1.  **打开工具**：Chrome 或 Edge 浏览器访问 `chrome://tracing`。
2.  **加载数据**：点击 `Load` 按钮或直接拖入生成的 `trace.json`。
3.  **操作快捷键 (这是精髓)**：
    * **`W`**: 放大 (Zoom In)。查看微秒级细节。
    * **`S`**: 缩小 (Zoom Out)。查看宏观全貌。
    * **`A` / `D`**: 左/右平移时间轴。
    * **鼠标左键拖拽**：选择一个区域，下方面板会显示该区域内所有函数的统计信息（调用次数、平均耗时）。
4.  **分析思路**：
    * **找长条**：X轴越长代表耗时越久。
    * **看空隙**：如果 CPU 时间轴上有大段空白，说明线程在 Sleep 或者等锁（Blocked）。
    * **看堆栈**：一个大长条下面有很多小长条，说明是子函数在耗时；如果长条下面是空的，说明是这个函数自身的代码逻辑（Self Time）耗时。

---

## 5. 常见问题 (FAQ)

**Q: 加上工具后程序变慢了很多？**
A: 记录时间戳和写文件都有开销。
* **切记**：不要在极高频的循环内部（如每秒百万次的 `GetPixel`）里打点。
* **解决**：在循环的外层加 `SCOPE` 即可。

**Q: Release 版本编译报错？**
A: 本工具设计为 Release 可用。如果想在 Release 版本完全移除代码，请在 CMake 中定义预处理器宏 `Z3Y_PROFILER_DISABLE`，这样所有宏都会变成空操作。

**Q: JSON 文件只有开头没有结尾，浏览器打不开？**
A: 这是因为程序非正常退出（Crash）或没有 Flush。
* **补救**：用记事本打开 json，手动在文件末尾加上 `]}` 保存即可。
* **预防**：在 `main` 函数退出前，或线程退出前，调用 `Z3Y_PROFILE_FLUSH()`。

**Q: 为什么有的函数明明运行了，Trace 里却没有？**
A: 可能是被过滤了。
1.  检查 `global_enable` 是否为 true。
2.  如果使用了规则过滤，检查是否因为耗时太短被忽略了。
3.  检查线程是否还没 Flush 数据（数据还在内存缓冲里）。

---
> **End of User Guide**
> 遇到任何问题，请联系架构组。