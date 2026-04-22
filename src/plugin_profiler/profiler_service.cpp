/**
 * @file profiler_service.cpp
 * @brief 性能分析插件的核心实现逻辑。
 * * @details
 * 【面向维护者 - 工业级稳定性大揭秘】
 * 1. **UAF 防御 (Use-After-Free)**：
 * 当宿主框架热重载（卸载又重新装载）该插件时，旧的 master_nodes_
 * 会被析构。如果工作线程 的 `thread_local`
 * 不做感知，将会拿到失效的内存指针。这里通过 `g_profiler_service_instance` 和
 * `tls_last_service_instance` 双重原子比对，在重载瞬间强行清空 TLS
 * 缓存，完美避开段错误。
 * 2. **三级内存分配机制**：
 * 分配请求 -> 线程私有池(tls_free_nodes, O(1)无锁) -> 全局池(free_nodes_,
 * 批量获取, 低锁碰撞) -> OS 堆内存(new)
 * 3. **OOM 绝对防爆阀**：
 * 如果业务人员在错误的分支中生成了无限数量的节点名称，`master_nodes_.size() >=
 * 2000000` 将强制返回 nullptr 拒绝服务。在工业检测中，保全宿主系统内存（不
 * OOM）永远高于收集监控数据。
 * 4. **并发撕裂与僵尸节点驱逐**：
 * `ReleaseChildren` 和 `Formatxxx` 中采用了
 * Snapshot（快照）自旋锁机制，防止异步 Worker
 * 正在挂载节点时主线程去遍历树导致链表崩溃。
 * 对于 `g_async_slots`，当环形缓冲区满载僵尸（由于某异常没调
 * Commit）时，会强制驱逐旧节点。
 */

#include "profiler_service.h"

#include <spdlog/fmt/fmt.h>

#include <algorithm>

#include "interfaces_core/z3y_log_macros.h"

Z3Y_AUTO_REGISTER_SERVICE(z3y::plugins::profiler::ProfilerService,
                          "System.Profiler", true);

namespace z3y::plugins::profiler {

using namespace z3y::interfaces::core;
using namespace z3y::interfaces::profiler;

std::atomic<uint64_t> ProfilerService::g_instance_counter_{1};

// 全局静态原子指针，用于跟踪当前的 Service 实例存活状态，切断 DLL
// 卸载期的重入死锁
static std::atomic<ProfilerService*> g_profiler_service_instance{nullptr};

thread_local uint64_t tls_last_service_id = 0;
thread_local z3y::interfaces::profiler::AggregatorNode*
    tls_free_nodes[256];  // 假设最多缓存256个
thread_local int tls_free_node_count = 0;

/**
 * @brief 线程局部变量包装器，用于在线程退出时触发 C++ 运行时自动垃圾回收。
 */
struct ProfilerThreadStateWrapper {
  ProfilerThreadState state;
  ~ProfilerThreadStateWrapper() {
    // 只有在 Service
    // 实例存活时（没有在卸载期），才安全地将自己名下的根树归还给对象池。
    if (auto* svc =
            g_profiler_service_instance.load(std::memory_order_acquire)) {
      // 仅当当前存活的实例，就是当初分配这些节点的那个实例时，才允许释放
      if (svc && svc->GetInstanceId() == tls_last_service_id) {
        for (size_t i = 0; i < state.thread_root_count; ++i) {
          svc->ReleaseNodeTree(state.thread_roots[i]);
        }
      }
    }
  }
};
thread_local ProfilerThreadStateWrapper t_profiler_state_wrapper;

/**
 * @brief 异步分析流槽位结构，采用环形缓冲区实现无锁分配。
 */
struct AsyncSlot {
  std::atomic<uint64_t> active_frame_id{
      0};                    ///< 当前槽位服务的目标帧ID，0表示空闲
  std::atomic<uint32_t> ref_count{
      0};                    // [新增] 根级侵入式引用计数，防止僵尸线程 UAF
  AggregatorNode root_node;  ///< 该异步流的专属根节点
  ProfileNodeData dynamic_info{nullptr, "", 0,
                               NodeType::Timer};  ///< 预置的动态元信息
  uint32_t period = 1;                            ///< 触发报告的次数阈值
  double sla_ms = 0.0;                            ///< SLA 容忍极限
};
static AsyncSlot g_async_slots[1024];

ProfilerService::ProfilerService() {
  uint64_t timestamp =
      std::chrono::steady_clock::now().time_since_epoch().count();
  instance_id_ =
      timestamp ^ g_instance_counter_.fetch_add(1, std::memory_order_relaxed);
}

bool ProfilerService::IsEnabled() const {
  return enable_.load(std::memory_order_relaxed);
}

void ProfilerService::Initialize() {
  g_profiler_service_instance.store(this, std::memory_order_release);
  is_active_.store(true, std::memory_order_release);

  if (auto [log_mgr, err] = z3y::TryGetDefaultService<ILogManagerService>();
      err == z3y::InstanceError::kSuccess) {
    profiler_logger_ = log_mgr->GetLogger("System.Profiler");
  }
  // 监听配置中心的控制开关
  if (auto [cfg_svc, err] = z3y::TryGetDefaultService<IConfigService>();
      err == z3y::InstanceError::kSuccess) {
    config_conns_ += cfg_svc->Builder<bool>("System.Profiler.Enable")
                         .NameKey("Profiler Enable")
                         .Default(true)
                         .Bind([this](bool val) {
                           enable_.store(val, std::memory_order_relaxed);
                         });
  }
}

void ProfilerService::Shutdown() {
  is_active_.store(false, std::memory_order_release);
  g_profiler_service_instance.store(nullptr, std::memory_order_release);

  // [新增] 强行清理静态槽位中的幽灵指针，防止下次重载时发生 UAF
  for (auto& slot : g_async_slots) {
    slot.active_frame_id.store(0, std::memory_order_relaxed);
    // 使用线程安全的隔离函数，确保没有异步线程正在操作它
    ReleaseChildren(&slot.root_node);
  }

  config_conns_.Clear();
  profiler_logger_.reset();
}

ProfilerThreadState* ProfilerService::GetOrCreateThreadState() {
  // 这里假设你原本的代码是获取本地的 thread_local state wrapper
  // 比如：auto* state = &t_profiler_state_wrapper.state;
  auto* state = &t_profiler_state_wrapper.state;

  // 【新增防御】：检测当前线程的 TLS 状态是否属于上一次加载的实例
  static thread_local uint64_t tls_current_state_service_id = 0;

  if (tls_current_state_service_id != this->instance_id_) {
    // 一旦发现 Service 发生换届（如批量跑 GTest 时的组件重载）
    // 必须将这个主线程残留的分析栈和野指针彻底清空！
    state->current_root = nullptr;
    state->stack_depth = 0;

    // 彻底清空缓存的根节点数组，防止 ScopedRoot 顺藤摸瓜找到野指针触发崩溃
    for (int i = 0; i < 32; ++i) {
      state->thread_roots[i] = nullptr;
    }

    // 彻底清空影子栈
    for (int i = 0; i < 128; ++i) {
      state->shadow_stack[i] = nullptr;
    }

    // 记录新的从属身份
    tls_current_state_service_id = this->instance_id_;
    state->thread_root_count = 0;  // 必须彻底复位计数器
  }

  return state;
}

void ProfilerService::ReturnTlsNodesToGlobal(
    const std::vector<AggregatorNode*>& nodes) {
  if (nodes.empty()) return;
  std::lock_guard<std::mutex> lock(alloc_mutex_);
  free_nodes_.insert(free_nodes_.end(), nodes.begin(), nodes.end());
}

AggregatorNode* ProfilerService::AcquireNode() {
  // 生命周期安全校验：如果当前线程记录的服务地址与 this
  // 不同，说明发生了组件热插拔，立刻清空该线程兜里的失效野指针池。
  if (tls_last_service_id != this->instance_id_) {
    tls_free_node_count = 0;  // O(1) 瞬间废弃野指针，绝不触发 free()
    tls_last_service_id = this->instance_id_;
  }

  // 2. 优先从 TLS 原始数组中拿
  if (tls_free_node_count > 0) {
    AggregatorNode* node = tls_free_nodes[--tls_free_node_count];
    node->Reset();
    return node;
  }

  // 3. 如果 TLS 没货，再去全局池拿
  std::lock_guard<std::mutex> lock(alloc_mutex_);
  if (!free_nodes_.empty()) {
    AggregatorNode* node = free_nodes_.back();
    free_nodes_.pop_back();
    node->Reset();
    return node;
  }

// 4. 扩容路径：如果全局池也没货，且没有超过硬性上限，则创建新节点
  if (master_nodes_.size() >= 2000000) {
    return nullptr;  // 达到 200W 个节点上限，触发防爆阀，防止 OOM 撑爆物理内存
  }

  master_nodes_.push_back(
      std::make_unique<z3y::interfaces::profiler::AggregatorNode>());
  z3y::interfaces::profiler::AggregatorNode* node = master_nodes_.back().get();
  node->Reset();
  return node;
}

void ProfilerService::ReleaseNodeTree(
    z3y::interfaces::profiler::AggregatorNode* node) {
  if (!node) return;

  // 1. 先释放它的所有子节点（保留你原有的树遍历逻辑）
  ReleaseChildren(node);

  // 2.
  // 跨边界防御：实例换届检查，防止把当前实例的节点塞进上一个实例残留的数组逻辑里
  if (tls_last_service_id != this->instance_id_) {
    tls_free_node_count = 0;
    tls_last_service_id = this->instance_id_;
  }

  // 3. 极速归还：尝试塞回本线程的无锁口袋
  if (tls_free_node_count < 256) {
    tls_free_nodes[tls_free_node_count++] = node;
  } else {
    // 4. 溢出归还：如果口袋满了 (超过 256 个)，则归还给中央全局池
    std::lock_guard<std::mutex> lock(alloc_mutex_);
    free_nodes_.push_back(node);
  }
}

void ProfilerService::ReleaseChildren(AggregatorNode* parent) {
  if (!parent) return;

  // 【带锁原子摘除】
  // 为了防止在遍历销毁子树时，其他异步 Worker
  // 正好同时在向该树挂载新节点导致链表撕裂引发崩溃，
  // 必须使用自旋锁保护，一刀切断 first_child
  // 指针（原子摘除），然后在无锁状态下慢慢递归释放被隔离的子树。
  while (parent->lock.test_and_set(std::memory_order_acquire)) {
#if defined(_MSC_VER) || defined(__i386__) || defined(__x86_64__)
    _mm_pause();  // 加入 Pause 指令，极大缓解空转烧核与内存总线拥挤
#elif defined(__arm__) || defined(__aarch64__)
    __asm__ volatile("yield");
#endif
  }
  AggregatorNode* detached_child_tree = parent->first_child;
  parent->first_child = nullptr;
  parent->lock.clear(std::memory_order_release);

  if (detached_child_tree) {
    ReleaseNodeTree(detached_child_tree);
  }
}

void ProfilerService::SubmitRootForCheck(AggregatorNode* root, uint32_t period,
                                         double sla_ms) {
  bool is_enabled = enable_.load(std::memory_order_relaxed);
  if (!is_enabled || !profiler_logger_) {
    if (root) {
      ReleaseChildren(root);
      auto* saved_info = root->static_info;  // [修复] 保存节点身份
      root->Reset();
      root->static_info = saved_info;  // [修复] 恢复节点身份
    }
    return;
  }
  if (!root) return;

  // [修改] 原子自增并获取当前值
  uint64_t current_calls =
      root->call_count.fetch_add(1, std::memory_order_relaxed) + 1;
  double def_sla = default_sla_ms_.load(std::memory_order_relaxed);
  double effective_sla = (sla_ms > 0.0) ? sla_ms : def_sla;

  // [新增] 安全提取毫秒耗时快照
  double current_total_ms = root->GetTotalTimeMs();

  bool should_report =
      (effective_sla > 0.0 && current_total_ms > effective_sla) ||
      (period > 0 && current_calls >= period);

  if (should_report) {
    std::string reason;
    if (effective_sla > 0.0 && current_total_ms > effective_sla) {
      reason =
          fmt::format("SLA Timeout (Limit: {:.1f}ms, Actual: {:.1f}ms)",
                      effective_sla, current_total_ms);  // [修改] 使用快照变量
    } else {
      reason = fmt::format("Periodic Tick (Period: {})", period);
    }
    GenerateReportAndLog(root, reason);
    // 汇报完毕后，清理子树数据准备下一个周期的积攒
    ReleaseChildren(root);
    auto* saved_info = root->static_info;  // [修复] 保存节点身份
    root->Reset();
    root->static_info = saved_info;  // [修复] 恢复节点身份
  }
}

void ProfilerService::GenerateReportAndLog(AggregatorNode* root,
                                           const std::string& reason) {
  std::string report = fmt::format(
      "\n[Z3Y Profiler] Performance Report | Trigger: {}\n", reason);
  for (size_t i = 0; i < root->tag_count; ++i) {
    report += fmt::format("Global Context: [{}: {}] ", root->tags[i].key,
                          root->tags[i].value);
  }
  if (root->tag_count > 0) report += "\n";

  report +=
      "========================================================================"
      "===========\n";
  report +=
      "Node Name                             Count     Avg(ms)   Min(ms)   "
      "Max(ms)   %Time\n";
  report +=
      "------------------------------------------------------------------------"
      "-----------\n";

  FormatTimerRecursive(report, root, 0, root->GetTotalTimeMs());

  report +=
      "------------------------------------------------------------------------"
      "-----------\n";
  report += "[Metrics]\n";
  FormatMetricsRecursive(report, root);
  report +=
      "========================================================================"
      "===========\n";

  // 注意：极高频工业环境，强烈建议底层 Logger 实现采用异步文件队列机制（Async
  // Sink），防止 IO 阻滞。
  z3y::interfaces::core::LogSourceLocation loc{__FILE__, __LINE__,
                                               __FUNCTION__};
  profiler_logger_->Log(loc, z3y::interfaces::core::LogLevel::Warn,
                        report.c_str());
}

void ProfilerService::FormatTimerRecursive(std::string& output,
                                           AggregatorNode* node, int depth,
                                           double root_ms) {
  if (!node || !node->static_info) return;
  if (node->static_info->type != NodeType::Value &&
      node->static_info->type != NodeType::Event) {
    std::string indent(depth * 2, ' ');
    std::string prefix = (depth > 0) ? "|- " : "";
    std::string raw_name = indent + prefix + node->static_info->name;
    if (raw_name.length() > 34) raw_name = raw_name.substr(0, 31) + "...";

    // [修复] 提前通过 load() 取出常规数值快照
    uint64_t current_count = node->call_count.load(std::memory_order_relaxed);

    // 后面的算术运算和格式化，全部使用 current_count 这个普通变量
    double avg =
        current_count > 0 ? (node->GetTotalTimeMs() / current_count) : 0.0;
    double percent =
        (root_ms > 0) ? (node->GetTotalTimeMs() / root_ms * 100.0) : 100.0;
    if (percent > 100.0) percent = 100.0;

    output += fmt::format("{:<37} {:<9} {:<9.2f} {:<9.2f} {:<9.2f} {:.1f}%\n",
                          raw_name, current_count, avg, node->GetMinTimeMs(),
                          node->GetMaxTimeMs(), percent);

    for (size_t i = 0; i < node->tag_count; ++i) {
      output += fmt::format("{}  |- [Tag] {}: {}\n", indent, node->tags[i].key,
                            node->tags[i].value);
    }
  }

  // 将快照数组的声明和预分配移到【获取锁的外面】
  std::vector<AggregatorNode*> snapshot;
  snapshot.reserve(16);  // [新增] 预先分配好内存，避免在锁内触发 new

  while (node->lock.test_and_set(std::memory_order_acquire)) {
#if defined(_MSC_VER) || defined(__i386__) || defined(__x86_64__)
    _mm_pause();
#elif defined(__arm__) || defined(__aarch64__)
    __asm__ volatile("yield");
#endif
  }

  AggregatorNode* child = node->first_child;
  while (child) {
    snapshot.push_back(child);  // 现在这里大概率不会触发内存分配了
    child = child->next_sibling;
  }
  node->lock.clear(std::memory_order_release);

  for (auto* c : snapshot) {
    FormatTimerRecursive(output, c, depth + 1, root_ms);
  }
}

void ProfilerService::FormatMetricsRecursive(std::string& output,
                                             AggregatorNode* node) {
  if (!node || !node->static_info) return;

  if (node->static_info->type == NodeType::Value) {
    uint64_t current_count = node->call_count.load(std::memory_order_relaxed);
    double avg =
        current_count > 0 ? (node->GetSumValue() / current_count) : 0.0;

    output += fmt::format(
        " |- {:<26} (Value)  Count: {:<6} Avg: {:<8.2f} Max: {:.2f}\n",
        node->static_info->name, current_count, avg,
        node->GetMaxValue());  // [修复] GetMaxValue()

  } else if (node->static_info->type == NodeType::Event) {
    output += fmt::format(
        " |- {:<26} (Event)  Total Occurrences: {}\n", node->static_info->name,
        node->call_count.load(std::memory_order_relaxed));  // [修复]
  }

  // 将快照数组的声明和预分配移到【获取锁的外面】
  std::vector<AggregatorNode*> snapshot;
  snapshot.reserve(16);  // [新增] 预先分配好内存，避免在锁内触发 new

  while (node->lock.test_and_set(std::memory_order_acquire)) {
#if defined(_MSC_VER) || defined(__i386__) || defined(__x86_64__)
    _mm_pause();
#elif defined(__arm__) || defined(__aarch64__)
    __asm__ volatile("yield");
#endif
  }

  AggregatorNode* child = node->first_child;
  while (child) {
    snapshot.push_back(child);  // 现在这里大概率不会触发内存分配了
    child = child->next_sibling;
  }
  node->lock.clear(std::memory_order_release);

  for (auto* c : snapshot) {
    FormatMetricsRecursive(output, c);
  }
}

void ProfilerService::AsyncBegin(const char* name, uint64_t frame_id,
                                 uint32_t period, double sla_ms) {
  size_t start_idx = frame_id % 1024;
  for (size_t i = 0; i < 1024; ++i) {
    size_t idx = (start_idx + i) % 1024;

    auto& slot = g_async_slots[idx];

    uint64_t expected = 0;

    // [新增防御] 必须且仅当物理槽位完全释放（没有僵尸 Worker
    // 引用）时，才允许复用
    if (slot.ref_count.load(std::memory_order_acquire) == 0) {
      if (slot.active_frame_id.compare_exchange_strong(
              expected, frame_id, std::memory_order_acq_rel)) {
        ReleaseChildren(&slot.root_node);
        slot.root_node.Reset();
        slot.dynamic_info.name = name;
        slot.root_node.static_info = &slot.dynamic_info;
        slot.period = period;
        slot.sla_ms = sla_ms;
        // [新增] 抢占成功，赋予初始主生命周期计数 1
        slot.ref_count.store(1, std::memory_order_release);
        return;
      } else if (expected == frame_id) {
        // 重复的 Begin，软复位
        ReleaseChildren(&slot.root_node);
        slot.root_node.Reset();
        slot.dynamic_info.name = name;
        slot.root_node.static_info = &slot.dynamic_info;
        slot.period = period;
        slot.sla_ms = sla_ms;
        slot.ref_count.store(1, std::memory_order_release);
        return;
      }
    }
  }

  if (profiler_logger_) {
    z3y::interfaces::core::LogSourceLocation loc{__FILE__, __LINE__,
                                                 __FUNCTION__};
    profiler_logger_->Log(
        loc, z3y::interfaces::core::LogLevel::Error,
        "[Profiler Error] 1024 Async Slots fully exhausted by zombies! "
        "Circuit breaker active. Profiling skipped for this frame to protect "
        "process memory.");
  }
}

void ProfilerService::AsyncAttach(uint64_t frame_id) {
  // 【关键修复】：强制触发当前线程的 TLS 初始化与旧状态清理。
  // 防止后续宏调用时发生延迟初始化，将我们下面挂载的数据误杀。
  GetOrCreateThreadState();

  size_t start_idx = frame_id % 1024;
  for (size_t i = 0; i < 1024; ++i) {
    size_t idx = (start_idx + i) % 1024;
    // 使用 Memory_order_acquire 保证读取到的 frame_id 同步一致性
    if (g_async_slots[idx].active_frame_id.load(std::memory_order_acquire) ==
        frame_id) {
      g_async_slots[idx].ref_count.fetch_add(1, std::memory_order_relaxed);

      t_profiler_state_wrapper.state.current_root =
          &(g_async_slots[idx].root_node);
      t_profiler_state_wrapper.state.stack_depth = 1;
      t_profiler_state_wrapper.state.shadow_stack[0] =
          t_profiler_state_wrapper.state.current_root;
      return;
    }
  }
}

void ProfilerService::AsyncCommit(uint64_t frame_id) {
  GetOrCreateThreadState();

  size_t start_idx = frame_id % 1024;
  for (size_t i = 0; i < 1024; ++i) {
    size_t idx = (start_idx + i) % 1024;
    if (g_async_slots[idx].active_frame_id.load(std::memory_order_acquire) ==
        frame_id) {
      auto& slot = g_async_slots[idx];
      SubmitRootForCheck(&slot.root_node, slot.period, slot.sla_ms);
      ReleaseChildren(&slot.root_node);
      // 【修改为】：仅当当前线程确实正挂载在该槽位时，才清空 TLS
      // 防止破坏主调度线程的分析栈导致整数下溢越界
      if (t_profiler_state_wrapper.state.current_root == &slot.root_node) {
        t_profiler_state_wrapper.state.current_root = nullptr;
        t_profiler_state_wrapper.state.stack_depth = 0;
      }
      uint32_t prior_count =
          slot.ref_count.fetch_sub(1, std::memory_order_release);

      if (prior_count == 1) {
        // [新增] Acquire 内存屏障，强迫当前核心读取主存，获取其他所有 Worker
        // 刚刚刷入的数据
        std::atomic_thread_fence(std::memory_order_acquire);

        // 此时绝无任何其他线程访问此槽位，安全生成报表并销毁子树
        SubmitRootForCheck(&slot.root_node, slot.period, slot.sla_ms);
        ReleaseChildren(&slot.root_node);
        slot.active_frame_id.store(0, std::memory_order_release);
      }
      return;
    }
  }
}
}  // namespace z3y::plugins::profiler