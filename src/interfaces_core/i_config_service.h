/**
 * @file i_config_service.h
 * @brief 暴露给业务插件的统一配置服务接口。
 * * @details
 * 本文件提供了极其优雅的基于 Fluent API（流式接口）的配置注册与交互手段。
 * * 【设计思想】
 * - 强类型抹平 (Type Erasure)：底层只认识 std::variant，但暴露给业务层的
 * Builder、GetValueSafe 全部是模板函数，业务侧只需关心原始的 int, double,
 * enum，框架会自动进行安全转换。
 * - 事务一致性 (Transaction)：通过 BatchUpdater 提供了对多个配置同时修改的 ACID
 * 特性（全成功或全失败回滚）。
 * * 【使用范例】
 * @code
 * // 注册并订阅一个整数型曝光参数
 * auto conn = config_service->Builder<int>("Camera.Exposure")
 * .NameKey("曝光时间").GroupKey("相机设置")
 * .Default(1000).Min(100).Max(5000)
 * .Bind([this](int val) { this->UpdateExposure(val); });
 * @endcode
 */
#pragma once
#ifndef Z3Y_I_CONFIG_SERVICE_H_
#define Z3Y_I_CONFIG_SERVICE_H_

#include <memory>
#include <stdexcept>
#include <type_traits>
#include <vector>
#include "framework/i_component.h"
#include "config_types.h"

namespace z3y {
namespace interfaces {
namespace core {
class IConfigService;

/**
 * @brief 类型擦除辅助工具：将任意 C++ 业务类型安全转换为底层的 ConfigValue。
 * @tparam T 传入的 C++ 数据类型 (如 int, enum, float)
 * @param val 要转换的实际值
 * @return 转换后的 ConfigValue (std::variant)
 */
template <typename T>
inline ConfigValue ToConfigValue(const T& val) {
  if constexpr (std::is_enum_v<T>) {
    return static_cast<int64_t>(val);
  } else if constexpr (std::is_integral_v<T> && !std::is_same_v<T, bool>) {
    return static_cast<int64_t>(val);
  } else if constexpr (std::is_floating_point_v<T>) {
    return static_cast<double>(val);
  } else {
    return val;
  }
}

/**
 * @brief 类型恢复辅助工具：将底层的 ConfigValue 强转恢复为业务 C++ 类型。
 * @tparam T 期望恢复的目标 C++ 数据类型
 * @param val 底层存储的 ConfigValue
 * @param fallback 如果类型不匹配或未初始化时的后备默认值
 * @return 恢复后的强类型数据
 */
template <typename T>
inline T FromConfigValue(const ConfigValue& val, const T& fallback) {
  if (val.valueless_by_exception() ||
      std::holds_alternative<std::monostate>(val)) {
    return fallback;
  }
  try {
    if constexpr (std::is_enum_v<T>) {
      return static_cast<T>(std::get<int64_t>(val));
    } else if constexpr (std::is_integral_v<T> && !std::is_same_v<T, bool>) {
      return static_cast<T>(std::get<int64_t>(val));
    } else if constexpr (std::is_floating_point_v<T>) {
      return static_cast<T>(std::get<double>(val));
    } else {
      return std::get<T>(val);
    }
  } catch (const std::bad_variant_access&) {
    return fallback;
  }
}

/**
 * @brief 核心语法糖：提供 Fluent API 链式调用的配置构建器。
 * @tparam T 此节点存储的数据类型，例如 int, double, std::string
 * @details
 * 将原本繁杂的 SchemaMetadata 结构体赋值过程，转换为可读性极强的链式调用。
 * 最终通过调用 Bind() 或 RegisterOnly() 提交给后端服务。
 */
template <typename T>
class ConfigBuilder {
 public:
  /**
   * @brief 构建器构造函数。业务侧无需手动实例化，请通过
   * IConfigService::Builder<T> 获取。
   * @param service 注入的配置服务后台接口指针
   * @param path 配置项的唯一全局路径，建议采用 "模块名.子模块名.参数名" 格式
   */
  ConfigBuilder(IConfigService* service, std::string path)
      : service_(service), path_(std::move(path)) {}

  ConfigBuilder& NameKey(const std::string& key) {
    meta_.name_key = key;
    return *this;
  }
  ConfigBuilder& GroupKey(const std::string& key) {
    meta_.group_key = key;
    return *this;
  }

  ConfigBuilder& SubGroupKey(const std::string& key) {
    meta_.subgroup_key = key;
    return *this;
  }

  template <typename U>
  ConfigBuilder& Min(U val) {
    if constexpr (std::is_floating_point_v<T> ||
                  std::is_same_v<T, std::vector<double>>) {
      meta_.min_val = static_cast<double>(val);
    } else if constexpr (std::is_integral_v<T> || std::is_enum_v<T> ||
                         std::is_same_v<T, std::vector<int64_t>>) {
      meta_.min_val = static_cast<int64_t>(val);
    }
    return *this;
  }

  template <typename U>
  ConfigBuilder& Max(U val) {
    if constexpr (std::is_floating_point_v<T> ||
                  std::is_same_v<T, std::vector<double>>) {
      meta_.max_val = static_cast<double>(val);
    } else if constexpr (std::is_integral_v<T> || std::is_enum_v<T> ||
                         std::is_same_v<T, std::vector<int64_t>>) {
      meta_.max_val = static_cast<int64_t>(val);
    }
    return *this;
  }

  template <typename U>
  ConfigBuilder& Step(U val) {
    if constexpr (std::is_floating_point_v<T> ||
                  std::is_same_v<T, std::vector<double>>) {
      meta_.step_val = static_cast<double>(val);
    } else if constexpr (std::is_integral_v<T> || std::is_enum_v<T> ||
                         std::is_same_v<T, std::vector<int64_t>>) {
      meta_.step_val = static_cast<int64_t>(val);
    }
    return *this;
  }

  ConfigBuilder& Widget(WidgetType type) {
    meta_.widget_type = type;
    return *this;
  }
  ConfigBuilder& ReadOnly(bool is_read_only) {
    meta_.read_only = is_read_only;
    return *this;
  }
  ConfigBuilder& Permission(const std::string& token) {
    meta_.permission_token = token;
    return *this;
  }
  ConfigBuilder& Advanced(bool is_advanced) {
    meta_.is_advanced = is_advanced;
    return *this;
  }

  /**
   * @brief 配置下拉框特有属性
   * @param values 后台真实值列表 (存储用)
   * @param display_keys 前台显示列表 (展示用)
   */
  ConfigBuilder& Enum(const std::vector<std::string>& values,
                      const std::vector<std::string>& display_keys) {
    meta_.enum_values = values;
    meta_.enum_display_keys = display_keys;
    meta_.widget_type = WidgetType::kComboBox;
    return *this;
  }

  // 修复：彻底解决 variant 的歧义隐式转换报错
  ConfigBuilder& Default(const T& val) {
    default_val_ = ToConfigValue(val);
    return *this;
  }

  // 【新增】快速设置是否为隐藏配置 (便于结构体注册)
  ConfigBuilder& Hidden(bool is_hidden) {
    meta_.is_hidden = is_hidden;
    return *this;
  }

  // 【新增】快速设置是否需要重启生效
  ConfigBuilder& RequiresRestart(bool req) {
    meta_.requires_restart = req;
    return *this;
  }

  /**
   * @brief 注入自定义的复杂业务校验逻辑 (图灵完备防线)
   * @param val_fn 业务端编写的 Lambda 表达式，接收当前强类型 T，返回
   * std::string。
   * * 【使用范例】：
   * .Validator([](int val) -> std::string {
   * if (val % 2 != 0) return "相机帧率必须是偶数！";
   * return ""; // 返回空字符串代表校验完美通过
   * })
   */
  ConfigBuilder& Validator(std::function<std::string(const T&)> val_fn) {
    // 利用 C++ Lambda 的捕获机制，进行“类型擦除式”的安全封装
    meta_.custom_validator =
        [val_fn = std::move(val_fn)](const ConfigValue& val) -> std::string {
      try {
        // 利用 constexpr 抹平枚举和基础整型到底层 int64_t / double 的差异
        if constexpr (std::is_enum_v<T>) {
          return val_fn(static_cast<T>(std::get<int64_t>(val)));
        } else if constexpr (std::is_integral_v<T> &&
                             !std::is_same_v<T, bool>) {
          return val_fn(static_cast<T>(std::get<int64_t>(val)));
        } else if constexpr (std::is_floating_point_v<T>) {
          return val_fn(static_cast<T>(std::get<double>(val)));
        } else {
          return val_fn(std::get<T>(val));
        }
      } catch (...) {
        // 极小概率防御：如果在 ValidateInternal 之前发生了内存错乱，安全驳回
        return "Validator execution error: Underlying data type mismatch.";
      }
    };
    return *this;
  }

  /**
   * @brief 终结操作：仅将配置注册到后台，不监听其数值变化。
   * @details 适用于“被动轮询”式的参数，业务在需要时手动调取 GetValueSafe。
   */
  void RegisterOnly();

  /**
   * @brief 终结操作：注册到后台并绑定响应式回调。
   * @param callback 一个 Lambda 表达式，当配置被外部（如 UI
   * 修改）合法改变时触发。
   * @return ScopedConnection
   * 返回连接句柄，**调用方必须保存该句柄，否则回调将立刻失效！**
   * @note
   * 调用此函数后，回调会被**立即执行一次**，以传入当前系统中的初始值回填业务。
   */
  ScopedConnection Bind(std::function<void(const T&)> callback);

 private:
  IConfigService* service_; /**< 后台服务指针 */
  std::string path_; /**< 配置路径 */
  SchemaMetadata meta_; /**< 正在构建的模式数据 */
  ConfigValue default_val_; /**< 暂存的默认值 */
};

/**
 * @brief 快照结构体：提供给 UI 层渲染界面的数据包裹。
 */
struct ConfigSnapshot {
  SchemaMetadata meta; /**< 模式元数据 (指引如何画界面) */
  ConfigValue current_value; /**< 当前系统生效值 (回填界面的状态) */
};

/**
 * @brief 事务性批量更新器 (Transaction Updater)。
 * @details
 * 解决多项参数联动时的并发与一致性问题。
 * 例如：同时修改 X 和 Y 坐标，如果分两次调用 SetValue，可能会在修改 X 成功但 Y
 * 未修改时，
 * 触发业务逻辑导致行为怪异。批量更新保证全量锁定，成功则全部生效并触发回调，任一失败则全量回滚。
 */
class BatchUpdater {
 public:
  BatchUpdater(IConfigService* service) : service_(service) {}

  /**
   * @brief 将目标路径和新值加入暂存区（此时并未真正生效修改）。
   */
  template <typename T>
  BatchUpdater& Set(const std::string& path, T value) {
    changes_[path] = ToConfigValue(value);
    return *this;
  }

  /**
   * @brief 提交事务到后台。
   * @param role 操作者角色权限标识符，用于拦截越权操作。
   * @return std::vector<std::string>
   * 返回错误信息列表。如果为空表示全量提交成功。
   */
  std::vector<std::string> Commit(const std::string& role = "");

 private:
  IConfigService* service_;
  std::map<std::string, ConfigValue> changes_; /**< 修改暂存表 */
};

/**
 * @brief 纯虚基类：业务层与配置后台交互的唯一桥梁。
 */
class IConfigService : public virtual z3y::IComponent {
 public:
  Z3Y_DEFINE_INTERFACE(IConfigService, "z3y-core-IConfigService-v1", 1, 0);

 public:
  virtual ~IConfigService() = default;

   /**
   * @brief 重定向配置文件持久化落地路径。
   * @param absolute_path 物理磁盘的绝对路径 (如 "C:/my_app/config.json")
   * @note 建议在主程序引导阶段尽早调用，修改路径后会触发一次重新加载覆盖内存。
   */
  virtual void SetStoragePath(const std::string& absolute_path) = 0;

  /**
   * @brief 创建流式构建器以注册新的配置项。
   * @tparam T 配置项的强类型。
   * @param path 唯一路径 (Key)。
   */
  template <typename T>
  ConfigBuilder<T> Builder(const std::string& path) {
    return ConfigBuilder<T>(this, path);
  }

  /**
   * @brief 动态订阅已存在的配置项。
   * @details 通常用于插件之间监听对方的数据变化，而不具备注册权。
   */
  template <typename T>
  ScopedConnection Subscribe(const std::string& path,
                             std::function<void(const T&)> callback);

  /**
   * @brief 裸写接口：向某个路径下发一个新值。
   * @param path 目标路径。
   * @param value 底层类型的变体。
   * @param operator_role 操作者权限令牌，校验不通过将拒绝写入。
   * @return bool 是否成功生效（如果路径不存在或校验失败返回 false）。
   */
  virtual bool SetValue(const std::string& path, const ConfigValue& value,
                        const std::string& operator_role = "") = 0;

  /**
   * @brief 启动一个修改事务。
   */
  virtual BatchUpdater CreateBatch() { return BatchUpdater(this); }

  /**
   * @brief 底层批量修改接口，建议通过 BatchUpdater 进行调用。
   */
  virtual std::vector<std::string> ApplyBatch(
      const std::map<std::string, ConfigValue>& changes,
      const std::string& operator_role = "") = 0;

    /**
   * @brief 高频读接口：主动获取指定路径当前生效的强类型值。
   * @tparam T 期待返回的强类型。
   * @param path 唯一路径。
   * @param fallback_value 如果不存在或解析失败的安全后备值。
   * @return T 获取到的结果。
   */
  template <typename T>
  T GetValueSafe(const std::string& path, T fallback_value = T{}) const {
    return FromConfigValue<T>(GetValue(path), fallback_value);
  }

  /**
   * @brief 强类型安全赋值快捷接口。
   */
  template <typename T>
  bool SetValueSafe(const std::string& path, T value,
                    const std::string& operator_role = "") {
    return SetValue(path, ToConfigValue(value), operator_role);
  }

  /**
   * @brief 框架底层防坠网机制：获取服务的存活令牌。
   * @details 通过 weak_ptr，ScopedConnection
   * 可以在析构时判断后端服务是否已经先一步死亡，从而避免野指针调用。
   */
  virtual std::shared_ptr<void> GetAliveToken() const = 0;

  /**
   * @brief 拉取全局数据快照 (UI 引擎专用)。
   * @return std::map 完整的节点拓扑与配置状态。过滤了 meta.is_hidden == true
   * 的隐藏配置。
   */
  virtual std::map<std::string, ConfigSnapshot> GetAllConfigs() const = 0;

  /**
   * @brief 从持久化配置文件中强制重新加载配置。
   * * @details
   * 这是一个重量级的状态对齐接口，通常用于：
   * 1. 运维人员在后台手动修改了 config.json 后，通过指令通知系统重新加载。
   * 2. 多进程架构中，收到外部配置变更信号时同步内存状态。
   * * 【执行逻辑】：
   * - 解析磁盘上的 JSON 文件。
   * - 遍历所有已注册的节点，若磁盘值合法且发生变化，则更新内存。
   * - 触发所有值发生变化的节点的回调函数（无锁派发）。
   * - 将未被认领的新 JSON 节点存入初始缓存池。
   * * @return bool 如果文件不存在或格式严重损坏返回 false，成功重载返回 true。
   */
  virtual bool ReloadFromFile() = 0;

  // ============================================================================
  // 高级外围业务接口 (容灾、配方、过滤)
  // ============================================================================

  /**
   * @brief 【原子级】将指定路径的参数强制恢复为出厂默认值。
   * @param path 配置项的唯一路径。
   * @return bool 如果路径不存在、或校验失败，返回 false；成功重置返回 true。
   * @details 会自动触发该节点绑定的 Lambda 回调，并进行异步落盘。
   */
  virtual bool ResetToDefault(const std::string& path) = 0;

  /**
   * @brief 【模块级】将指定页签 (Group) 下的所有参数批量恢复为出厂默认值。
   * @param group_key 目标 Group 的名称 (需与 Builder 注册时的 GroupKey 一致)。
   * @details
   * 采用 ACID
   * 事务机制批量覆盖，全部成功或全部回滚。自动触发所有受影响节点的回调。 专用于
   * UI 端“一键重置当前模块/相机/机械臂参数”的防爆破功能。
   */
  virtual void ResetGroupToDefault(const std::string& group_key) = 0;

  /**
   * @brief 导出当前系统的全量配置快照到指定文件 (配方导出)。
   * @param target_path 目标文件的绝对路径 (如 "D:/recipes/product_A.json")。
   * @return bool 是否导出成功。
   * @details 该操作不会改变当前系统的默认读取路径，仅做离线备份/配方保存。
   */
  virtual bool ExportToFile(const std::string& target_path) const = 0;

  /**
   * @brief 从指定文件导入配置并覆盖当前系统 (配方导入)。
   * @param source_path 来源文件的绝对路径 (如 "D:/recipes/product_A.json")。
   * @param apply_immediately 是否立即让导入的配置在内存中生效 (触发回调)。
   * @return bool 是否导入成功。
   * @details
   * 会将源文件物理拷贝并覆盖当前的 config.json。
   * 若 apply_immediately 为 true，则会连带调用
   * ReloadFromFile()，强制系统状态瞬间切换至新配方。
   */
  virtual bool ImportFromFile(const std::string& source_path,
                              bool apply_immediately = true) = 0;

  /**
   * @brief 获取系统中当前所有已注册的非隐藏分组名称。
   * @return 所有唯一的 GroupKey 列表。用于 UI 自动创建顶层 Tab 页签。
   */
  virtual std::vector<std::string> GetAllGroupKeys() const = 0;

  /**
   * @brief 【动态加载优化】仅拉取指定 Group 下的配置快照。
   * @param group_key 目标 Group 的名称。
   * @return std::map<std::string, ConfigSnapshot> 过滤后的快照字典。
   * @details
   * 专为包含成千上万参数的庞大 UI 界面设计。
   * UI 在启动时按需加载 (Lazy Load) 当前选中的 Tab 页签数据，彻底杜绝 UI
   * 白屏卡顿。
   */
  virtual std::map<std::string, ConfigSnapshot> GetConfigsByGroup(
      const std::string& group_key) const = 0;

  // ---------------- 以下为底层设施接口（业务层一般不需要直接调用）----------------

  /** @brief 注册配置节点 (Builder 底层调用的方法) */
  virtual void RegisterSchema(const std::string& path,
                              const SchemaMetadata& meta,
                              const ConfigValue& default_val) = 0;

  /** @brief 内部绑定订阅回调机制 */
  virtual uint64_t InternalSubscribe(
      const std::string& path, std::function<void(const ConfigValue&)> cb) = 0;

  /** @brief 内部解绑订阅回调机制 */
  virtual void InternalUnsubscribe(const std::string& path, uint64_t cb_id) = 0;

  /** @brief 原生获取底层值接口 */
  virtual ConfigValue GetValue(const std::string& path) const = 0;
};

// ============================================================================
// 模板具体实现区 (由于 C++ 模板特性，必须放在头文件)
// ============================================================================

/**
 * @brief 工厂函数：为一个强类型 Lambda 生成负责类型反向转换的底层变体包装器。
 * @details 拦截并处理恶意的类型不匹配，防止变体强转导致的 bad_variant_access
 * 抛出。
 */
template <typename T>
auto CreateTypeSafeWrapper(std::string path, std::function<void(const T&)> cb) {
  return [cb = std::move(cb), p = std::move(path)](const ConfigValue& val) {
    if constexpr (std::is_enum_v<T>) {
      if (std::holds_alternative<int64_t>(val)) {
        cb(static_cast<T>(std::get<int64_t>(val)));
      } else {
        throw std::invalid_argument("ConfigType Mismatch [Enum] for path: " +
                                    p);
      }
    } else if constexpr (std::is_integral_v<T> && !std::is_same_v<T, bool>) {
      if (std::holds_alternative<int64_t>(val)) {
        cb(static_cast<T>(std::get<int64_t>(val)));
      } else {
        throw std::invalid_argument("ConfigType Mismatch [Integer] for path: " +
                                    p);
      }
    } else if constexpr (std::is_floating_point_v<T>) {
      if (std::holds_alternative<double>(val)) {
        cb(static_cast<T>(std::get<double>(val)));
      } else {
        throw std::invalid_argument("ConfigType Mismatch [Double] for path: " +
                                    p);
      }
    } else {
      if (std::holds_alternative<T>(val)) {
        cb(std::get<T>(val));
      } else {
        throw std::invalid_argument("ConfigType Mismatch [Variant] for path: " +
                                    p);
      }
    }
  };
}

template <typename T>
ScopedConnection ConfigBuilder<T>::Bind(
    std::function<void(const T&)> callback) {
  service_->RegisterSchema(path_, meta_, default_val_);

  auto wrapper_cb = CreateTypeSafeWrapper<T>(path_, std::move(callback));
  // 重要逻辑：绑定成功后，立刻手动调用一次包装器，将系统的初始值“注入”给业务层
  wrapper_cb(service_->GetValue(path_));

  uint64_t id = service_->InternalSubscribe(path_, wrapper_cb);
  std::weak_ptr<void> alive = service_->GetAliveToken();

  // RAII 控制：打包断开逻辑。并利用 alive.expired() 拦截框架析构时的乱序风险
  return ScopedConnection([service = service_, p = path_, id, alive]() {
    if (auto token = alive.lock()) {
      service->InternalUnsubscribe(p, id);
    }
  });
}

template <typename T>
ScopedConnection IConfigService::Subscribe(
    const std::string& path, std::function<void(const T&)> callback) {
  auto wrapper_cb = CreateTypeSafeWrapper<T>(path, std::move(callback));
  uint64_t id = InternalSubscribe(path, wrapper_cb);
  std::weak_ptr<void> alive = GetAliveToken();
  return ScopedConnection([this, p = path, id, alive]() {
    if (auto token = alive.lock()) {
      InternalUnsubscribe(p, id);
    }
  });
}

template <typename T>
void ConfigBuilder<T>::RegisterOnly() {
  service_->RegisterSchema(path_, meta_, default_val_);
}

inline std::vector<std::string> BatchUpdater::Commit(const std::string& role) {
  return service_->ApplyBatch(changes_, role);
}
}  // namespace core
}  // namespace interfaces
}  // namespace z3y

#endif  // Z3Y_I_CONFIG_SERVICE_H_