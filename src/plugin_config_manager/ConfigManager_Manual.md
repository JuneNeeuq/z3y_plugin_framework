# z3y 框架核心组件：配置管理服务 (ConfigManager) 官方开发者指南 v3.0

## 1. 概述与核心价值
欢迎使用 z3y 框架的核心配置中枢。`ConfigProviderService` 旨在为你解决工业软件开发中最头疼的三个问题：
1. **模块解耦**：A 插件和 B 插件需要共享参数？不需要互相 include 头文件，通过本服务即可完成数据交互。
2. **状态同步**：UI 界面、后端内存、物理磁盘文件，三者状态永远保持绝对一致 (SSOT)。
3. **高并发安全**：即便 10 个线程同时狂暴读写，底层也能保证绝对的线程安全与防死锁。

---

## 2. 🚀 新手快速起步 (Quick Start)

在一个全新的插件中，你只需要三步即可完美接入配置服务。

### 2.1 引入必备头文件
```cpp
// 引入配置服务的纯虚接口与基础类型
#include "interfaces_core/i_config_service.h"
```

### 2.2 完整的“Hello World”插件示例
这是每一个新手必须掌握的标准模板（注意体会 `ConnectionGroup` 的使用）：

```cpp
class MyFirstPlugin : public z3y::PluginImpl<MyFirstPlugin> {
private:
    // 【强制规范 1】必须作为类成员变量声明，用于自动管理订阅的生命周期！
    z3y::interfaces::core::ConnectionGroup conn_group_; 

public:
    void Initialize() override {
        // 【强制规范 2】通过 ServiceLocator 获取配置服务实例
        auto config = z3y::GetDefaultService<z3y::interfaces::core::IConfigService>();
        if (!config) return;

        // 【核心操作】向系统注册一个名为 "Motion.Speed" 的浮点型参数
        // 使用 += 语法糖将连接托管给 conn_group_
        conn_group_ += config->Builder<double>("Motion.Speed")
            .NameKey("马达速度")
            .Default(50.0)         // 必须提供默认值
            .Min(0).Max(100.0)     // 可选：防呆校验
            .Bind([this](double v) {
                // 当系统初始化，或任何人修改了该值时，这段 Lambda 会被自动触发！
                std::cout << "马达速度已更新为: " << v << std::endl;
                // this->UpdateHardware(v); 
            });
    }

    void Shutdown() override {
        // 插件卸载时，conn_group_ 会自动析构并断开配置回调，彻底杜绝野指针崩溃！
    }
};
```

---

## 3. 核心 API 字典：Builder 语法糖详解

当你想向系统注册一个配置时，请使用 `config->Builder<类型>("路径").属性A().属性B()...终结动作()` 的链式语法。

### 3.1 属性配置表

| 链式方法 | 参数说明 | 作用与呈现效果 |
| :--- | :--- | :--- |
| `Default(T)` | 强类型 `T` | **【必填】** 设定基准默认值，这是底层确定数据类型的唯一依据。 |
| `NameKey(str)` | `string` | 设定 UI 面板上该参数显示的中文/多语言名称。 |
| `GroupKey(str)` | `string` | 一级分组。UI 将根据此字段把参数分门别类放入不同的 Tab 页签。 |
| `SubGroupKey(str)` | `string` | 二级分组。UI 将根据此字段把参数分门别类放入不同的子框中。 |
| `Min(U) / Max(U)` | 数字字面量 | 绝对物理边界。设定后，任何人试图突破此边界的修改都会被底层拦截并报错。 |
| `Step(U)` | 数字字面量 | 步进值。指导 UI 生成带上下箭头的 SpinBox 时，点击一次增减的幅度。 |
| `Enum(vals, keys)`| `vector` | 生成下拉框。`vals` 为后端存的真实值，`keys` 为 UI 显示的选项名。 |
| `Widget(type)` | `WidgetType` | 强制 UI 使用特定控件渲染（如 `kPasswordInput` 显示为星号密码框）。 |
| `ReadOnly(bool)` | `bool` | 将参数设为只读。用于将硬件的状态（如当前温度）只读展示给前台 UI。 |
| `Hidden(bool)` | `bool` | 设为纯后端参数。UI 在获取全量数据时，此参数会被直接过滤隐藏。 |
| `Advanced(bool)` | `bool` | 设为高级参数。UI 默认折叠，勾选“专家模式”后才显示。 |
| `Validator(func)` | `Lambda` | **图灵完备校验**。例如：`.Validator([](int v){ return v%2==0 ? "" : "必须是偶数"; })` |

### 3.2 终结动作（必须调用其一）
* **`.Bind(Callback)`**：注册参数，立刻拿到默认值，并持续监听未来的所有修改。返回 `ScopedConnection` 句柄。
* **`.RegisterOnly()`**：仅仅将参数挂载到系统中，自身不关心它的变化（被动轮询）。无返回值。

---

## 4. 运行时操作：读写、事务与 UI

### 4.1 基础读写 (API 调用)
当你需要主动获取或修改其他模块的参数时：
```cpp
// 1. 安全读取（如果路径写错，自动返回后面的备用值 0）
int count = config->GetValueSafe<int>("Camera.TriggerCount", 0);

// 2. 安全写入（触发合法性校验，通过后自动广播给所有订阅者）
bool ok = config->SetValueSafe<int>("Camera.TriggerCount", count + 1, "Admin");
```

### 4.2 ACID 批量事务 (BatchUpdater)
当你必须同时修改两个互相绑定的参数（如 XYZ 坐标，宽高比例），不允许出现中间态时：
```cpp
auto batch = config->CreateBatch();
batch.Set("ROI.Width", 1920)
     .Set("ROI.Height", 1080);

// 提交事务。如果有任何一个参数校验失败，全量回滚，一个都不会生效。
std::vector<std::string> errors = batch.Commit("Admin");
if (!errors.empty()) {
    std::cerr << "事务失败: " << errors[0] << std::endl;
}
```

### 4.3 UI 前端数据拉取
UI 界面不应自己保存参数，应按需拉取快照渲染：
```cpp
// 懒加载策略：仅拉取用户当前点击的 "相机设置" Tab 下的所有参数快照
std::map<std::string, ConfigSnapshot> tab_data = config->GetConfigsByGroup("相机设置");
```

---

## 5. 企业级高级特性

### 5.1 现场急救：恢复出厂设置
```cpp
// 恢复单个被改乱的参数
config->ResetToDefault("Algo.Threshold");
// 恢复整个模块的所有参数
config->ResetGroupToDefault("相机设置");
```

### 5.2 配方管理：导入与导出
```cpp
// 导出当前配方
config->ExportToFile("D:/Recipes/A.json");
// 导入新配方（参数 2: true 表示导入后立即生效，触发所有硬件的回调）
config->ImportFromFile("D:/Recipes/B.json", true);
```

### 5.3 审计留痕 (Audit Trail)
系统对参数的所有合法修改，都会生成一条 `ConfigChangedEvent` 并广播。日志插件可进行订阅防篡改留痕：
```cpp
// 监听系统的参数修改事件
auto bus = z3y::GetDefaultService<z3y::IEventBus>();
conn_group_ += bus->SubscribeGlobal<z3y::interfaces::core::ConfigChangedEvent>(
    this, &MyLoggerPlugin::OnConfigChanged, z3y::ConnectionType::kQueued
);

// 回调签名
void OnConfigChanged(const z3y::interfaces::core::ConfigChangedEvent& e) {
    // e.path (路径), e.old_value, e.new_value, e.operator_role (操作人)
}
```

---

## 6. ⚠️ 新手必读：避坑指南与底层黑科技

新人在使用本配置框架时，请牢记以下三条铁律：

### 💣 铁律 1：绝对不能抛弃生命周期句柄 (ScopedConnection)
**【错误示范】**：
```cpp
// 致命错误：没有用 conn_group_ 保存返回值！
config->Builder<int>("A").Default(1).Bind([](int){}); 
```
**【后果】**：返回的 `ScopedConnection` 是个临时变量，离开函数作用域立刻析构。这会导致你的回调**刚绑定上就被光速注销**，你永远收不到后续的数值更新！

### 💣 铁律 2：警惕隐式类型转换截断
**【错误示范】**：
```cpp
// 企图创建一个 double 类型的参数，但 Default 传了整型 10
config->Builder<double>("Speed").Default(10).Bind(...);
```
**【后果】**：C++ 编译器会聪明反被聪明误，把你的 `Builder<double>` 模板推导与 `10` 结合，强制转成了 `int64_t` 存入底层。后续你调用 `SetValueSafe<double>` 时会因为类型不匹配被严格的底层安全系统驳回！
**【正确做法】**：`Builder<double>` 就必须老老实实写 `.Default(10.0)`。

### 💣 铁律 3：禁止在回调中制造死循环重入
虽然框架底层拥有最高级的 **“防死循环递归保护 (Recursion Protection)”**，当发现你来回踢皮球深度超过 3 次时会自动掐断并报错，但这依然是糟糕的设计。
**【不建议的做法】**：在 A 参数的 `Bind` 回调里去 `SetValue(B)`，同时在 B 参数的回调里又去 `SetValue(A)`。尽量用 `BatchUpdater` 解决联动问题。

### 🌟 底层黑科技：不要担心插件的加载顺序 (占位节点)
**新手常问**：*“如果 UI 插件先启动，它想去订阅 `Camera.Exposure`，但这个时候相机插件还没加载，参数还没注册，UI 订阅会崩溃或者失败吗？”*

**解答**：**绝对不会！** z3y 底层拥有极其强大的 **“占位节点 (Phantom Node)”** 机制。当 UI 提前订阅时，底层会自动创建一个隐形的占位坑。等到 5 秒后相机插件真正调用 `.Default(1000).Bind(...)` 注册时，底层会瞬间将这个坑“转正”，并自动把 `1000` 顺着网线发射给苦苦等待的 UI。
**结论**：尽情解耦！你无需在任何地方配置插件的启动先后顺序！