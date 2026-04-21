/**
 * @file config_provider_service.cpp
 * @brief ConfigProviderService 后端实现全逻辑。
 * * @details
 * 【核心技术攻坚区】
 * 1. 占位节点转正 (Phantom Node
 * Upgrade)：解决“消费者先来，生产者后到”的时序耦合难题。
 * 2. 批量事务防死锁 (Mutex Address
 * Sorting)：解决多节点跨线程锁获取导致交叉死锁的问题。
 * 3. 无锁化派发 (Lock-Free
 * Dispatch)：绝不在持有锁的作用域内调用业务的回调，防止死锁重入。
 * 4. 循环递归保护 (Cyclic Update
 * Protection)：阻止互相更新的逻辑酿成栈溢出崩溃。
 */

#include "config_provider_service.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <set>

#include "framework/z3y_framework.h"

Z3Y_AUTO_REGISTER_SERVICE(z3y::plugins::config::ConfigProviderService,
                          "Config.Manager", true);

namespace z3y {
namespace plugins {
namespace config {

namespace {
/**
 * @brief 将严格变体类型降维序列化为无类型的 JSON 结构。
 */
nlohmann::json ConfigValueToJson(const ConfigValue& val) {
  // std::visit 会自动匹配底层类型并调用 nlohmann::json 的对应构造函数
  return std::visit(
      [](auto&& arg) -> nlohmann::json {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, std::monostate>) {
          return nullptr;  // 空状态转为 JSON 的 null
        } else {
          return arg;
        }
      },
      val);
}

/**
 * @brief 基于默认值(Reference)提供的类型指引，从 JSON
 * 泥潭中安全提取强类型并存入 ConfigValue。
 */
bool JsonToConfigValue(const nlohmann::json& j, const ConfigValue& reference,
                       ConfigValue& out_val) {
  try {
    // 强制检查 reference 里的变体存的是什么类型，必须用同样的方法读取 JSON
    if (std::holds_alternative<int64_t>(reference)) {
      // 风险防范：若 JSON 里是 1.5 却强求整型，这里在 get 时可能抛异常，外部会
      // catch
      out_val = j.get<int64_t>();
      return true;
    }
    if (std::holds_alternative<double>(reference)) {
      out_val = j.get<double>();
      return true;
    }
    if (std::holds_alternative<bool>(reference)) {
      out_val = j.get<bool>();
      return true;
    }
    if (std::holds_alternative<std::string>(reference)) {
      out_val = j.get<std::string>();
      return true;
    }
    if (std::holds_alternative<std::vector<int64_t>>(reference)) {
      out_val = j.get<std::vector<int64_t>>();
      return true;
    }
    if (std::holds_alternative<std::vector<double>>(reference)) {
      out_val = j.get<std::vector<double>>();
      return true;
    }
    if (std::holds_alternative<std::vector<std::string>>(reference)) {
      out_val = j.get<std::vector<std::string>>();
      return true;
    }
  } catch (const nlohmann::json::exception& e) {
    std::cerr << "[Config Parse Error] Type mismatch: " << e.what()
              << std::endl;
  }
  return false;
}

/** @brief 辅助：获取当前系统毫秒级时间戳 */
uint64_t GetCurrentTimestampMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

/** @brief 辅助：将 Variant 转化为清晰的字符串，用于写入审计日志 */
std::string ConfigValueToString(const ConfigValue& val) {
  return ConfigValueToJson(val).dump();
}

}  // namespace

ConfigProviderService::ConfigProviderService() {}

ConfigProviderService::~ConfigProviderService() {}

// 框架在安全时刻调用的初始化
void ConfigProviderService::Initialize() {
  // 生成存活防线：在 Shutdown 之前它都不会过期
  alive_token_ = std::make_shared<int>(0);
  LoadFromFile();  // 读取落盘文件到初始内存池

  // 必须在准备工作全部就绪后，再拉起 IO 守护线程
  worker_thread_ = std::thread(&ConfigProviderService::WorkerRoutine, this);
}

// 框架在卸载插件前调用的清理
void ConfigProviderService::Shutdown() {
  // 优雅停止线程的经典写法
  {
    std::unique_lock<std::mutex> lock(worker_mutex_);
    stop_worker_ = true;
  }
  worker_cv_.notify_one();

  if (worker_thread_.joinable()) {
    worker_thread_.join();
  }
}

void ConfigProviderService::RegisterSchema(const std::string& path,
                                           const SchemaMetadata& meta,
                                           const ConfigValue& default_val) {
  std::shared_ptr<ConfigEntry> target_entry;
  bool is_phantom_upgrade = false;  // 关键标记：是否正在把一个占位节点“转正”

  // 用于在全局字典锁内安全地提取和暂存缓存数据
  nlohmann::json cached_json;
  bool has_cache = false;

  {
    // 独占全局字典锁：严格保护 config_dict_ 和 initial_load_cache_ 的并发操作
    std::unique_lock<std::shared_mutex> dict_lock(dict_mutex_);
    auto it = config_dict_.find(path);

    if (it != config_dict_.end()) {
      // 检查当前是不是空节点。
      // 【背景】如果有插件在底层注册前，就已经通过 Subscribe 想听这个数据，
      // 系统为了保存它的回调句柄，会强行插入一个 default_value 为 monostate
      // 的假节点（占位）。
      if (std::holds_alternative<std::monostate>(it->second->default_value)) {
        target_entry = it->second;
        is_phantom_upgrade = true;  // 确认正在给假节点转正
      } else {
        return;  // 真的是被别的业务注册过了，直接忽略退出
      }
    } else {
      // 全新节点，直接插入拓扑树
      target_entry = std::make_shared<ConfigEntry>();
      config_dict_[path] = target_entry;
    }

    // 在安全区内从“未定型数据池”认领数据
    auto cache_it = initial_load_cache_.find(path);
    if (cache_it != initial_load_cache_.end()) {
      cached_json = cache_it->second;  // 将 JSON 数据深拷贝出来
      has_cache = true;
      initial_load_cache_.erase(cache_it);  // 清理内存
    }
  }  // <--- 全局字典锁 dict_lock 在此释放，后续操作不再阻塞其他插件的注册或查询

  // [第二阶段] 节点装配与回填区。
  std::vector<std::function<void(const ConfigValue&)>> callbacks_to_notify;
  ConfigValue initial_actual_val = default_val;

  {
    // 节点级写锁：只保护当前这一个节点的读写，极大提升了并发性能
    std::unique_lock<std::shared_mutex> entry_lock(target_entry->entry_mutex);

    target_entry->meta = meta;
    target_entry->default_value = default_val;

    if (has_cache) {
      ConfigValue parsed_val;
      // 利用刚确立的 default_val 做基准进行类型反演
      if (JsonToConfigValue(cached_json, default_val, parsed_val)) {
        std::string err;
        // 关键安全门：防止外来不合法旧数据污染刚创立的完美节点
        if (ValidateInternal(*target_entry, parsed_val, "System_Init", err)) {
          initial_actual_val = parsed_val;
        } else {
          std::cerr << "[Config Warn] Path [" << path
                    << "] invalid value in file(" << err
                    << "), falling back to default." << std::endl;
        }
      }
    }

    target_entry->current_value = initial_actual_val;

    // 【极其重要】：如果在你注册之前，已经有苦等数据的订阅者了。
    // 转正后必须立刻把他们的回调装进火箭准备发射，告诉他们初始值已到账！
    if (is_phantom_upgrade) {
      for (const auto& pair : target_entry->callbacks) {
        callbacks_to_notify.push_back(pair.second);
      }
    }
  }  // <--- 节点级写锁 entry_lock 在此释放

  // [第三阶段] 无锁化回调派发区。
  // 不要在锁里调用未知业务 Lambda！它可能在里面又调用了一次 GetValue
  // 导致同一把锁死锁重入。
  for (const auto& cb : callbacks_to_notify) {
    try {
      cb(initial_actual_val);
    } catch (...) {
      // 隔离业务端故障，保证核心模块不随之崩溃
    }
  }
}

ConfigValue ConfigProviderService::GetValue(const std::string& path) const {
  std::shared_ptr<ConfigEntry> entry;
  {
    // 对字典只采用共享读锁，百万次读取也不会降低系统性能。
    std::shared_lock<std::shared_mutex> dict_lock(dict_mutex_);
    auto it = config_dict_.find(path);
    if (it == config_dict_.end()) return ConfigValue{};
    entry = it->second;
  }

  // 对节点同样采用共享读锁
  std::shared_lock<std::shared_mutex> entry_lock(entry->entry_mutex);
  return entry->current_value;
}

uint64_t ConfigProviderService::InternalSubscribe(
    const std::string& path, std::function<void(const ConfigValue&)> cb) {
  std::shared_ptr<ConfigEntry> entry;
  {
    // 这里必须是独占锁，因为可能面临目标未注册需要插入“占位节点”的情况
    std::unique_lock<std::shared_mutex> dict_lock(dict_mutex_);
    auto it = config_dict_.find(path);
    if (it == config_dict_.end()) {
      // 【核心方案】：如果是订阅者先到，创建一个“占位节点”
      entry = std::make_shared<ConfigEntry>();
      // 默认的 ConfigEntry.default_value 就是 monostate 空白。
      config_dict_[path] = entry;
    } else {
      entry = it->second;
    }
  }

  uint64_t id = next_cb_id_.fetch_add(1);
  std::unique_lock<std::shared_mutex> entry_lock(entry->entry_mutex);
  entry->callbacks[id] = std::move(cb);
  return id;
}

void ConfigProviderService::InternalUnsubscribe(const std::string& path,
                                                uint64_t cb_id) {
  std::shared_ptr<ConfigEntry> entry;
  {
    std::shared_lock<std::shared_mutex> dict_lock(dict_mutex_);
    auto it = config_dict_.find(path);
    if (it == config_dict_.end()) return;
    entry = it->second;
  }

  std::unique_lock<std::shared_mutex> entry_lock(entry->entry_mutex);
  entry->callbacks.erase(cb_id);
}

bool ConfigProviderService::SetValue(const std::string& path,
                                     const ConfigValue& new_val,
                                     const std::string& operator_role) {
  std::shared_ptr<ConfigEntry> entry;
  {
    std::shared_lock<std::shared_mutex> dict_lock(dict_mutex_);
    auto it = config_dict_.find(path);
    if (it == config_dict_.end()) return false;
    entry = it->second;
  }

  std::vector<std::function<void(const ConfigValue&)>> callbacks_to_run;
  ConfigValue validated_val;

  bool value_changed = false;
  ConfigChangedEvent audit_evt;

  {
    // 在节点修改层面采用独占写锁
    std::unique_lock<std::shared_mutex> entry_lock(entry->entry_mutex);

    std::string err;
    if (!ValidateInternal(*entry, new_val, operator_role, err)) {
      return false;  // 校验失败立即驳回
    }

    // 数值发生实质性改变时，才产生更新动作，屏蔽无意义的刷新
    if (entry->current_value.index() == new_val.index() &&
        entry->current_value == new_val) {
      return true;
    }

    // 【新增】：在更新前，收集安全锁内的审计数据
    audit_evt.path = path;
    audit_evt.old_value = ConfigValueToString(entry->current_value);
    audit_evt.new_value = ConfigValueToString(new_val);
    audit_evt.operator_role =
        operator_role.empty() ? "System_API" : operator_role;
    audit_evt.timestamp_ms = GetCurrentTimestampMs();
    value_changed = true;

    entry->current_value = new_val;
    validated_val = new_val;

    callbacks_to_run.reserve(entry->callbacks.size());
    for (const auto& pair : entry->callbacks) {
      callbacks_to_run.push_back(pair.second);
    }
  }

  // 【安全墙：循环递归死锁保护】
  // 假想极端场景：插件 A 在回调中设置 B 的值，插件 B 在回调里又设置 A 的值。
  // 通过 thread_local 计数可将堆栈溢出扼杀在摇篮里。
  thread_local int recursion_depth = 0;
  thread_local bool has_cyclic_error = false;  // 新增：全局死循环污染标记

  // 1. 如果是最外层调用，初始化/重置污染标记
  if (recursion_depth == 0) {
    has_cyclic_error = false;
  }

  if (recursion_depth > 3) {
    std::cerr << "[Config Error] Maximum recursion depth exceeded, potential "
                 "cyclic update detected at path:"
              << path << std::endl;
    has_cyclic_error = true;  // 标记当前调用链已被死循环污染
    return false;
  }

  // 在锁范围之外执行真正的回调，极度安全
  recursion_depth++;
  for (const auto& cb : callbacks_to_run) {
    try {
      cb(validated_val);
    } catch (const std::exception& e) {
      // 防止某个不讲武德的插件在回调里抛出异常，把整个配置服务搞崩
      std::cerr
          << "[Config Error] Exception thrown during plugin callback execution:"
          << e.what() << std::endl;
    } catch (...) {
      std::cerr << "[Config Error] Unknown exception thrown during plugin "
                   "callback execution."
                << std::endl;
    }
  }
  recursion_depth--;

  // 3. 【核心修复】：如果子树中检测到了死循环，剥夺最外层的成功返回值
  if (has_cyclic_error) {
    return false;
  }

  // 无锁广播审计事件！
  if (value_changed) {
    z3y::FireGlobalEvent<ConfigChangedEvent>(audit_evt);
    AsyncSaveSnapshot();
  }
  return true;
}

std::vector<std::string> ConfigProviderService::ApplyBatch(
    const std::map<std::string, ConfigValue>& changes,
    const std::string& operator_role) {
  std::vector<std::string> errors;

  struct LockPair {
    std::shared_ptr<ConfigEntry> entry;
    std::string path;
    ConfigValue new_val;
  };
  std::vector<LockPair> pairs;

  {
    std::shared_lock<std::shared_mutex> dict_lock(dict_mutex_);
    for (const auto& kv : changes) {
      auto it = config_dict_.find(kv.first);
      if (it == config_dict_.end()) {
        errors.push_back("Path does not exist:" + kv.first);
        return errors;  // 严格事务拦截：若有一个路径不对，整个事务必须无条件流产截断
      }
      pairs.push_back({it->second, kv.first, kv.second});
    }
  }

  // 【极其硬核的防止交叉死锁机制】
  // 对需要同时加锁的互斥量内存地址强行排序，保证获取锁的顺序绝对单向一致。
  std::sort(pairs.begin(), pairs.end(),
            [](const LockPair& a, const LockPair& b) {
              return a.entry.get() < b.entry.get();
            });

  // RAII 批量加锁门神
  struct BatchLockGuard {
    std::vector<LockPair>& p_ref;
    BatchLockGuard(std::vector<LockPair>& p) : p_ref(p) {
      for (auto& item : p_ref) item.entry->entry_mutex.lock();
    }
    ~BatchLockGuard() {
      for (auto& item : p_ref) item.entry->entry_mutex.unlock();
    }
  };

  // 准备审计事件和回调的容器（存放在锁外）
  std::vector<ConfigChangedEvent> audit_events;
  struct PendingCb {
    const std::function<void(const ConfigValue&)>* cb;
    ConfigValue val;
  };
  std::vector<PendingCb> batch_callbacks;

  {
    // ================= [绝对安全的事务锁内区域] =================
    BatchLockGuard guard(pairs);

    // 1. 在批量锁定状态下执行统一校验
    for (const auto& p : pairs) {
      std::string err;
      if (!ValidateInternal(*(p.entry), p.new_val, operator_role, err)) {
        errors.push_back("Validation failed for parameter [" + p.path +
                         "]: " + err);
      }
    }
    if (!errors.empty())
      return errors;  // 校验哪怕错一个，直接 return（guard 自动解锁）

    // 2. 校验全通：原子赋值、生成审计事件、收集回调
    for (auto& p : pairs) {
      if (p.entry->current_value != p.new_val) {
        ConfigChangedEvent audit_evt;
        audit_evt.path = p.path;
        audit_evt.old_value = ConfigValueToString(p.entry->current_value);
        audit_evt.new_value = ConfigValueToString(p.new_val);
        audit_evt.operator_role =
            operator_role.empty() ? "System" : operator_role;
        audit_evt.timestamp_ms = GetCurrentTimestampMs();
        audit_events.push_back(audit_evt);

        p.entry->current_value = p.new_val;
        for (const auto& cb_pair : p.entry->callbacks) {
          batch_callbacks.push_back({&cb_pair.second, p.new_val});
        }
      }
    }
  }  // <--- guard 在这里超出作用域析构，所有的节点锁被安全释放！

  // ================= [完全无锁的派发区域] =================

  // 1. 触发所有收集到的回调任务
  for (const auto& task : batch_callbacks) {
    try {
      (*(task.cb))(task.val);
    } catch (...) {
      // 隔离业务端抛出的异常
    }
  }

  // 2. 无锁广播审计事件
  if (!audit_events.empty()) {
    for (const auto& evt : audit_events) {
      z3y::FireGlobalEvent<ConfigChangedEvent>(evt);
    }
  }

  // 3. 异步落盘
  if (!batch_callbacks.empty()) {
    AsyncSaveSnapshot();
  }

  return errors;
}

std::map<std::string, ConfigSnapshot> ConfigProviderService::GetAllConfigs()
    const {
  std::map<std::string, ConfigSnapshot> result;
  // 仅需对全局字典加一次读锁
  std::shared_lock<std::shared_mutex> dict_lock(dict_mutex_);
  for (const auto& kv : config_dict_) {
    // 必须用读锁逐个读取，虽然是快照也不能容忍半脏数据
    std::shared_lock<std::shared_mutex> entry_lock(kv.second->entry_mutex);
    if (!kv.second->meta
             .is_hidden) {  // 【核心过滤】将隐藏参数拒之于 UI 显示之外
      result[kv.first] = {kv.second->meta, kv.second->current_value};
    }
  }
  return result;
}

void ConfigProviderService::SetStoragePath(const std::string& absolute_path) {
  // 夺取最高权限防止重定向并发异常
  std::unique_lock<std::shared_mutex> dict_lock(dict_mutex_);

  config_file_path_ = absolute_path;
  config_tmp_path_ = absolute_path + ".tmp";

  // 路径改变后，立即触发一次读取
  LoadFromFile();
}

void ConfigProviderService::LoadFromFile() {
  // 【新增：启动时读取 config.json】
  std::ifstream ifs(config_file_path_);
  if (ifs.is_open()) {
    try {
      nlohmann::json root;
      ifs >> root;
      // 存入临时缓存，等待各模块注册时认领
      for (auto& el : root.items()) {
        initial_load_cache_[el.key()] = el.value();  // 全量搬入未定型缓存池
      }
      std::cout << "[Config IO] Configuration file loaded successfully, parsed "
                << initial_load_cache_.size() << " nodes." << std::endl;
    } catch (const std::exception& e) {
      std::cerr << "[Config IO Error] Configuration file format corrupted: "
                << e.what() << std::endl;
    }
  }
}

bool ConfigProviderService::ValidateInternal(const ConfigEntry& entry,
                                             const ConfigValue& new_val,
                                             const std::string& role,
                                             std::string& out_error) const {
  if (entry.meta.read_only) {
    out_error = "Parameter is read-only.";
    return false;
  }

  // 极度细粒度的权限校验拦截
  if (!entry.meta.permission_token.empty() &&
      role != entry.meta.permission_token) {
    out_error = "Insufficient permissions.";
    return false;
  }

  // 阻止一切试图改变底层数据类型的变体覆盖操作
  if (new_val.index() != entry.default_value.index()) {
    out_error = "Data type mismatch.";
    return false;
  }

  // -------------各类型的刚性数值边界校验逻辑---------------------
  if (std::holds_alternative<int64_t>(new_val)) {
    int64_t val = std::get<int64_t>(new_val);
    if (std::holds_alternative<int64_t>(entry.meta.min_val) &&
        val < std::get<int64_t>(entry.meta.min_val)) {
      out_error = "Value is below the allowed minimum.";
      return false;
    }
    if (std::holds_alternative<int64_t>(entry.meta.max_val) &&
        val > std::get<int64_t>(entry.meta.max_val)) {
      out_error = "Value exceeds the allowed maximum.";
      return false;
    }
  }
  // 针对 double 类型的边界校验
  else if (std::holds_alternative<double>(new_val)) {
    double val = std::get<double>(new_val);
    if (std::holds_alternative<double>(entry.meta.min_val) &&
        val < std::get<double>(entry.meta.min_val)) {
      out_error = "Floating-point value is below the allowed minimum.";
      return false;
    }
    if (std::holds_alternative<double>(entry.meta.max_val) &&
        val > std::get<double>(entry.meta.max_val)) {
      out_error = "Floating-point value exceeds the allowed maximum.";
      return false;
    }
  }
  // 【新增】：针对整型数组的边界校验
  else if (std::holds_alternative<std::vector<int64_t>>(new_val)) {
    const auto& vec = std::get<std::vector<int64_t>>(new_val);
    for (size_t i = 0; i < vec.size(); ++i) {
      if (std::holds_alternative<int64_t>(entry.meta.min_val) &&
          vec[i] < std::get<int64_t>(entry.meta.min_val)) {
        out_error = "Value of array element at index " + std::to_string(i) +
                    " is below the allowed minimum.";
        return false;
      }
      if (std::holds_alternative<int64_t>(entry.meta.max_val) &&
          vec[i] > std::get<int64_t>(entry.meta.max_val)) {
        out_error = "Value of array element at index " + std::to_string(i) +
                    " exceeds the allowed maximum.";
        return false;
      }
    }
  }
  // 【新增】：针对浮点型数组的边界校验
  else if (std::holds_alternative<std::vector<double>>(new_val)) {
    const auto& vec = std::get<std::vector<double>>(new_val);
    for (size_t i = 0; i < vec.size(); ++i) {
      if (std::holds_alternative<double>(entry.meta.min_val) &&
          vec[i] < std::get<double>(entry.meta.min_val)) {
        out_error = "Value of array element at index " + std::to_string(i) +
                    " is below the allowed minimum.";
        return false;
      }
      if (std::holds_alternative<double>(entry.meta.max_val) &&
          vec[i] > std::get<double>(entry.meta.max_val)) {
        out_error = "Value of array element at index " + std::to_string(i) +
                    " exceeds the allowed maximum.";
        return false;
      }
    }
  }

  // 【自定义高级校验防线】：这是最后一关，将权利交回给业务插件自己写死的 Lambda
  if (entry.meta.custom_validator) {
    // 此时已经保证了 new_val 的底层类型正确，大胆执行闭包
    std::string custom_err = entry.meta.custom_validator(new_val);
    if (!custom_err.empty()) {
      out_error = custom_err;  // 拦截！将业务提供的中文报错原因透传给外部
      return false;
    }
  }

  return true;
}

void ConfigProviderService::AsyncSaveSnapshot() {
  // 仅仅触发一个电信号立刻返回，绝不在业务主循环里进行哪怕几微秒的文件系统操作
  {
    std::unique_lock<std::mutex> lock(worker_mutex_);
    is_snapshot_dirty_ = true;
  }
  worker_cv_.notify_one();
}

void ConfigProviderService::WorkerRoutine() {
  while (true) {
    bool should_sleep = false;
    {
      std::unique_lock<std::mutex> lock(worker_mutex_);

      // 线程在此处睡眠，直至有人将脏标记设为 true 或者发出框架终止信号
      worker_cv_.wait(lock,
                      [this] { return stop_worker_ || is_snapshot_dirty_; });

      // 如果只是要求退出，且没有脏数据了，和平离去
      if (stop_worker_ && !is_snapshot_dirty_) {
        break;
      }

      if (is_snapshot_dirty_) {
        should_sleep = true;
        is_snapshot_dirty_ = false;  // 提前清除脏标记
      }
    }

    // 【IO 防抖机制】
    // 业务可能在半秒内调用了 100 次 SetValue（比如拉动滑动条）。
    // 醒来后别急着去保存，稍微等一等。这段时间的狂乱赋值最终都会只产生一次写盘。
    // 在无锁状态下睡眠防抖，绝不阻塞前端的 SetValue 业务！
    if (should_sleep) {
      std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    is_snapshot_dirty_ = false;

    // [提取阶段]
    // 在持有互斥锁的最短时间内，把全局数据全量拷贝成为“内存离线快照”。
    // 这样做可以让你放开锁去进行漫长的 IO 写盘，彻底解放主线程。
    std::map<std::string, ConfigValue> io_snapshot;
    {
      std::shared_lock<std::shared_mutex> dict_lock(dict_mutex_);
      for (const auto& kv : config_dict_) {
        std::shared_lock<std::shared_mutex> entry_lock(kv.second->entry_mutex);
        // 注释掉的隐藏参数过滤：底层数据库持久化时不挑剔是否展示给 UI
        // if (!kv.second->meta.is_hidden) {
        io_snapshot[kv.first] = kv.second->current_value;
        // }
      }
    }

    // [序列化与原子落盘阶段]
    try {
      nlohmann::json root;

      // 1. 保底机制：首先无脑填充未被认领的残留缓存配置，防丢处理。
      {
        std::shared_lock<std::shared_mutex> dict_lock(dict_mutex_);
        for (const auto& kv : initial_load_cache_) {
          root[kv.first] = kv.second;  // cache 里存的本来就是 nlohmann::json
        }
      }

      // 2. 覆盖机制：将当前最新的提取快照覆盖进去（同名键会天然顶掉旧缓存）
      for (const auto& kv : io_snapshot) {
        root[kv.first] = ConfigValueToJson(kv.second);
      }

      // 3. 原子覆写 (Atomic Override) 操作
      // 先写到一个不存在的 .tmp 文件中。
      std::ofstream ofs(config_tmp_path_, std::ios::trunc);
      if (ofs.is_open()) {
        ofs << root.dump(4);  // 4个空格的漂亮缩进
        ofs.close();

        // 使用操作系统级的重命名接口。
        // 好处：如果前面大篇幅的 dump 写入中断电了，原始的 config.json
        // 并未损坏！
        std::error_code ec;
        std::filesystem::rename(config_tmp_path_, config_file_path_, ec);
        if (ec) {
          std::cerr << "[Config IO Error] Rename failed: " << ec.message()
                    << std::endl;
        }
      }
    } catch (const std::exception& e) {
      std::cerr << "[Config IO Fatal] Serialization crashed: " << e.what()
                << std::endl;
    }

    // 检测到框架正在下达逐客令，处理完最后一次脏数据便结束自己的一生
    if (stop_worker_) {
      break;
    }
  }
}

bool ConfigProviderService::ReloadFromFile() {
  std::ifstream ifs(config_file_path_);
  if (!ifs.is_open()) {
    std::cerr << "[Config Error] Reload failed: Cannot open file "
              << config_file_path_ << std::endl;
    return false;
  }

  nlohmann::json root;
  try {
    ifs >> root;
  } catch (const nlohmann::json::exception& e) {
    std::cerr << "[Config Error] Reload failed: JSON parse error: " << e.what()
              << std::endl;
    return false;  // 严格拦截：文件损坏时不破坏当前内存任何状态
  }

  // 【核心设计 1：闭包收集器】
  // 用于收集所有需要被触发的回调。我们绝不在持有锁的时候去执行它！
  struct PendingCallback {
    std::function<void(const ConfigValue&)> cb;
    ConfigValue val;
  };
  std::vector<PendingCallback> callbacks_to_run;

  // 【新增】：审计事件篮子
  std::vector<ConfigChangedEvent> audit_events;

  // 【终极并发修复】：准备一个临时篮子，专门装“孤儿数据”，绝不在读锁里写数据！
  std::unordered_map<std::string, nlohmann::json> pending_orphans;

  {
    // 【核心设计 2：细粒度并发控制】
    // 使用共享读锁保护全局字典。因为 Reload
    // 只是修改现有节点的值，不增删节点拓扑。
    std::shared_lock<std::shared_mutex> dict_lock(dict_mutex_);

    // 1. 遍历内存中所有活跃的节点
    for (const auto& [path, entry_ptr] : config_dict_) {
      if (!root.contains(path)) {
        continue;  // 磁盘文件中不存在该项，略过（保持内存原状）
      }

      // 获取该特定节点的独占写锁
      std::unique_lock<std::shared_mutex> entry_lock(entry_ptr->entry_mutex);

      // 【修复】：占位节点处理。将其放入局部篮子 pending_orphans 中，而非直接写
      // initial_load_cache_！
      if (std::holds_alternative<std::monostate>(entry_ptr->default_value)) {
        pending_orphans[path] = root[path];
        continue;
      }

      ConfigValue parsed_val;
      // 借助 default_value 的类型信息，安全地将无类型的 JSON 解析为强类型的
      // ConfigValue
      if (JsonToConfigValue(root[path], entry_ptr->default_value, parsed_val)) {
        std::string err_msg;

        // 【核心设计 3：合法性防线】
        // 外部用记事本瞎改的数据，必须经过严格的 Schema 校验
        // (Min/Max/只读/类型)。
        if (ValidateInternal(*entry_ptr, parsed_val, "System_Reload",
                             err_msg)) {
          // 【核心设计 4：实质变化检测】
          // 只有当数据真正发生改变时，才更新内存并收集回调。
          // 避免运维人员只改了 A 参数，却导致 B
          // 参数也触发了毫无意义的硬件重置。
          if (entry_ptr->current_value != parsed_val) {
            // 【新增】：在内存被覆盖前，生成审计事件
            ConfigChangedEvent audit_evt;
            audit_evt.path = path;
            audit_evt.old_value = ConfigValueToString(entry_ptr->current_value);
            audit_evt.new_value = ConfigValueToString(parsed_val);
            audit_evt.operator_role = "System_Reload";
            audit_evt.timestamp_ms = GetCurrentTimestampMs();
            audit_events.push_back(audit_evt);

            entry_ptr->current_value = parsed_val;

            // 遍历并收集该节点下所有嗷嗷待哺的订阅者
            for (const auto& cb_pair : entry_ptr->callbacks) {
              callbacks_to_run.push_back({cb_pair.second, parsed_val});
            }
          }
        } else {
          std::cerr << "[Config Warn] Reload rejected for path [" << path
                    << "]: " << err_msg << ". Kept old value." << std::endl;
        }
      }
    }

    // 【修复】：孤儿节点处理。同样放入局部篮子 pending_orphans 中！
    for (auto& el : root.items()) {
      if (config_dict_.find(el.key()) == config_dict_.end()) {
        pending_orphans[el.key()] = el.value();
      }
    }
  }  // <=== dict_lock 和所有的 entry_lock 在这里被自动释放

  // 【终极并发修复落实】：如果发现了孤儿数据，单独获取极短暂的【独占写锁】合并！
  if (!pending_orphans.empty()) {
    std::unique_lock<std::shared_mutex> dict_write_lock(dict_mutex_);
    for (const auto& kv : pending_orphans) {
      initial_load_cache_[kv.first] = kv.second;
    }
  }

  // 【核心设计 6：无锁派发回调】
  // 锁已经全部释放，业务线程畅通无阻。现在安全地触发所有回调。
  for (const auto& task : callbacks_to_run) {
    try {
      task.cb(task.val);
    } catch (const std::exception& e) {
      std::cerr << "[Config Error] Exception in Reload callback: " << e.what()
                << std::endl;
    } catch (...) {
      std::cerr << "[Config Error] Unknown exception in Reload callback."
                << std::endl;
    }
  }

  // 【核心新增】：安全触发所有被 Reload 篡改的参数的审计事件！
  if (!audit_events.empty()) {
    for (const auto& evt : audit_events) {
      z3y::FireGlobalEvent<ConfigChangedEvent>(evt);
    }
    AsyncSaveSnapshot();
  }

  std::cout << "[Config Info] Reload completed. Triggered "
            << callbacks_to_run.size() << " callbacks." << std::endl;
  return true;
}

// ============================================================================
// 1. 恢复出厂设置 (复用 ApplyBatch 事务锁)
// ============================================================================

bool ConfigProviderService::ResetToDefault(const std::string& path) {
  ConfigValue def_val;

  {
    // [第一阶段] 极速只读锁：从字典中查询目标节点的默认值
    std::shared_lock<std::shared_mutex> dict_lock(dict_mutex_);
    auto it = config_dict_.find(path);
    if (it == config_dict_.end()) return false;

    std::shared_lock<std::shared_mutex> entry_lock(it->second->entry_mutex);
    def_val = it->second->default_value;
  }

  // 拦截占位节点：如果它还没被真正注册过，默认值会是 std::monostate，拒绝重置
  if (std::holds_alternative<std::monostate>(def_val)) return false;

  // 【架构精髓】：绝不在此时自己写加锁逻辑！
  // 直接委托给极其健壮的 ApplyBatch
  // 事务处理器，它会自动完成验证、防死锁、以及无锁派发回调。
  auto errors = ApplyBatch({{path, def_val}}, "System_Reset_Action");

  return errors.empty();
}

void ConfigProviderService::ResetGroupToDefault(const std::string& group_key) {
  std::map<std::string, ConfigValue> batch_changes;

  {
    // [第一阶段] 收集特定 Group 下的所有默认值
    std::shared_lock<std::shared_mutex> dict_lock(dict_mutex_);
    for (const auto& [path, entry_ptr] : config_dict_) {
      std::shared_lock<std::shared_mutex> entry_lock(entry_ptr->entry_mutex);

      // 过滤匹配的 Group，并且剔除掉未经注册的占位节点
      if (entry_ptr->meta.group_key == group_key &&
          !std::holds_alternative<std::monostate>(entry_ptr->default_value)) {
        // 优化：只有当前值与默认值不一样时，才加入修改批次，避免无意义的触发
        if (entry_ptr->current_value != entry_ptr->default_value) {
          batch_changes[path] = entry_ptr->default_value;
        }
      }
    }
  }

  // [第二阶段] 如果没有变化，直接返回
  if (batch_changes.empty()) return;

  // [第三阶段] 复用 ACID 事务批量提交
  // 这将保证模块级的重置是一次性生效的，即便内部发生了地址抢占，也会被排序算法化解。
  auto errors = ApplyBatch(batch_changes, "System_GroupReset_Action");

  if (!errors.empty()) {
    std::cerr << "[Config Error] Failed to reset group '" << group_key
              << "'. Errors:" << std::endl;
    for (const auto& err : errors) std::cerr << "  - " << err << std::endl;
  }
}

// ============================================================================
// 2. 配方/配置文件 导出与导入
// ============================================================================

bool ConfigProviderService::ExportToFile(const std::string& target_path) const {
  nlohmann::json root;

  {
    // 获取全局字典拓扑保护锁
    std::shared_lock<std::shared_mutex> dict_lock(dict_mutex_);

    // 1. 先填充未定型的“孤儿数据”，保证配方导出时不会丢失未加载插件的配置
    for (const auto& kv : initial_load_cache_) {
      root[kv.first] = kv.second;
    }

    // 2. 填充已注册活跃节点的当前内存值
    for (const auto& kv : config_dict_) {
      std::shared_lock<std::shared_mutex> entry_lock(kv.second->entry_mutex);
      // 调用本文件匿名命名空间中的辅助函数：ConfigValueToJson
      root[kv.first] = ConfigValueToJson(kv.second->current_value);
    }
  }

  // 3. 序列化并写入目标绝对路径
  try {
    std::ofstream ofs(target_path, std::ios::trunc);
    if (!ofs.is_open()) return false;
    ofs << root.dump(4);
    ofs.close();
    return true;
  } catch (const std::exception& e) {
    std::cerr << "[Config Fatal] ExportToFile failed: " << e.what()
              << std::endl;
    return false;
  }
}

bool ConfigProviderService::ImportFromFile(const std::string& source_path,
                                           bool apply_immediately) {
  std::error_code ec;

  // 1. 利用标准库，原子级覆盖当前系统的配置文件 (config.json)
  std::filesystem::copy_file(source_path, config_file_path_,
                             std::filesystem::copy_options::overwrite_existing,
                             ec);

  if (ec) {
    std::cerr << "[Config Error] Import failed. Cannot copy file: "
              << ec.message() << std::endl;
    return false;
  }

  // 2. 如果要求立刻生效，则直接调用我们之前写好的 ReloadFromFile
  if (apply_immediately) {
    return ReloadFromFile();
  }

  return true;
}

// ============================================================================
// 3. 前端的高级搜索与层级过滤 API
// ============================================================================
std::vector<std::string> ConfigProviderService::GetAllGroupKeys() const {
  std::set<std::string> groups;
  std::shared_lock<std::shared_mutex> dict_lock(dict_mutex_);

  for (const auto& [path, entry_ptr] : config_dict_) {
    // 只有非隐藏且设置了 GroupKey 的才返回给 UI
    std::shared_lock<std::shared_mutex> entry_lock(entry_ptr->entry_mutex);
    if (!entry_ptr->meta.is_hidden && !entry_ptr->meta.group_key.empty()) {
      groups.insert(entry_ptr->meta.group_key);
    }
  }
  return std::vector<std::string>(groups.begin(), groups.end());
}

std::map<std::string, ConfigSnapshot> ConfigProviderService::GetConfigsByGroup(
    const std::string& group_key) const {
  std::map<std::string, ConfigSnapshot> result;
  std::shared_lock<std::shared_mutex> dict_lock(dict_mutex_);

  for (const auto& [path, entry_ptr] : config_dict_) {
    // 【必须】给特定节点加读锁，防止在快照打包时该节点数据被其他线程篡改
    std::shared_lock<std::shared_mutex> entry_lock(entry_ptr->entry_mutex);

    // 双重过滤：1. 隐藏参数不给前端 ； 2. Group 名称必须匹配
    if (!entry_ptr->meta.is_hidden && entry_ptr->meta.group_key == group_key) {
      result[path] = {entry_ptr->meta, entry_ptr->current_value};
    }
  }

  return result;
}

}  // namespace config
}  // namespace plugins
}  // namespace z3y