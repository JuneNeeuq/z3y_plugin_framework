/**
 * @file profiler_macros.h
 * @brief 业务代码使用的核心耗时监控与指标记录宏。
 * * @details
 * 【面向使用者 - 入门必读】
 * 这是你进行日常算法优化最常用的头文件，全 RAII 机制，无感侵入业务逻辑。
 * - 用法 1：在函数开头写
 * `Z3Y_PROFILE();`，会自动以函数名为名字记录该作用域的耗时。
 * - 用法 2：如果想自定义一段代码的名字，用 `Z3Y_PROFILE_NAMED("MyStep_1");`。
 * - 用法 3：如果是跨线程的流水线监控，主控端发 `Z3Y_PROFILE_ASYNC_BEGIN("Task",
 * id, 1, 0);` Worker 子线程在开头写 `Z3Y_PROFILE_ASYNC_ATTACH(id);`
 * 一切结束后主控端写 `Z3Y_PROFILE_ASYNC_COMMIT(id);` 即可汇总大报告。
 * * ⚠️【重大避坑警告】⚠️
 * 所有宏中带有 `name` 参数的，**绝对、绝对、绝对不准传入动态 std::string 或
 * char 数组！** 必须手写英文字符串（如 "Init_Device"）。我们利用了 C++
 * 的特殊语法 `"" name ""`，
 * 如果你试图传入动态字符串，编译器会直接报错制止你！这是为了防止严重的内存悬空崩溃。
 * 如果你想传动态流水号，请使用 `Z3Y_PROFILE_TAG("SerialID",
 * dyn_str.c_str());`。
 * * 【面向维护者 - 架构剖析】
 * 本文件的核心难点在于 `FindOrCreateNode`
 * 的双重检查锁（DCL）设计，以及服务跨界指针 的生命周期获取。
 */

#pragma once
#include <chrono>
#include <cstring>  // std::memcpy, std::strlen

// 引入平台特定的内联汇编指令头文件，用于 _mm_pause 缓解自旋锁烧核
#if defined(_MSC_VER) || defined(__INTEL_COMPILER)
#include <immintrin.h>
#elif defined(__GNUC__) || defined(__clang__)
#include <x86intrin.h>
#endif

#include "framework/z3y_service_locator.h"
#include "i_profiler_service.h"

namespace z3y::interfaces::profiler {

/**
 * @brief 从 ServiceLocator 中极速获取当前线程的上下文状态缓存。
 * @details
 * 传统跨 DLL 的 inline thread_local 极其危险，极易在卸载时产生段错误。
 * 这里使用了一个极尽巧妙的设计：缓存当前 Service 的内存地址
 * (`cached_service_id`)。 只要服务没有被卸载重装（地址不变），就可以绕过沉重的
 * Locator 查询，实现 O(1) 性能逼近。
 */
inline ProfilerThreadState* GetThreadState() {
  static thread_local ProfilerThreadState* cached_state = nullptr;
  static thread_local uint64_t cached_service_id = 0;

  if (auto [svc, err] = z3y::TryGetDefaultService<IProfilerService>();
      err == z3y::InstanceError::kSuccess) {
    auto current_id = svc->GetInstanceId();
    if (cached_service_id != current_id) {
      cached_state = svc->GetOrCreateThreadState();
      cached_service_id = current_id;
    }
    return cached_state;
  }

  cached_state = nullptr;
  cached_service_id = 0;
  return nullptr;
}

/**
 * @brief 清空当前线程的缓存状态，复位监控层级。
 */
inline void ResetProfilerCache() {
  if (auto* state = GetThreadState()) {
    state->stack_depth = 0;
    state->current_root = nullptr;
    state->thread_root_count = 0;
  }
}

/**
 * @brief 内部调用函数：向当前的 Profiler 上下文绑定一个附加属性标签。
 * @param k 键名（必须静态）
 * @param v 键值（允许动态，会被强制执行最大长度的栈复制）
 */
inline void AddTagInternal(const char* k, const char* v) {
  if (auto* tls = GetThreadState();
      tls && tls->current_root && tls->stack_depth > 0) {
    auto* node = tls->shadow_stack[tls->stack_depth - 1];
    if (node && node->tag_count < 2) {
      auto& tag = node->tags[node->tag_count++];
      tag.key = k;
      if (v) {
        // 【修复 MSVC 警告】：使用 strlen + memcpy 替代 strncpy，
        // 不仅屏蔽了 C4996 警告，而且在编译器底层有极强的 intrinsic
        // 汇编级优化，速度极快
        size_t len = std::strlen(v);
        if (len >= sizeof(tag.value)) {
          len = sizeof(tag.value) - 1;
        }
        std::memcpy(tag.value, v, len);
        tag.value[len] = '\0';
      }
    }
  }
}

/**
 * @brief 在 LCRS 多叉树结构中执行高效的查找或挂载创建动作。
 * * @details
 * 【面向维护者 - 极其精密的双重检查锁定 (DCL) 机制】
 * 步骤一：获取父节点的自旋锁，在子链表中查找命中，找到即返回。未找到则释放锁（避免持锁引发
 * Service 死锁）。 步骤二：无锁状态下向 Service `AcquireNode`。
 * 步骤三：【二次校验】再次获取自旋锁！重新遍历链表。因为在步骤二去申请内存的时间缝隙里，别的线程
 * 可能已经抢先把同样名字的节点挂上去了。如果有，则退还自己申请的，借用已存在的；如果没有，
 * 才真正将其插入链表尾部。
 */
inline AggregatorNode* FindOrCreateNode(ProfileNodeData* data,
                                        AggregatorNode* parent) {
  if (!parent) return nullptr;

  // 第一步：自旋锁保护并引入 CPU Pause 防止死锁争用导致该核 100% 负载降频
  while (parent->lock.test_and_set(std::memory_order_acquire)) {
#if defined(_MSC_VER) || defined(__i386__) || defined(__x86_64__)
    _mm_pause();
#elif defined(__arm__) || defined(__aarch64__)
    __asm__ volatile("yield");
#endif
  }
  AggregatorNode* child = parent->first_child;
  AggregatorNode* last_child = nullptr;
  while (child) {
    if (child->static_info == data) {
      parent->lock.clear(std::memory_order_release);
      return child;
    }
    last_child = child;
    child = child->next_sibling;
  }

  parent->lock.clear(std::memory_order_release);

  AggregatorNode* new_node = nullptr;
  if (auto [svc, err] = z3y::TryGetDefaultService<IProfilerService>();
      err == z3y::InstanceError::kSuccess) {
    new_node = svc->AcquireNode();
  }
  // 遇到防爆上限或底层禁用的情况，优雅降级，返回空从而静默放弃记录
  if (!new_node) return nullptr;

  new_node->static_info = data;
  new_node->parent = parent;

  // 第二步：DCL 双重检查锁定
  while (parent->lock.test_and_set(std::memory_order_acquire)) {
#if defined(_MSC_VER) || defined(__i386__) || defined(__x86_64__)
    _mm_pause();
#elif defined(__arm__) || defined(__aarch64__)
    __asm__ volatile("yield");
#endif
  }
  child = parent->first_child;
  last_child = nullptr;
  while (child) {
    if (child->static_info == data) {
      parent->lock.clear(std::memory_order_release);
      if (auto [svc, err] = z3y::TryGetDefaultService<IProfilerService>();
          err == z3y::InstanceError::kSuccess) {
        svc->ReleaseNodeTree(new_node);  // 被其他抢先，退货防泄漏
      }
      return child;
    }
    last_child = child;
    child = child->next_sibling;
  }

  // 确认属于未占有的新分支，将其串接在链表末端
  if (last_child) {
    last_child->next_sibling = new_node;
  } else {
    parent->first_child = new_node;
  }
  parent->lock.clear(std::memory_order_release);
  return new_node;
}

/**
 * @brief 基于 RAII 的作用域耗时自动测量包装器。
 * @details 构造时记录开始时间，析构时计算差值并写入 Node。
 */
class ScopedTimer {
 public:
  ScopedTimer(ProfileNodeData* static_data)
      : node_(nullptr), tls_state_(nullptr), service_id_(0) {
    // [新增] 在构造时记录获取到的服务实例 ID
    if (auto [svc, err] = z3y::TryGetDefaultService<IProfilerService>();
        err == z3y::InstanceError::kSuccess) {
      service_id_ = svc->GetInstanceId();
    }
    tls_state_ = GetThreadState();
    if (!static_data->enabled.load(std::memory_order_relaxed) || !tls_state_ ||
        !tls_state_->current_root)
      return;

    // 栈保护屏障。超过 128 层直接放弃分析，进入优雅降级，防止一切越界和失步
    if (tls_state_->stack_depth >= 128) {
      return;
    }

    AggregatorNode* parent =
        tls_state_->stack_depth > 0
            ? tls_state_->shadow_stack[tls_state_->stack_depth - 1]
            : tls_state_->current_root;
    node_ = FindOrCreateNode(static_data, parent);
    if (node_) {
      tls_state_->shadow_stack[tls_state_->stack_depth++] = node_;
      start_ts_ = std::chrono::steady_clock::now();
    }
  }
  ~ScopedTimer() {
    if (!node_) return;
    // [修改] 析构时必须强制校验当前服务的实例
    // ID，如果不匹配说明遭遇了热重载，立刻放弃访问野指针
    if (auto [svc, err] = z3y::TryGetDefaultService<IProfilerService>();
        err != z3y::InstanceError::kSuccess ||
        svc->GetInstanceId() != service_id_) {
      return;
    }
    double ms = std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - start_ts_)
                    .count();
    node_->call_count.fetch_add(1, std::memory_order_relaxed);
    uint64_t ns = static_cast<uint64_t>(ms * 1000000.0);
    node_->total_time_ns.fetch_add(ns, std::memory_order_relaxed);
    AtomicUpdateMax(node_->max_time_ms_bits, ms);
    AtomicUpdateMin(node_->min_time_ms_bits, ms);
    if (tls_state_->stack_depth > 0) {
      tls_state_->stack_depth--;
    }
  }

 private:
  AggregatorNode* node_;
  ProfilerThreadState* tls_state_;
  std::chrono::steady_clock::time_point start_ts_;
  uint64_t service_id_;
};

/**
 * @brief 线性流程记录管理器，用于处理一系列同级的非嵌套流水线步序。
 */
class LinearManager {
 public:
  LinearManager(ProfileNodeData* total_data) : service_id_(0) {
    if (auto [svc, err] = z3y::TryGetDefaultService<IProfilerService>();
        err == z3y::InstanceError::kSuccess) {
      service_id_ = svc->GetInstanceId();
    }
    tls_state_ = GetThreadState();
    // 增加 128 层屏障，防止未入栈却被强行出栈
    if (!tls_state_ || !tls_state_->current_root ||
        tls_state_->stack_depth >= 128)
      return;

    AggregatorNode* parent =
        tls_state_->stack_depth > 0
            ? tls_state_->shadow_stack[tls_state_->stack_depth - 1]
            : tls_state_->current_root;
    total_node_ = FindOrCreateNode(total_data, parent);
    if (total_node_) {
      if (tls_state_->stack_depth < 128) {  // [新增]
        tls_state_->shadow_stack[tls_state_->stack_depth++] = total_node_;
      }
      start_total_ = std::chrono::steady_clock::now();
    }
  }

  void Next(ProfileNodeData* step_data) {
    // 如果总节点为空(被拦截)，或恰好达到栈顶，拒绝操作
    if (!total_node_ || tls_state_->stack_depth >= 128) return;
    if (auto [svc, err] = z3y::TryGetDefaultService<IProfilerService>();
        err != z3y::InstanceError::kSuccess ||
        svc->GetInstanceId() != service_id_)
      return;
    auto now = std::chrono::steady_clock::now();
    CloseStep(current_step_node_, start_step_, now);

    current_step_node_ = FindOrCreateNode(step_data, total_node_);
    if (current_step_node_) {
      if (tls_state_->stack_depth < 128) {  // [新增]
        tls_state_->shadow_stack[tls_state_->stack_depth++] =
            current_step_node_;
      }
      start_step_ = now;
    }
  }

  ~LinearManager() {
    if (!total_node_) return;
    if (auto [svc, err] = z3y::TryGetDefaultService<IProfilerService>();
        err != z3y::InstanceError::kSuccess ||
        svc->GetInstanceId() != service_id_)
      return;
    auto now = std::chrono::steady_clock::now();
    CloseStep(current_step_node_, start_step_, now);
    CloseStep(total_node_, start_total_, now);
  }

 private:
  inline void CloseStep(AggregatorNode* node,
                        std::chrono::steady_clock::time_point start_time,
                        std::chrono::steady_clock::time_point end_time) {
    if (!node) return;
    if (tls_state_->stack_depth > 0) {
      tls_state_->stack_depth--;
    }
    double ms = std::chrono::duration<double, std::milli>(end_time - start_time)
                    .count();
    node->call_count.fetch_add(1, std::memory_order_relaxed);
    uint64_t ns = static_cast<uint64_t>(ms * 1000000.0);
    node->total_time_ns.fetch_add(ns, std::memory_order_relaxed);
    AtomicUpdateMax(node->max_time_ms_bits, ms);
    AtomicUpdateMin(node->min_time_ms_bits, ms);
  }

  ProfilerThreadState* tls_state_ = nullptr;
  AggregatorNode* total_node_ = nullptr;
  AggregatorNode* current_step_node_ = nullptr;
  std::chrono::steady_clock::time_point start_total_, start_step_;
  uint64_t service_id_;
};

/**
 * @brief 直接记录一条度量指标的便捷函数（数值或单次事件）。
 */
inline void RecordMetric(ProfileNodeData* data, double value) {
  auto* tls = GetThreadState();
  if (!tls || !tls->current_root || tls->stack_depth == 0) return;
  AggregatorNode* parent = tls->shadow_stack[tls->stack_depth - 1];
  if (auto* node = FindOrCreateNode(data, parent)) {
    node->call_count.fetch_add(1, std::memory_order_relaxed);  // [修复]

    // [修复] 使用 CAS 原语进行无锁浮点数累加和极值更新
    AtomicAddDouble(node->sum_value_bits, value);
    AtomicUpdateMax(node->max_value_bits, value);
    AtomicUpdateMin(node->min_value_bits, value);
  }
}

/**
 * @brief 初始化局部的独立分析根节点。
 * @details 适用于独立的后台常驻线程，它的析构函数会直接将数据推送到 Logger（即
 * SubmitRootForCheck）。
 */
class ScopedRoot {
 public:
  ScopedRoot(ProfileNodeData* static_data, uint32_t period, double sla_ms)
      : period_(period), sla_ms_(sla_ms), service_id_(0) {
    if (auto [svc, err] = z3y::TryGetDefaultService<IProfilerService>();
        err == z3y::InstanceError::kSuccess) {
      service_id_ = svc->GetInstanceId();
    }
    tls_state_ = GetThreadState();
    if (!tls_state_) return;

    if (auto [svc, err] = z3y::TryGetDefaultService<IProfilerService>();
        err == z3y::InstanceError::kSuccess) {
      if (!svc->IsEnabled()) return;

      for (size_t i = 0; i < tls_state_->thread_root_count; ++i) {
        if (tls_state_->thread_roots[i]->static_info == static_data) {
          root_node_ = tls_state_->thread_roots[i];
          break;
        }
      }

      if (!root_node_) {
        root_node_ = svc->AcquireNode();
        if (root_node_) {
          root_node_->static_info = static_data;
          if (tls_state_->thread_root_count < 64) {
            tls_state_->thread_roots[tls_state_->thread_root_count++] =
                root_node_;
          }
        }
      }
    }

    if (root_node_) {
      tls_state_->current_root = root_node_;
      tls_state_->stack_depth = 1;
      tls_state_->shadow_stack[0] = root_node_;
      start_ts_ = std::chrono::steady_clock::now();
    }
  }

  ~ScopedRoot() {
    if (!root_node_ || !tls_state_) return;
    // [新增] 必须把服务存活性校验提到最前面！因为下面紧跟着就要解引用
    // root_node_
    if (auto [svc, err] = z3y::TryGetDefaultService<IProfilerService>();
        err != z3y::InstanceError::kSuccess ||
        svc->GetInstanceId() != service_id_) {
      return;
    }
    double ms = std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - start_ts_)
                    .count();
    uint64_t ns = static_cast<uint64_t>(ms * 1000000.0);
    root_node_->total_time_ns.fetch_add(ns, std::memory_order_relaxed);
    if (auto [svc, err] = z3y::TryGetDefaultService<IProfilerService>();
        err == z3y::InstanceError::kSuccess) {
      svc->SubmitRootForCheck(root_node_, period_, sla_ms_);
    }
    tls_state_->current_root = nullptr;
    tls_state_->stack_depth = 0;
  }

 private:
  AggregatorNode* root_node_ = nullptr;
  ProfilerThreadState* tls_state_ = nullptr;
  uint32_t period_;
  double sla_ms_;
  std::chrono::steady_clock::time_point start_ts_;
  uint64_t service_id_;
};
}  // namespace z3y::interfaces::profiler

/** 宏拼接工具定义 */
#define Z3Y_PROF_CAT_INNER(a, b) a##b
#define Z3Y_PROF_CAT(a, b) Z3Y_PROF_CAT_INNER(a, b)

/**
 * @def Z3Y_PROFILE
 * @brief 最常规的探针注入方式。自动将当前函数名称（__FUNCTION__）作为记录名字。
 * @details 将本行代码插入函数起始位置，通过 RAII 机制，函数退出时自动结算耗时。
 */
#define Z3Y_PROFILE()                                             \
  static z3y::interfaces::profiler::ProfileNodeData Z3Y_PROF_CAT( \
      s_n, __LINE__){__FUNCTION__, __FILE__, __LINE__,            \
                     z3y::interfaces::profiler::NodeType::Timer}; \
  z3y::interfaces::profiler::ScopedTimer Z3Y_PROF_CAT(            \
      _t, __LINE__)(&Z3Y_PROF_CAT(s_n, __LINE__))

/**
 * @def Z3Y_PROFILE_NAMED
 * @brief 对代码块进行自定义名称的探针注入。
 * * @details
 * 【极其核心的安全防线】
 * 这里强行使用了 `"" name ""` 的双重字面量包裹语法。
 * 目的是为了在物理编译期，彻底掐死任何尝试传入 `std::string`、`sprintf(char*)`
 * 等 动态临时指针的企图（它们会导致静默引发极其致命的悬空指针段错误崩溃）。
 */
#define Z3Y_PROFILE_NAMED(name)                                   \
  static z3y::interfaces::profiler::ProfileNodeData Z3Y_PROF_CAT( \
      s_n, __LINE__){"" name "", __FILE__, __LINE__,              \
                     z3y::interfaces::profiler::NodeType::Timer}; \
  z3y::interfaces::profiler::ScopedTimer Z3Y_PROF_CAT(            \
      _t, __LINE__)(&Z3Y_PROF_CAT(s_n, __LINE__))

/**
 * @def Z3Y_PROFILE_ROOT
 * @brief 注册一个独立树的根结点。常用于后台死循环 Worker 的外层，定期生成报告。
 */
#define Z3Y_PROFILE_ROOT(name, period, sla)                            \
  static z3y::interfaces::profiler::ProfileNodeData Z3Y_PROF_CAT(      \
      s_r, __LINE__){"" name "", __FILE__, __LINE__,                   \
                     z3y::interfaces::profiler::NodeType::Timer};      \
  z3y::interfaces::profiler::ScopedRoot Z3Y_PROF_CAT(_root, __LINE__)( \
      &Z3Y_PROF_CAT(s_r, __LINE__), period, sla)

/**
 * @def Z3Y_PROFILE_TAG
 * @brief
 * 将当前的上下文状态（如业务流水号、错误码）作为标示追加到当前统计节点上。
 */
#define Z3Y_PROFILE_TAG(k, v) z3y::interfaces::profiler::AddTagInternal(k, v)

/**
 * @def Z3Y_PROFILE_LINEAR
 * @brief 开启一个扁平的链式线性流程。
 */
#define Z3Y_PROFILE_LINEAR(name)                                    \
  static z3y::interfaces::profiler::ProfileNodeData Z3Y_PROF_CAT(   \
      s_ln, __LINE__){"" name "", __FILE__, __LINE__,               \
                      z3y::interfaces::profiler::NodeType::Linear}; \
  z3y::interfaces::profiler::LinearManager _z3y_linear_mgr(         \
      &Z3Y_PROF_CAT(s_ln, __LINE__))

/**
 * @def Z3Y_PROFILE_NEXT
 * @brief 线性流程向下推进一步。结束上一步统计，开启当前新统计。
 */
#define Z3Y_PROFILE_NEXT(name)                                     \
  static z3y::interfaces::profiler::ProfileNodeData Z3Y_PROF_CAT(  \
      s_nx, __LINE__){"" name "", __FILE__, __LINE__,              \
                      z3y::interfaces::profiler::NodeType::Timer}; \
  _z3y_linear_mgr.Next(&Z3Y_PROF_CAT(s_nx, __LINE__))

/**
 * @def Z3Y_PROFILE_VALUE
 * @brief 记录一项自定义数值量（如 CPU
 * 温度、丢帧数量）。底层会对该数值自动求平均和极值。
 */
#define Z3Y_PROFILE_VALUE(name, val)                                        \
  {                                                                         \
    static z3y::interfaces::profiler::ProfileNodeData Z3Y_PROF_CAT(         \
        s_val, __LINE__){"" name "", __FILE__, __LINE__,                    \
                         z3y::interfaces::profiler::NodeType::Value};       \
    z3y::interfaces::profiler::RecordMetric(&Z3Y_PROF_CAT(s_val, __LINE__), \
                                            static_cast<double>(val));      \
  }

/**
 * @def Z3Y_PROFILE_EVENT
 * @brief 记录单次触发性事件的发生（类似于埋点）。在报告中仅累加触发次数。
 */
#define Z3Y_PROFILE_EVENT(name)                                             \
  {                                                                         \
    static z3y::interfaces::profiler::ProfileNodeData Z3Y_PROF_CAT(         \
        s_evt, __LINE__){"" name "", __FILE__, __LINE__,                    \
                         z3y::interfaces::profiler::NodeType::Event};       \
    z3y::interfaces::profiler::RecordMetric(&Z3Y_PROF_CAT(s_evt, __LINE__), \
                                            1.0);                           \
  }

/**
 * @def Z3Y_PROFILE_ASYNC_BEGIN
 * @brief 【异步多线程调度器使用】在一组跨线程处理流程之初创建槽位并绑定。
 */
#define Z3Y_PROFILE_ASYNC_BEGIN(name, id, period, sla)                  \
  if (auto [svc, err] = z3y::TryGetDefaultService<                      \
          z3y::interfaces::profiler::IProfilerService>();               \
      err == z3y::InstanceError::kSuccess) {                            \
    if (svc->IsEnabled()) svc->AsyncBegin("" name "", id, period, sla); \
  }

/**
 * @def Z3Y_PROFILE_ASYNC_ATTACH
 * @brief 【异步 Worker 线程使用】将所在线程当下的性能剖析栈“挂靠”回指定的
 * Frame_ID 主分析槽位上。
 */
#define Z3Y_PROFILE_ASYNC_ATTACH(id)                      \
  if (auto [svc, err] = z3y::TryGetDefaultService<        \
          z3y::interfaces::profiler::IProfilerService>(); \
      err == z3y::InstanceError::kSuccess) {              \
    if (svc->IsEnabled()) svc->AsyncAttach(id);           \
  }

/**
 * @def Z3Y_PROFILE_ASYNC_COMMIT
 * @brief
 * 【异步生命周期收尾处使用】当这一帧的完整处理已经结束，结算并提交报告，同时腾出槽位。
 */
#define Z3Y_PROFILE_ASYNC_COMMIT(id)                      \
  if (auto [svc, err] = z3y::TryGetDefaultService<        \
          z3y::interfaces::profiler::IProfilerService>(); \
      err == z3y::InstanceError::kSuccess) {              \
    if (svc->IsEnabled()) svc->AsyncCommit(id);           \
  }