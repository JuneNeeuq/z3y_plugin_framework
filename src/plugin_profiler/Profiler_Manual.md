# Z3Y Profiler 性能分析插件保姆级使用手册 (v2.0)

欢迎使用全新重写的 **Z3Y Profiler 性能分析插件**！

本次重写在底层架构上进行了极其深度的极客级优化（如 LCRS 多叉树、L1 Cache Line 对齐消除伪共享、无锁 CAS 数据累加、双重检查锁 DCL 以及 1024 槽位僵尸线程防御等）。

**【对你的承诺】**：你不需要懂任何底层原理。这套插件被设计为**绝对无感侵入**、**全 RAII 自动管理**、**极限零开销**。只要跟着本手册的宏（Macros）复制粘贴，你就能得到工业级的性能报表！

---

## 1. 快速入门：如何加载与配置

### 1.1 前置依赖
Profiler 插件并非孤立存在，它在底层需要记录日志并监听动态配置。因此，在加载 Profiler 之前，**必须优先加载日志插件和配置插件**。

加载顺序如下：
```cpp
// 1. 基础服务支撑
PluginManager::LoadPlugin("plugin_spdlog_logger");
PluginManager::LoadPlugin("plugin_config_manager");
// 2. 加载性能分析插件
PluginManager::LoadPlugin("plugin_profiler");
```

### 1.2 全局配置开关
Profiler 默认是开启的。但它接入了 `IConfigService` 配置中心，你可以随时在你的 `config.json` 中，或者通过代码在**运行时动态关闭/开启**它。关闭后，所有的探针开销将降为绝对的零（直接 return）。

**在 config.json 中配置（可选）：**
```json
{
  "System.Profiler.Enable": true
}
```

---

## 2. 核心魔法：业务代码怎么用？

日常开发中，**绝对不要**去 `new` 任何分析节点，也**不要**去调用 `IProfilerService` 里的虚函数。
你唯一需要做的，就是引入宏头文件：
```cpp
#include "interfaces_profiler/profiler_macros.h"
```

### 🎯 场景一：给无名的工作线程上户口 (多线程排查必备)
如果你通过 `std::thread` 或线程池创建了一个后台线程，默认情况下日志系统只知道它叫 `Thread 14204`，这在排查问题时如同看天书。
**在任何新线程的入口第一行**，一定要使用 `Z3Y_PROFILE_THREAD` 为线程命名。

```cpp
void CameraGrabThreadFunc() {
    Z3Y_PROFILE_THREAD("Camera_Grab_Thread"); // 必须放在线程第一行！
    
    // ... 之后的监控日志都会带上这个清晰的名字 ...
}
```

### 🎯 场景二：我只想看看这个函数执行了多久（最常用）
在函数体的第一行，直接写上 `Z3Y_PROFILE();`。
它会自动获取当前函数名，并在函数执行结束（无论是 `return` 还是抛出异常）时自动计算耗时。

```cpp
void ProcessImage() {
    Z3Y_PROFILE(); // 就这一句，搞定！
    
    // ... 你的复杂图像处理逻辑 ...
}
```

### 🎯 场景三：我想精确监控函数里的某几行代码
如果你觉得监控整个函数太粗，想监控函数里的“初始化”和“处理”两个阶段。使用 `Z3Y_PROFILE_NAMED`，配合大括号 `{}` 划定作用域。

```cpp
void ComplexAlgorithm() {
    {
        Z3Y_PROFILE_NAMED("Step1_Init"); // 监控初始化
        // ... 耗时 5ms ...
    } // 离开大括号，Step1_Init 耗时结算完毕

    {
        Z3Y_PROFILE_NAMED("Step2_Compute"); // 监控计算
        // ... 耗时 20ms ...
    } // 离开大括号，Step2_Compute 耗时结算完毕
}
```

🚨 **【极其致命的死亡警告】** 🚨
`Z3Y_PROFILE_NAMED(name)` 里的 `name` **绝对、绝对、绝对只能填双引号包起来的英文字符串（字符串字面量常量）**！
**严禁**传入 `std::string`，**严禁**传入 `char*` 变量，**严禁**传入任何动态拼接的字符串！
* ❌ 错误：`std::string s = "Step_" + id; Z3Y_PROFILE_NAMED(s.c_str());` （编译会直接报错拦截你）
* ✅ 正确：`Z3Y_PROFILE_NAMED("Step_A");`
* **原因**：为了达到 O(1) 的极限性能，底层完全没有拷贝这个名字，只存了指针。如果传入临时变量，函数结束后指针悬空，下次打印报表必将引发**段错误进程崩溃**。

### 🎯 场景四：后台死循环线程的定期性能汇报
如果你写了一个后台线程，一直在 `while(true)` 循环，你想让它每跑 100 次，或者某次单帧处理超过 50 毫秒时，自动打印一份报表。
请在 `while` 循环内部的开头，使用 `Z3Y_PROFILE_ROOT(名称, 周期, SLA阈值)`。

```cpp
void BackgroundWorker() {
    while (running) {
        // 每处理 100 帧打印一次，或者一旦某一帧超过 33.3 毫秒立刻报警！
        Z3Y_PROFILE_ROOT("Background_Worker_Root", 100, 33.3);
        ProcessImage();
    }
}
```
💡 **【ROOT 数据重置机制说明】**：小白最常问的问题：“第 101 次到 200 次的报表，是包含前 100 次的总和吗？”
**答案是：不是！** 每次触发打印（无论是达到次数还是因为超时），该 Root 节点下的所有统计数据（包括子节点）会**自动清零（Reset）**。每一轮输出都是全新的计算，绝不会被前几天的历史数据无限稀释！

### 🎯 场景五：流水线打动态标签
上面说了不能把动态的 `Barcode` 设为节点名字，那怎么记录动态流水号？用 `Z3Y_PROFILE_TAG(静态键, 动态值)`！

```cpp
void ProcessProduct(const std::string& barcode) {
    Z3Y_PROFILE_NAMED("Inspect_Product");
    // 给这次分析打上动态标签。Value 最大支持 31 个字符，底层会安全深拷贝
    Z3Y_PROFILE_TAG("Barcode", barcode.c_str()); 
}
```

### 🎯 场景六：记录数值和事件次数（非耗时统计）
Profiler 不仅能记时间，还能记状态。
* `Z3Y_PROFILE_VALUE`：记录一次数值。底层会自动帮你算出 历史最大值、最小值、平均值。
* `Z3Y_PROFILE_EVENT`：记录发生了一次事件。底层会累加发生次数。

```cpp
void CheckHardware() {
    Z3Y_PROFILE_NAMED("Hardware_Check");
    Z3Y_PROFILE_VALUE("CPU_Temperature", GetCpuTemp()); // 记录温度极值与均值
    if (DropFrame()) {
        Z3Y_PROFILE_EVENT("Camera_Drop_Frame"); // 记录一次丢帧事件
    }
}
```

### 🎯 场景七：线性面条代码分步监控
如果一个函数像一根面条一样长，里面有几十个步骤，你不想写几十个 `{}`，可以使用**线性流宏**：

```cpp
void LongLinearFunction() {
    Z3Y_PROFILE_LINEAR("Total_Process"); 
    
    Z3Y_PROFILE_NEXT("Step1");
    // 步骤 1 的代码
    
    Z3Y_PROFILE_NEXT("Step2"); // 自动结束 Step1，开始 Step2
    // 步骤 2 的代码
} // 函数结束，自动结算 Step2 和 Total_Process 的总时间
```

---

## 3. 高阶魔法：框架级的无缝穿透

### 3.1 跨插件/动态库 (DLL/SO) 的同步“结界”穿透
Z3Y 是插件化框架，你肯定会遇到：**主程序（EXE）调用 -> 算子插件（DLL_A） -> 通信插件（DLL_B）** 的情况。

**🔥 小白最爽的特性**：在 Z3Y Profiler 中，只要这些调用是**在同一个线程内（同步调用）**，你**什么都不用管**！
底层使用了线程局部存储（TLS），`Z3Y_PROFILE()` 宏能自动穿透 DLL/SO 的物理边界。在主程序写了 `ROOT`，跑到算子插件里写的 `PROFILE` 会像魔法一样完美挂载到主程序的树节点下。**完全不需要手动传递任何 Context 或 ID！**

### 3.2 异步跨线程流式分析
工业软件经常遇到：**主线程接收图像 -> 线程池 Thread A 处理 -> 线程池 Thread B 汇总**。
这种跨线程任务，必须手动使用 `ASYNC` 系列宏，利用统一的 `frame_id` (帧号/流水号) 串接槽位。

**【主线程】：开辟槽位**
```cpp
uint64_t frame_id = current_image.id;
Z3Y_PROFILE_ASYNC_BEGIN("Async_Vision_Pipeline", frame_id, 1, 100.0);
PushToThreadPool(current_image);
```

**【Worker线程】：挂靠干活**
```cpp
void WorkerThreadFunc(Image img) {
    Z3Y_PROFILE_ASYNC_ATTACH(img.id); // 挂靠到对应槽位
    Z3Y_PROFILE_NAMED("Worker_Inference");
}
```

**【收尾线程】：提交释放**
```cpp
void FinalizeThreadFunc(Image img) {
    Z3Y_PROFILE_ASYNC_ATTACH(img.id);
    // ...
    Z3Y_PROFILE_ASYNC_COMMIT(img.id); // 🔥 必须提交！结算总耗时并释放内存槽位
}
```

---

## 4. 输出报表长什么样？

一旦满足 `period` 或 `sla_ms` 触发条件，日志系统就会输出如下极其详细的树状报表：

```text
[Z3Y Profiler] Performance Report | Trigger: Periodic Tick (Period: 100)
Global Context: [Barcode: NV-2026] [Model: YOLO_v26] 
===================================================================================
Node Name                             Count     Avg(ms)   Min(ms)   Max(ms)   %Time
-----------------------------------------------------------------------------------
Background_Worker_Root                100       30.50     28.10     35.60     100.0%
|- ProcessImage                       100       25.00     24.00     28.00     81.9%
  |- Step1_Init                       100       5.00      4.80      5.20      16.3%
  |- Step2_Compute                    100       20.00     19.20     22.80     65.5%
|- UploadData                         100       5.50      4.00      7.60      18.0%
-----------------------------------------------------------------------------------
[Metrics]
 |- CPU_Temperature           (Value)  Count: 100    Avg: 65.40    Max: 72.10
 |- Camera_Drop_Frame         (Event)  Total Occurrences: 2
===================================================================================
```

---

## 5. ⚠️ 终极防暴走避坑指南 ⚠️

如果你发现你的报表没有输出，或者系统异常，请核对是否踩了以下红线：

1. **节点名忘加引号 (UAF段错误防线)**
   * 再次强调，`Z3Y_PROFILE_NAMED(name)` 必须传宏常量 `Z3Y_PROFILE_NAMED("MyStep")`。试图强行传入动态字符串会导致进程瞬间崩溃。
2. **异步流的异常泄漏陷阱 (1024 僵尸槽位断路器)**
   * **这是小白最容易犯的错！** 如果你在 Worker 线程里写了 `throw exception`，或者在某个 `if (error) return;` 提前退出了，导致最后的 `Z3Y_PROFILE_ASYNC_COMMIT(id)` 永远没有被执行到。系统就会永久泄漏一个异步槽位！
   * **后果**：系统底层只预留了 1024 个环形无锁槽位。如果产生 1024 个未闭环的僵尸流，后续所有 `ASYNC_BEGIN` 将全部失效并疯狂报错 **[Profiler Error] Circuit breaker active**。
   * **正确做法**：如果在异步流中途发生了失败，**必须显式调用取消宏**释放槽位：
     ```cpp
     if (image_is_bad) {
         Z3Y_PROFILE_ASYNC_CANCEL(img.id); // 异常中止，安全释放槽位！
         return;
     }
     ```
3. **全局节点撑爆内存 (2,000,000 节点 OOM 防爆阀)**
   * 如果你瞎写了一个宏包装，把“动态条码”当作节点名传给了 `Z3Y_PROFILE_NAMED`，会导致系统中生成几百万个永远不同的树节点，疯狂吃内存。
   * **后果**：当全局节点超过 200 万时，申请新节点将直接返回 `nullptr`，Profiler 会静默停止记录新节点，保全机器物理内存不被你撑爆（OOM）。
4. **递归太深 / 栈深度超限 (128层防线)**
   * 嵌套调用超过 128 层，第 129 层及以后的监控会被系统静默丢弃（防死机），你会丢失数据。
5. **不要随便修改底层锁代码**
   * 本框架底层采用了极尽苛刻的 `_mm_pause()` 自旋锁和双重检查锁定 (DCL)。不要觉得 `std::mutex` 更好就去改它，改了就会引发工业相机的推流线程被系统内核挂起而导致严重的物理丢帧。

享受你的极速性能排查之旅吧！