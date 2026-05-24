# Qt Config UI 模块 (plugin_qt_config_ui) 设计与使用说明书

## 1. 模块简介与设计思想

`plugin_qt_config_ui` 是 `z3y_plugin_framework` 框架中负责**全自动生成与渲染系统配置界面**的重量级插件模块。

### 1.1 核心设计理念：数据驱动 UI (Data-Driven UI)
在传统的软件开发中，每增加一个配置参数，开发者都需要去 UI 设计器（如 Qt Designer）中拖拽一个控件，并手写繁琐的信号槽绑定代码和读写硬盘的逻辑。这不仅极大地拖慢了开发效率，还容易造成界面与底层数据的不一致。

本模块打破了这一传统，采用了**完全数据驱动**的思想：
- **开发者只需在业务代码中声明参数（Schema）**（例如参数的类型、极值、名字、依赖关系）。
- **UI 模块会自动监听并解析这些元数据**，在内存中像搭积木一样，动态“变幻”出完整的配置界面（包括输入框、滑块、下拉菜单、多维表格等）。
- **所有的数据双向绑定均被框架接管**。用户在界面上的操作会自动经过 ACID 事务包发送到底层；底层被其他线程修改的数据，也会无缝在界面上实时反馈。

---

## 2. 软件架构解析

整个模块的架构可以分为以下几大核心组件：

### 2.1 IConfigUIManager (门面接口)
存在于 `interfaces_ui/i_config_ui_manager.h` 中。这是面向业务层开发者的**唯一接触点**。通过面向接口编程（IOP），业务代码完全不需要包含任何 `<QWidget>` 等 Qt 头文件。它实现了业务逻辑与 GUI 库的彻底解耦。

### 2.2 EventBridge (跨线程通信桥)
配置系统的底层服务可能在任何工作线程中高频修改数据，而 Qt 强制要求界面的刷新必须在主线程（UI 线程）中进行。
`EventBridge` 作为一座护城河：
- 订阅底层的 `ConfigChangedEvent` 广播。
- 使用 `QMetaObject::invokeMethod(..., Qt::QueuedConnection)` 将数据安全地抛入主线程的消息队列。
- 内置 **30 FPS 节流定时器 (Throttling)**：在 33ms 内如果数据发生了上百次突发改变，它只会在最后将去重后的数据打包发给 UI 刷新，彻底杜绝了并发风暴导致的界面卡死。

### 2.3 DependencyGraph (依赖关系图谱)
系统支持参数间的联动（如“勾选自动测光后，曝光时间输入框立刻灰显”）。该引擎会在页面加载时解析预编译的正则条件字符串，并在参数变化时沿着依赖树级联刷新下游控件的 `isEnabled` 状态。

### 2.4 WidgetFactory (控件铸造工厂)
它是页面的直接生成者。内部有长达数百行的 `if-else` 分支，利用 C++17 的 `std::visit` 和 `std::holds_alternative` 拆解底层传来的无类型变体，并精准实例化出诸如 `QDoubleSpinBox`、`QSlider`、`QTableWidget` 等各种 Qt 原生控件。

### 2.5 ConfigMainWindow (主窗体与 LRU 管理器)
考虑到工业级软件可能有上千个配置项，如果一次性创建所有控件，会瞬间吃掉几百MB内存并导致打开界面卡顿几秒。
主窗体创新性地引入了 **懒加载与 LRU (Least Recently Used) 页面置换机制**：
- 只有当用户点到某个大类（如“网络设置”）时，才会呼叫工厂去生成该页面的控件。
- 内存中最多保留 `10` 个活跃页面，如果打开第 11 个页面，系统会自动摧毁最久未查看的那个页面的控件以释放内存。

---

## 3. CMake 构建配置指南

为了适应工业与服务端环境，本模块可以被非常方便地**裁剪**。

### 3.1 完整构建（需要 Qt 界面）
当你的机器拥有显示器并且安装了 **Qt6**（本框架全面支持 Qt6 核心特性）时。

**系统环境要求**：
- 安装了 Qt6 (Core, Gui, Widgets 模块)
- C++17 及以上标准

**CMake 核心配置**：
在编译该模块所在的 `CMakeLists.txt` 中，必须开启 Qt 相关的自动编译宏：
```cmake
set(CMAKE_AUTOMOC ON) # 自动处理 Q_OBJECT 宏
set(CMAKE_AUTOUIC ON) # 自动处理 UI 文件（若有）
set(CMAKE_AUTORCC ON) # 自动处理 QRC 资源文件（若有）

# 查找 Qt6 依赖
find_package(Qt6 COMPONENTS Core Gui Widgets REQUIRED)

add_library(plugin_qt_config_ui SHARED ${PLUGIN_SRCS})
target_link_libraries(plugin_qt_config_ui Qt6::Core Qt6::Gui Qt6::Widgets plugin_framework)
```

### 3.2 裁剪构建（Headless 模式，不需要界面 / 服务器无 Qt 环境）
如果这套系统被部署在一台没有图形界面的 Linux 服务器上，或者仅仅是嫌 Qt 太大不想编译它。你可以做到**一行代码都不改，系统依然完美运行**。

因为底层配置模块 (`plugin_config_manager`) 和 UI 模块 (`plugin_qt_config_ui`) 是**物理隔离**的插件！
- 底层并不依赖 UI，底层通过字符串或者 API 调用即可管理配置。
- 你只需要在最顶层的 `CMakeLists.txt` 中不编译 `plugin_qt_config_ui` 目录，且运行时不去加载该插件的 DLL / SO 即可。

如果业务代码中尝试调用 `ShowConfigWindow()`：
```cpp
// 尝试获取 UI 服务。如果没有加载对应的插件，这里会安全返回 nullptr
auto ui_mgr = z3y::GetDefaultService<z3y::interfaces::ui::IConfigUIManager>();
if (ui_mgr) {
    ui_mgr->ShowConfigWindow();
} else {
    std::cout << "当前运行在 Headless 模式，无界面可供显示。" << std::endl;
}
```
**总结：只要隔离得当，不编译此插件，就是完美的无界面模式。**

---

## 4. 详细接口使用说明

在使用之前，请确保你包含了框架接口头文件：
```cpp
#include "interfaces_ui/i_config_ui_manager.h"
#include "framework/z3y_service_locator.h"
```

### 4.1 获取服务实例（关于 z3y 框架的别名与默认服务获取）
在 z3y 插件框架中，一个接口可能存在多种实现。获取服务时需要特别注意**是否带有别名（Alias）**。

在模块初始化注册时，如果已经配置了默认服务提供者（`true` 参数），那么最简单的获取方式是：
```cpp
// 方式一：获取该接口的默认实现（UI 插件通常注册为默认提供者）
auto ui_manager = z3y::GetDefaultService<z3y::interfaces::ui::IConfigUIManager>();
```

如果你在注册时并没有设为默认，或者你需要调用特定的实现版本，则**必须**传入别名：
```cpp
// 方式二：根据组件注册时的别名（Alias）进行精确定位
auto ui_manager = z3y::GetService<z3y::interfaces::ui::IConfigUIManager>("Config.UIManager.Qt");
if (!ui_manager) {
    // 处理插件未加载或别名输入错误的逻辑
}
```

### 4.2 弹出全局配置窗口 `ShowConfigWindow`
**签名**：`virtual void ShowConfigWindow(void* parent_window_handle = nullptr) = 0;`

**用法**：
- 如果传入 `nullptr`，配置窗口会作为一个完全独立的顶层窗口浮现。
- 如果你的主程序本身就是用 Qt 写的，希望配置窗口“依附”于主界面中心显示，你可以将你的主 `QWidget*` 指针作为参数传入（自动转为 `void*`，UI插件内部再转换回去）。
- 多次调用此函数不会创建多个窗口，而是将已经打开的窗口强制置顶闪烁（Raise and Activate）。

### 4.3 角色与权限控制 `SetCurrentUserRole`
**签名**：`virtual void SetCurrentUserRole(const std::string& role) = 0;`

**用法**：
很多软件具有“操作员”和“管理员”两套权限。如果你希望隐藏某些敏感选项：
1. 底层注册参数时，通过 `.Permission("admin_only")` 等方式设定需要管理员权限。
2. 当普通操作员登录时，调用 `ui_manager->SetCurrentUserRole("Operator");`。
3. 当管理员登录时，调用 `ui_manager->SetCurrentUserRole("Admin");`。
此时无论配置窗口是否已经打开，界面都会**瞬间重构**，权限不足的条目会从界面上消失。

### 4.4 挂载高级逃生舱 `RegisterCustomPanel`
**签名**：`virtual void RegisterCustomPanel(const std::string& custom_key, CustomPanelCreator creator) = 0;`

**用法**：
遇到需要画一张实时图表，或者一个复杂的网络拓扑配置页面，普通的通用控件无能为力。此时你需要向框架注册一个自定义的 Qt 控件生成器。

**调用顺序要求**：
- **必须在**调用 `ShowConfigWindow()` 之前完成所有 `RegisterCustomPanel` 的注册。一旦页面被渲染，再注册就无法在这个生命周期内生效了。

---

## 5. 快速上手示例代码

以下提供一段**即插即用**的伪代码示例，展示如何在一个典型的 C++ 程序中结合配置底层模块来弹出这个 UI：

```cpp
#include <iostream>
#include "framework/z3y_service_locator.h"
#include "interfaces_core/i_config_service.h"
#include "interfaces_ui/i_config_ui_manager.h"

// 假设我们已经初始化了插件框架并加载了 ConfigManager 与 QtConfigUI 插件

void SetupSystemSettings() {
    // 1. 获取底层配置服务（获取默认服务实现）
    auto config_srv = z3y::GetDefaultService<z3y::interfaces::core::IConfigService>();
    if (!config_srv) return;

    // 2. 模拟注册几个参数（这通常在各个业务模块自己的初始化阶段完成）
    // 注意 Builder 的链式调用，GroupKey 是左侧一级大树，SubGroupKey 是右侧二级框
    config_srv->Builder<int>("Camera.Exposure")
        .NameKey("曝光时间")        // 在界面上显示的标题
        .Default(100)              // 默认值
        .Min(10).Max(5000)         // 极限约束
        .GroupKey("硬件设置")       // 归属左侧菜单大类
        .SubGroupKey("相机参数")    // 归属右侧面板小框
        .RegisterOnly();
        
    config_srv->Builder<bool>("System.AdvancedMode")
        .NameKey("启用底层维护功能")
        .Default(false)
        .GroupKey("通用")
        .SubGroupKey("系统行为")
        .RegisterOnly();

    // 3. 获取 UI 管理器（获取默认服务实现）
    auto ui_mgr = z3y::GetDefaultService<z3y::interfaces::ui::IConfigUIManager>();
    if (!ui_mgr) {
        std::cerr << "未加载 UI 插件，无法显示界面。" << std::endl;
        return;
    }

    // 4. 设置当前用户的角色为 Admin，使得一切高级选项都可见
    ui_mgr->SetCurrentUserRole("Admin");

    // 5. 进阶玩法：注入一个极其特殊的自绘面版 (注意：这里需要你自己用 Qt 编写面板)
    // auto my_creator = [](void* parent) -> void* {
    //     // 把 void* 强转回 QWidget*
    //     QWidget* qt_parent = static_cast<QWidget*>(parent);
    //     MyComplexChartWidget* chart = new MyComplexChartWidget(qt_parent);
    //     return chart;
    // };
    // ui_mgr->RegisterCustomPanel("MyChart", my_creator);

    // 6. 轰轰烈烈地弹出界面！
    ui_mgr->ShowConfigWindow(nullptr);
}
```

---

## 6. 其他特色机制说明 (排雷指南)

当你初次使用这个模块时，你可能会遇到一些看似“奇怪”的行为，实际上它们都是为了工业级安全性所设计的防护机制：

1. **红框与拦截 (Validation Blocking)**：
   如果在 UI 上通过“漏洞”输入了一个违背 Schema 的非法值并点击了 `Apply`。底层核心（Backend）会坚决予以驳回。此时 UI 会进入拦截模式：
   - 界面上方会弹出阻断警告。
   - 所有非法的控件会被加上刺眼的**红色高亮边框**。
   - 左边导航树对应的项会显示红色的十字报错图标，提示你需要去修补它才能继续使用。

2. **脏数据的橙色高亮**：
   当你在界面上调整了一个滑块，但还没有点击 `Apply`。此时系统称之为“悬而未决的脏数据”。
   为了区分它和底层真实生效的数据，这个控件会变成**加粗的橙色**。只有当你按下 Apply 提交成功后，才会褪去橙色恢复正常。

3. **防冲突覆盖 (Conflict Prevention)**：
   如果你正在某一个文本框里缓慢打字（产生了脏数据），而在同一时刻，底层的某个硬件把这个参数本身的值通过代码给改了！
   系统**绝不会**霸道地把输入框刷新为你硬件上报的值，因为这会直接吞掉你打了一半的字，引起用户抓狂。
   系统会让这个文本框变成橙色，并悬浮提示：“后台数据已变化，请决定是应用你的修改去覆盖它，还是点击 [↺] Reset 放弃打字以刷新底层最新值”。

4. **安全滚轮过滤器 (Scroll Guard)**：
   你在滚动查看几百个参数时，鼠标经常会不小心划过 `QSpinBox` 数字旋转盒。默认的 Qt 行为是滚轮会改变它的数值，导致你无意中破坏了系统参数而不自知。
   模块内的 `ScrollGuardFilter` 强行拦截了这一机制。只有当你明确点击选中（Focus）了某个数字框时，滚轮调节才会生效。其他情况下，所有的滚动都被抛给最外层的大滚动条。
