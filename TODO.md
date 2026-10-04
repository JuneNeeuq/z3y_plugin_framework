# 插件框架架构待办（Backlog）

> 本文记录 **z3y 插件框架自身**的已知设计缺陷与改进方向，供后续按优先级优化。
> 每个条目给出：现状（含代码位置）→ 问题 → 目标设计 → 验收标准。
> 文末附"宿主侧临时规避"，框架修复后应移除。

---

## 背景 / 动机

**现场故障（宿主退出崩溃）**：
宿主退出时崩溃在 `QApplication` 析构的 Qt 全局清理里，AV 地址落在某依赖模块（如 `Qt6PrintSupport`）已被解除映射的地址区间。

根因链：
1. 某插件是进程内**唯一**加载该依赖模块的模块；
2. 退出时 `UnloadAllPlugins()`（及 `~PluginManager()`）会 `FreeLibrary` 该插件；
3. 插件卸载后，依赖模块引用计数归零，被 Windows 一并卸载；
4. 但 Qt 的全局/静态清理仍会调用其代码 → `executing location` 访问违例。

**临时规避（见文末）**：宿主插件对该依赖模块 `LoadLibrary` 且永不释放（钉住）。**这不是框架级解法**，仅用于止血。

---

## P0 — 插件卸载：区分"逻辑关闭"与"物理卸载"

### 现状

- `PluginManager::UnloadAllPlugins()` 与 `PluginManager::~PluginManager()` 都汇入 `ClearAllRegistries()`；
- `ClearAllRegistries()`：先逆序 `Shutdown()` 各单例组件，清空注册表/事件总线，再调用 `PlatformSpecificLibraryUnload()` → 逐个 `FreeLibrary` 插件 DLL。

代码位置（函数名稳定，行号会漂移）：
- `src/z3y_plugin_manager/plugin_manager.cpp`
  - `PluginManager::~PluginManager()`（内部调 `ClearAllRegistries()`）
  - `PluginManager::ClearAllRegistries()`（Shutdown 循环 + `PlatformSpecificLibraryUnload()`）
  - `PluginManager::UnloadAllPlugins()`（调 `ClearAllRegistries()`）
  - `PluginManager::RegisterComponent()`（默认注册逻辑，见 P1）
  - `PluginManager::RollbackRegistrations()`
- `src/z3y_plugin_manager/platform_win.cpp`
  - `PluginManager::PlatformSpecificLibraryUnload()`（`FreeLibrary` 循环）
  - `PluginManager::PlatformLoadLibrary()`（`LoadLibraryExW`）
  - `PluginManager::PlatformUnloadLibrary()`

### 问题

1. **职责混淆**：把"逻辑生命周期"（Initialize/Shutdown、注册表、事件总线、单例、订阅）与"物理模块生命周期"（LoadLibrary/FreeLibrary）耦合在同一路径。
2. **危险默认**：退出/析构时卸载。C++ 模块的静态/全局状态、RTTI/vtable、Qt 元对象注册表、跨模块 `shared_ptr/weak_ptr` 都可能仍存活——只要有一个引用，卸载即 UB。Qt 官方亦明确：卸载 Qt 插件不安全。
3. **无"按插件卸载"机制**：当前只有"全局清空"（`ClearAllRegistries`）与"加载失败回滚"（`RollbackRegistrations`），没有"拆除单个已加载插件"的路径。
4. **无静默期（quiescence）**：卸载前未排空事件队列/确保无 in-flight 回调引用该插件对象，存在 UAF。
5. **同路径重载陷阱**：模块仍被映射时，`LoadLibrary` 返回**已映射的旧句柄**（不读新文件）→ 静默运行旧代码。
6. **析构期卸载**：进程/静态析构期卸载模块尤为危险。

### 目标设计

- **显式生命周期状态机**：`Loaded → Shutdown() → Unload()`。
- **默认策略 `KeepLoaded`（安全）**；插件显式声明 `Unloadable` 才允许物理卸载。
- `ShutdownAll()`：只做逻辑关闭（停组件、清注册表/事件总线），**绝不卸载模块**；`~PluginManager()` 同样**不卸载**。
- `Unload(plugin)`（或 `UnloadAllUnloadable()`）：**静默 → 按插件全量拆除 → FreeLibrary**；仅对声明 `Unloadable` 的插件生效；未声明的**拒绝并记日志**（不崩）。
- **真正的热重载优先用进程隔离**（独立进程承载插件）；in-process 卸载定位为"插件背书、尽力而为"的次级能力。
- 文档明确：**in-process 卸载 C++/Qt 模块是不安全的**，责任在插件。

### 验收标准

- [ ] 退出与 `~PluginManager()` 不再卸载任何模块；退出无 AV。
- [ ] `ShutdownAll()` 后，已加载模块仍在（可被再次解析/使用，若宿主需要）。
- [ ] 未声明 `Unloadable` 的插件调用 `Unload*` 被安全拒绝（日志），不崩。
- [ ] 对 `Unloadable` 插件：卸载=彻底拆除（组件/alias/default/interface_index/plugin_path_index/事件订阅/singleton），无悬垂引用。
- [ ] 卸载后**同路径重载**能真正加载新文件，或**明确拒绝并报错**（不得静默跑旧代码）。
- [ ] 卸载与事件循环的静默期正确（卸载期间无 in-flight 回调访问已卸载对象）。

---

## P1 — 默认注册语义：`is_default` 不应"全接口一刀切"

### 现状

`RegisterComponent` 在 `is_default=true` 时遍历**该组件的所有接口**（仅跳过 `IComponent`）逐个设默认：

```cpp
if (is_default) {
  for (iface : implemented_interfaces) {
    if (iface.iid == IComponent::kIid) continue;   // 只跳 IComponent
    if (default_map_.count(iface.iid)) throw "Default conflict: " + iface.name;
    default_map_[iface.iid] = clsid;
  }
}
```

### 问题

- 共享的**钩子/标记接口**（如 `IStartupHook`）会被**第一个**设默认的组件"占死"；
- 第二个"想设默认 + 又实现该钩子"的组件直接抛 `Default conflict`，导致**整个插件注册失败并被回滚卸载**；
- 这是**语义缺陷**：`is_default` 的本意是"某服务接口的默认实现"，不应波及钩子接口。

### 目标设计（择一）

- **方案 A（最小、零 API 变更）**：`is_default` 只作用于"**主接口**"——即 `Interfaces...` 包中**第一个非 `IComponent`** 的接口。文档写明"主接口 = 第一个接口"。
  - 改动：`RegisterComponent` 的默认设置循环 `break` 于第一个非 `IComponent`；`RollbackRegistrations`/`ClearAllRegistries` 相应保持精确。
- **方案 B（显式选择接口）**：注册签名把 `bool is_default` 换成**接口集合** `std::vector<InterfaceId> default_for`；只对列表内、且属于 `implemented_interfaces` 的 IID 设默认；`ComponentInfo` 存储 `default_for` 供回滚/卸载精确撤销。
  - 改动文件：`i_plugin_registry.h`（签名）、`plugin_manager_pimpl.h`（`ComponentInfo` 增 `default_for`）、`plugin_manager.cpp`（`RegisterComponent`/`RollbackRegistrations`）、`plugin_registration.h`（模板透传）、`auto_registration.h`（宏）。
  - 便捷宏：`Z3Y_AUTO_REGISTER_DEFAULT_SERVICE(Class, Alias, InterfaceType)`（用 `InterfaceType::kIid` 构造 `default_for`）。
  - 兼容：保留旧 `Z3Y_AUTO_REGISTER_SERVICE(..., IsDefault)`，其 `true` 语义收敛为"主接口默认"（等价方案 A）。
- **方案 C（更彻底）**：弱化/取消"默认"概念，统一**按别名解析**。默认 map 属"隐式魔法"，正是冲突根源；别名显式、无冲突。

### 验收标准

- [ ] 两个都实现 `IStartupHook` 的服务组件，各自对**自己的主接口**设默认，互不冲突。
- [ ] 回滚/卸载能**精确**撤销该组件的默认（不误伤他人）。
- [ ] `default_for` 必须校验 ⊆ `implemented_interfaces`（否则报错）。

---

## P2 — 其它框架级问题

- **`Destroy()` 后再 `Create()`**：`LoadLibrary` 返回旧句柄 + `z3yPluginInit` 重跑 → 可能重复注册。需定义"Destroy 后不可再 Create"或支持干净重建。
- **卸载/重载的身份冲突**：ClassId/IID 不变，必须彻底清账，否则 `already registered`。
- **文档**：明确 in-process 卸载的契约与插件责任（含"带模块静态状态/用 Qt 的插件通常不可卸载"）。
- **`is_default` 的存储与回滚一致性**：`RollbackRegistrations` 目前从 `implemented_interfaces` 反推默认，若采用方案 B 必须改为使用存储的 `default_for`。

---

## 附：宿主侧临时规避（框架修复后移除）

宿主在某插件内对"进程内唯一加载的依赖模块"执行 `LoadLibrary` 且**永不 `FreeLibrary`**（钉住），使框架的 `FreeLibrary` 成为空操作。

- 现状：`plugin_hw_printer` 钉住 `Qt6PrintSupport[d].dll`。
- 局限：魔法 DLL 名（Debug/Release 后缀）；每新增一个"引入新依赖模块"的插件都要各钉一次；把问题藏在插件里。
- 移除条件：P0 落地（退出/析构不卸载，或提供 `ShutdownAll` + 安全卸载机制）后删除。
