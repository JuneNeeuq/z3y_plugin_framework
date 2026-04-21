/**
 * @file config_provider_service.h
 * @brief 配置管理模块底层实现声明 (ConfigProviderService)。
 * * @details
 * 这是整个配置管理体系的心脏，负责承载基于内存的高并发读写请求以及文件系统的持久化落地。
 * * 【并发模型与锁策略设计】
 * 为了应对工业级高频并发场景，本服务采用**极高精度的双重读写锁 (Double-Level
 * Read-Write Locks)** 结构：
 * 1. 拓扑锁 (dict_mutex_)：全局唯一的 std::shared_mutex。
 * - 仅用于保护 `config_dict_` 这个 std::unordered_map
 * 的增删（也就是节点注册阶段）。
 * - 当业务查询(GetValue) 或 修改(SetValue)
 * 某个特定节点时，只持有此全局锁的**共享读锁(shared_lock)**，
 * 因此不同路径的读写请求可以完全并行，绝不会互相阻塞。
 * 2. 节点锁 (entry_mutex)：每个 ConfigEntry 内部各自拥有一把
 * std::shared_mutex。
 * - 负责保护该节点内部的数据 `current_value` 和回调表 `callbacks`。
 * - GetValue 时使用节点级共享读锁，SetValue 时使用节点级独占写锁。
 * * 【未来计划】
 * - 加入 File Watcher，支持在文件系统被修改时触发基于增量解析的热重载 (Hot
 * Reload) 机制。
 */
#pragma once
#ifndef Z3Y_CONFIG_PROVIDER_SERVICE_H_
#define Z3Y_CONFIG_PROVIDER_SERVICE_H_

#include <atomic>
#include <chrono>
#include <memory>
#include <shared_mutex>
#include <thread>
#include <unordered_map>
#include <nlohmann/json.hpp>
#include "interfaces_core/i_config_service.h"
#include "framework/z3y_define_impl.h"

namespace z3y {
namespace plugins {
namespace config {
using namespace z3y::interfaces::core;

/**
 * @brief 数据节点模型，表示单个配置项在内存中的全部信息。
 */
struct ConfigEntry {
  SchemaMetadata meta; /**< 该节点的 Schema 规则约束 */
  ConfigValue current_value; /**< 当前处于合法生效状态的值 */
  ConfigValue default_value; /**< 当重置或节点转正时参考的默认值 */

  mutable std::shared_mutex entry_mutex; /**< [关键] 保护当前这一条记录的超细粒度读写锁 */

  /** * @brief 存储注册到该节点的所有回调闭包。
   * key 是一次性生成的自增 id，便于后期 O(1) 复杂度注销。
   */
  std::map<uint64_t, std::function<void(const ConfigValue&)>> callbacks;
};

/**
 * @brief 配置核心服务提供者具体实现类。
 */
class ConfigProviderService
    : public z3y::PluginImpl<ConfigProviderService, IConfigService> {
 public:
  //! 组件唯一 ID
  Z3Y_DEFINE_COMPONENT_ID("z3y-core-ConfigProviderService-Impl-UUID-P001");

 public:
  ConfigProviderService();
  ~ConfigProviderService() override;

  /** @brief 框架调用的生命周期初始化钩子。在此处启动独立落盘线程。 */
  void Initialize() override;
  /** @brief
   * 框架调用的生命周期清理钩子。在此处优雅终止落盘线程并执行最后一次刷盘。 */
  void Shutdown() override;

  void SetStoragePath(const std::string& absolute_path) override;

  bool SetValue(const std::string& path, const ConfigValue& value,
                const std::string& operator_role) override;
  std::vector<std::string> ApplyBatch(
      const std::map<std::string, ConfigValue>& changes,
      const std::string& operator_role) override;

  void RegisterSchema(const std::string& path, const SchemaMetadata& meta,
                      const ConfigValue& default_val) override;
  uint64_t InternalSubscribe(
      const std::string& path,
      std::function<void(const ConfigValue&)> cb) override;
  void InternalUnsubscribe(const std::string& path, uint64_t cb_id) override;
  ConfigValue GetValue(const std::string& path) const override;

  std::shared_ptr<void> GetAliveToken() const override { return alive_token_; }

  std::map<std::string, ConfigSnapshot> GetAllConfigs() const;

  bool ReloadFromFile() override;

  bool ResetToDefault(const std::string& path) override;
  void ResetGroupToDefault(const std::string& group_key) override;
  bool ExportToFile(const std::string& target_path) const override;
  bool ImportFromFile(const std::string& source_path,
                      bool apply_immediately = true) override;

  std::vector<std::string> GetAllGroupKeys() const override;
  std::map<std::string, ConfigSnapshot> GetConfigsByGroup(
      const std::string& group_key) const override;

 private:
  /** @brief 内部加载逻辑：将 json 读取到初始缓存池 initial_load_cache_ 中。 */
  void LoadFromFile();

  /** * @brief 数据合法性仲裁中心。
   * @details 校验越界、越权及类型篡改，是抵御外部非法数据的最后防线。
   */
  bool ValidateInternal(const ConfigEntry& entry, const ConfigValue& new_val,
                        const std::string& role, std::string& out_error) const;

  /** @brief 极速返回的异步保存触发器。只修改标记，不阻塞业务线程。 */
  void AsyncSaveSnapshot();

   /** * @brief 专属后台 IO 守护线程的核心执行体。
   * @details 采用带有“睡眠抖动合并”设计的 Wait-Condition
   * 模型，极大地降低了高频写盘造成的 I/O 开销。
   */
  void WorkerRoutine();

 private:
  /** * @brief 未定型数据孤儿院。
   * @details 启动时从文件读取的 JSON
   * 如果尚未被任何业务插件注册（可能插件还没加载，或已被废弃），
   * 它们会存放在这里。当插件注册时，会从中“认领”属于自己的数据；
   * 而废弃的数据在落盘时会被原封不动写回，防止升级丢失。
   */
  std::unordered_map<std::string, nlohmann::json> initial_load_cache_;
  std::string config_file_path_ = "config.json"; /**< 真实配置文件存放路径 */
  std::string config_tmp_path_ = "config.json.tmp"; /**< 用于实现原子覆写的临时文件路径 */

  /** @brief 核心字典拓扑结构：路径字符串映射到共享指针节点。 */
  std::unordered_map<std::string, std::shared_ptr<ConfigEntry>> config_dict_;
  mutable std::shared_mutex dict_mutex_; /**< 保护字典拓扑结构的全局读写锁 */
  std::atomic<uint64_t> next_cb_id_{1}; /**< 全局自增 ID 生成器，用于派发回调句柄 */
  std::shared_ptr<void> alive_token_;  /**< 框架级防坠网生还标志。生命周期与插件相同 */

  // ---------------- 后台工作线程专属成员 (Worker Thread) ----------------
  std::thread worker_thread_; /**< 负责落盘操作的实际线程对象 */
  std::mutex worker_mutex_; /**< 守护下面脏标记和条件变量的专有互斥锁 */
  std::condition_variable worker_cv_; /**< 用于阻塞和唤醒 Worker 线程 */

  bool is_snapshot_dirty_ = false;  /**< 脏数据标志位，表示内存数据发生改变且未落盘 */
  bool stop_worker_ = false; /**< 优雅退出标志，接通 Shutdown() 的终止信号 */
};
}  // namespace config
}  // namespace plugins
}  // namespace z3y

#endif  // Z3Y_CONFIG_PROVIDER_SERVICE_H_