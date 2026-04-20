/**
 * @file config_types.h
 * @brief 配置管理模块的基础类型与数据结构定义（SSOT 模式核心基石）。
 * * @details
 * 本文件定义了跨越插件边界传递配置数据所需的全部基础类型。
 * * 【设计思想】
 * - 模式驱动 (Schema-Driven)：配置不仅仅是键值对，而是携带元数据的“模式”。UI
 * 层通过解析这些 Schema， 可以实现界面控件的动态生成（Data-Driven UI）。
 * - 唯一真相来源 (SSOT)：业务插件通过代码定义
 * Schema，后端负责托管，前端负责渲染，保证了配置定义的唯一性。
 * - 生命周期安全 (RAII)：通过 ScopedConnection 提供基于 RAII
 * 的订阅关系管理，彻底杜绝插件热插拔时的野指针崩溃。
 * * 【未来计划】
 * - 考虑为 ConfigValue 引入对 nlohmann::json
 * 的直接支持，以支持复杂的嵌套结构体一键注册。
 * - 增加自定义校验器（Validator Callback）接口。
 */

#pragma once
#ifndef Z3Y_CONFIG_TYPES_H_
#define Z3Y_CONFIG_TYPES_H_

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <variant>
#include <vector>

#include "framework/z3y_define_impl.h" 

namespace z3y {
namespace interfaces {
namespace core {

/**
 * @brief 核心变体类型：跨越插件 ABI 边界的统一数据载体。
 * @details
 * 为什么使用 std::variant 而不是 void* 或基类多态？
 * 因为 variant 在内存上是连续的（栈分配），且带有严格的类型安全校验。
 * @note
 * - 包含 std::monostate 是为了表达“未初始化/占位节点”的状态。
 * - 统一将整型映射为 int64_t，浮点型映射为 double，避免 C++
 * 繁杂的底层类型转换导致的 ABI 不兼容。
 */
using ConfigValue = std::variant<std::monostate, int64_t, double, bool,
                                 std::string, std::vector<int64_t>,
                                 std::vector<double>, std::vector<std::string>>;

/**
 * @brief UI 控件类型枚举，指导前端渲染引擎如何生成界面。
 */
enum class WidgetType {
  kAuto, /**< 自动推导：整数推导为 SpinBox，布尔推导为 CheckBox 等 */
  kFilePicker, /**< 文件选择器：带“浏览”按钮的输入框 */
  kDirPicker, /**< 文件夹选择器：用于选择目录路径 */
  kComboBox, /**< 下拉框：必须配合 SchemaMetadata 的 enum_values 使用 */
  kSlider, /**< 滑动条：必须配合 Min/Max/Step 使用 */
  kPasswordInput, /**< 密码输入框：显示为星号隐藏明文 */
  kProgressBar /**< 进度条：通常用于只读的运行时状态监控 */
};

/**
 * @brief 配置变更审计事件 (Audit Trail Event)
 * @details
 * 当配置被合法修改并实质生效时，将由 ConfigService 广播此事件。
 * 日志插件或云端同步插件可以订阅 "System.Config.Changed" 主题，
 * 将此事件持久化到防篡改数据库中，实现企业级操作留痕。
 */
struct ConfigChangedEvent : public z3y::Event {
  Z3Y_DEFINE_EVENT(ConfigChangedEvent, "z3y-evt-ConfigChangedEvent-001");
  std::string path;      /**< 被修改的配置路径 (如 "Camera.Exposure") */
  std::string old_value; /**< 修改前的旧值 (已序列化为 JSON 字符串形式) */
  std::string new_value; /**< 修改后的新值 (已序列化为 JSON 字符串形式) */
  std::string
      operator_role;     /**< 发起修改的操作人角色 (如 "Admin", "Operator") */
  uint64_t timestamp_ms; /**< 修改发生时的毫秒级系统时间戳 */
};

/**
 * @brief 模式元数据 (Schema Metadata)，定义了一个配置项的全生命周期属性。
 * @details
 * 这是“前后端分离”架构的灵魂。业务后端填写此结构体，UI
 * 前端拉取此结构体进行界面绘制。
 */
struct SchemaMetadata {
  std::string name_key; /**< @brief 配置项名称 (支持多语言 Key，例如 "Camera.Exposure.Name") */
  std::string group_key; /**< @brief 一级分组名 (通常映射为 UI 的 Tab 页签) */
  std::string subgroup_key; /**< @brief 二级分组名 (通常映射为 UI 的 GroupBox 框) */
  std::string tooltip_key; /**< @brief 鼠标悬浮提示 (Tooltip，解释该参数的具体作用) */
  std::string enable_condition; /**< @brief UI 联动表达式，例如 "ParamA == 1"，满足时此控件才启用 */

  ConfigValue min_val; /**< @brief 允许的最小值，如果类型为 monostate 表示无限制 */
  ConfigValue max_val; /**< @brief 允许的最大值，超出将遭到后端拦截并报错 */
  ConfigValue step_val; /**< @brief 步进值，用于 UI 控件 (如 SpinBox) 的单次点击增量 */

  std::vector<std::string> enum_values; /**< @brief 下拉框后台真实值列表 */
  std::vector<std::string> enum_display_keys; /**< @brief 下拉框前端显示文本列表 (与 values 必须一一对应) */

  std::string file_filter; /**< @brief 文件选择器过滤器，例如 "*.json *.txt" */
  std::string permission_token; /**< @brief 权限令牌，例如 "admin_only"，校验失败拒绝写入 */

  WidgetType widget_type = WidgetType::kAuto; /**< @brief 强制指定的 UI 控件呈现形式 */

  bool is_advanced = false; /**< @brief 是否为高级参数 (UI 默认隐藏，需勾选“高级模式”才显示) */
  bool is_hidden = false; /**< @brief 是否为隐藏参数 (纯后端使用，UI 完全不渲染) */
  bool read_only = false; /**< @brief 是否只读 (UI 显示为禁用状态，通常用于监控状态) */
  bool requires_restart = false; /**< @brief 修改后是否需要重启软件才能生效 (UI 负责弹窗提示) */

  /**
   * @brief 自定义高级校验器 (Custom Validator Callback)
   * @details
   * 突破了 Min/Max 的静态限制，允许业务层注入复杂的验证逻辑
   * （如：“帧率必须是偶数” 或 “起始坐标必须小于结束坐标”）。
   * * @return std::string 如果返回空字符串 ("")，表示校验通过！
   * 如果返回非空字符串，表示校验失败，该字符串将作为错误信息被直接驳回给调用方。
   */
  std::function<std::string(const ConfigValue&)> custom_validator = nullptr;
};

/**
 * @brief 自动管理配置订阅生命周期的 RAII 保护伞。
 * * @details
 * 【存在意义】
 * 当插件 A 订阅了某个配置的回调，如果插件 A 被卸载，而框架后台依然持有 A
 * 的函数指针（std::function），
 * 此时发生配置变更触发回调，将导致严重的“悬空指针访问 (Access Violation)”崩溃。
 * * 【工作机制】
 * 业务通过 Bind()
 * 获取此对象，通常作为类的成员变量。当业务类析构时，此类自动触发析构函数，
 * 通知后台注销对应的回调，实现绝对的内存安全。
 * * @warning 此类不可复制，只能移动。
 */
class ScopedConnection {
 public:
  /** @brief 默认构造，创建一个空连接 */
  ScopedConnection() = default;

  /**
   * @brief 构造函数，注入断开连接的具体逻辑。
   * @param disconnect_func 由 ConfigBuilder 或 IConfigService 生成的注销闭包。
   */
  explicit ScopedConnection(std::function<void()> disconnect_func)
      : disconnect_func_(std::move(disconnect_func)) {}

  /** @brief 析构函数，在此处触发自动注销逻辑 */
  ~ScopedConnection() {
    if (disconnect_func_) {
      disconnect_func_();
    }
  }
  // 禁用拷贝语义，保障资源所有权的唯一性
  ScopedConnection(const ScopedConnection&) = delete;
  ScopedConnection& operator=(const ScopedConnection&) = delete;
  
  // 启用移动语义，允许放入 std::vector 等容器
  ScopedConnection(ScopedConnection&& other) noexcept
      : disconnect_func_(std::move(other.disconnect_func_)) {
    other.disconnect_func_ = nullptr;
  }

  ScopedConnection& operator=(ScopedConnection&& other) noexcept {
    if (this != &other) {
      if (disconnect_func_) disconnect_func_();
      disconnect_func_ = std::move(other.disconnect_func_);
      other.disconnect_func_ = nullptr;
    }
    return *this;
  }

 private:
  std::function<void()> disconnect_func_;
};

/**
 * @brief 批量生命周期管理器，用于收集管理多个 ScopedConnection。
 * @details
 * 业务模块通常有很多配置项。为了避免声明几十个 ScopedConnection 成员变量，
 * 可以统一放进 ConnectionGroup 中，业务类析构时统一释放。
 */
class ConnectionGroup {
 public:
  ConnectionGroup() = default;
  ~ConnectionGroup() = default;

  ConnectionGroup(const ConnectionGroup&) = delete;
  ConnectionGroup& operator=(const ConnectionGroup&) = delete;

  ConnectionGroup(ConnectionGroup&&) = default;
  ConnectionGroup& operator=(ConnectionGroup&&) = default;

  /**
   * @brief 重载 += 运算符，语法糖，用于快速接管 ScopedConnection 的所有权。
   */
  ConnectionGroup& operator+=(ScopedConnection&& conn) {
    connections_.push_back(std::move(conn));
    return *this;
  }

  /** @brief 手动清空并断开所有连接 */
  void Clear() { connections_.clear(); }

 private:
  std::vector<ScopedConnection> connections_; /**< 独占所有权的连接容器 */
};

}  // namespace core
}  // namespace interfaces
}  // namespace z3y

#endif  // Z3Y_CONFIG_TYPES_H_