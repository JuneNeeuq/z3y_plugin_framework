/**
 * @file profiler_types.h
 * @brief 性能分析插件的基础数据结构定义。
 * * @details
 * 【面向使用者】
 * 这里定义了性能分析器底层所使用的数据结构，普通使用者通常不需要直接操作这些结构体，
 * 而是通过 profiler_macros.h 中的宏（如 Z3Y_PROFILE）来隐式使用它们。
 * * 【面向维护者】
 * 本文件是 Profiler 性能极客优化的核心。为了满足 7x24
 * 小时工业检测流水线的苛刻要求， 我们在数据结构上进行了极端的内存布局优化（如
 * alignas(64) 消除伪共享）以及
 * 左孩子右兄弟（LCRS）树的设计，以突破节点数量限制并提高 CPU L1 Cache 命中率。
 */

#pragma once
#include <cstring>
#include <array>
#include <atomic>
#include <cstdint>

namespace z3y::interfaces::profiler {
// [新增] 辅助原语：无锁双精度浮点数 CAS 更新最大值
inline void AtomicUpdateMax(std::atomic<uint64_t>& target, double val) {
  uint64_t val_bits;
  std::memcpy(&val_bits, &val, sizeof(double));
  uint64_t prev_bits = target.load(std::memory_order_relaxed);
  while (true) {
    double prev;
    std::memcpy(&prev, &prev_bits, sizeof(double));
    if (val <= prev) break;  // 如果当前值不大于历史最大值，放弃更新
    if (target.compare_exchange_weak(prev_bits, val_bits,
                                     std::memory_order_relaxed))
      break;
  }
}

// [新增] 辅助原语：无锁双精度浮点数 CAS 更新最小值
inline void AtomicUpdateMin(std::atomic<uint64_t>& target, double val) {
  uint64_t val_bits;
  std::memcpy(&val_bits, &val, sizeof(double));
  uint64_t prev_bits = target.load(std::memory_order_relaxed);
  while (true) {
    double prev;
    std::memcpy(&prev, &prev_bits, sizeof(double));
    if (val >= prev) break;
    if (target.compare_exchange_weak(prev_bits, val_bits,
                                     std::memory_order_relaxed))
      break;
  }
}

inline void AtomicAddDouble(std::atomic<uint64_t>& target, double val) {
  uint64_t prev_bits = target.load(std::memory_order_relaxed);
  while (true) {
    double prev;
    std::memcpy(&prev, &prev_bits, sizeof(double));
    double next = prev + val;
    uint64_t next_bits;
    std::memcpy(&next_bits, &next, sizeof(double));
    if (target.compare_exchange_weak(prev_bits, next_bits,
                                     std::memory_order_relaxed)) {
      break;
    }
  }
}

/**
 * @brief 性能分析节点的类型枚举。
 * * @details
 * 【面向维护者】
 * 用于在生成报告时区分该节点是普通耗时、单次事件、数值记录还是线性流程。
 * 报表生成器会根据不同类型采用不同的格式化输出逻辑。
 */
enum class NodeType {
  Timer,  ///< 耗时定时器类型，记录调用次数、总耗时、最大/最小耗时。
  Value,  ///< 动态数值类型，记录自定义数值的累加、最大/最小值。
  Event,  ///< 离散事件类型，仅记录发生次数。
  Linear  ///< 线性流程类型，用于 Z3Y_PROFILE_LINEAR，记录分步流程的总耗时。
};

/**
 * @brief 静态节点元数据。
 * * @details
 * 【面向维护者】
 * 这是一个在编译期或首次执行时初始化的静态结构。它的生命周期与宿主程序一致。
 * 将静态信息（名字、行号、文件）与动态指标（耗时、次数）分离，是为了在多线程
 * 并发拷贝和查找时，减少需要对比的内存大小，实现 O(1) 级别的静态地址指针匹配。
 */
struct ProfileNodeData {
  const char* name;  ///< 节点名称（强制要求为字符串字面量常量，防悬空指针）
  const char* file;  ///< 节点所在源文件路径（由 __FILE__ 宏自动生成）
  uint32_t line;     ///< 节点所在代码行号（由 __LINE__ 宏自动生成）
  NodeType type;     ///< 节点的类型标识
  std::atomic<bool> enabled{
      true};  ///< 动态启停开关，允许在运行时通过配置动态关闭某个特定的探针
};

/**
 * @brief 动态上下文标签数据。
 * * @details
 * 【面向使用者】
 * 当你需要记录一个业务流水号（如 "Barcode" : "12345"）时使用。
 * 注意：Key 必须是静态字符串字面量；Value
 * 允许是动态字符串，底层会进行安全的深拷贝。
 * * 【面向维护者】
 * value 数组设定为 32 字节。这是为了在不分配堆内存（不使用
 * std::string）的情况下， 能够容纳绝大多数工业场景的序列号或异常状态码。避免
 * malloc 是零开销 Profiler 的底线。
 */
struct TagData {
  const char* key = nullptr;  ///< 标签的键（静态指针）
  char value[32] = {0};  ///< 标签的值（预分配栈上内存，安全深拷贝，防止 UAF）
};

/**
 * @brief 核心聚合节点结构体（LCRS 多叉树节点）。
 * * @details
 * 【面向维护者 - 架构与优化揭秘】
 * 1. **消除伪共享 (False Sharing)**：采用 `alignas(64)` 强制按 CPU Cache Line
 * 对齐。 在多线程高并发写操作时，防止相邻节点的更新触发 MESI
 * 协议导致缓存失效，引发性能雪崩。
 * 2. **LCRS 多叉树**：放弃了 `std::vector` 或静态数组
 * `children[32]`，采用左孩子(`first_child`)
 * 右兄弟(`next_sibling`)的设计。这不仅打破了子节点数量的硬限制，还大幅压缩了结构体体积。
 * 3. **微型自旋锁**：`lock` 用于在多线程向同一个 AsyncRoot
 * 挂载子节点时，保护树的拓扑结构不被撕裂。
 */
struct alignas(64) AggregatorNode {
  // === L1 Cache Line 1: 拓扑结构与生命周期 (极少修改) ===
  ProfileNodeData* static_info = nullptr;  ///< 指向该节点静态元数据的指针
  AggregatorNode* parent = nullptr;        ///< 指向父节点的指针

  AggregatorNode* first_child = nullptr;   ///< 左孩子指针（LCRS 结构）
  AggregatorNode* next_sibling = nullptr;  ///< 右兄弟指针（LCRS 结构）

  std::atomic_flag lock =
      ATOMIC_FLAG_INIT;  ///< 极轻量级自旋锁，保护当前节点的并发拓扑修改

  // === L1 Cache Line 2: 高频并发指标 (完全原子化) ===
  alignas(64) std::atomic<uint64_t> call_count{0};  ///< 累计被调用的次数
  std::atomic<uint64_t> total_time_ns{0};  ///< 累计耗时纳秒，允许使用原生硬件 fetch_add

  std::atomic<uint64_t> max_time_ms_bits{0}; ///< 历史单次最大耗时
  std::atomic<uint64_t> min_time_ms_bits{0}; ///< 历史单次最小耗时

  std::atomic<uint64_t> sum_value_bits{0}; ///< 数值型节点的累加值
  std::atomic<uint64_t> max_value_bits{0}; ///< 数值型节点的历史最大值
  std::atomic<uint64_t> min_value_bits{0}; ///< 数值型节点的历史最小值

  std::array<TagData, 2>
      tags{};  ///< 绑定的上下文标签数组（硬上限 2 个，避免结构体过大）
  std::atomic<size_t> tag_count{0};  ///< 当前已绑定的标签数量

  // [新增] 供业务层安全读取的接口
  double GetTotalTimeMs() const {
    return total_time_ns.load(std::memory_order_relaxed) / 1000000.0;
  }

  double GetMaxTimeMs() const {
    double val;
    uint64_t bits = max_time_ms_bits.load(std::memory_order_relaxed);
    std::memcpy(&val, &bits, sizeof(double));
    return val;
  }

  double GetMinTimeMs() const {
    double val;
    uint64_t bits = min_time_ms_bits.load(std::memory_order_relaxed);
    std::memcpy(&val, &bits, sizeof(double));
    return val;
  }

  double GetSumValue() const {
    double val;
    uint64_t bits = sum_value_bits.load(std::memory_order_relaxed);
    std::memcpy(&val, &bits, sizeof(double));
    return val;
  }

  double GetMaxValue() const {
    double val;
    uint64_t bits = max_value_bits.load(std::memory_order_relaxed);
    std::memcpy(&val, &bits, sizeof(double));
    return val;
  }

  double GetMinValue() const {
    double val;
    uint64_t bits = min_value_bits.load(std::memory_order_relaxed);
    std::memcpy(&val, &bits, sizeof(double));
    return val;
  }

  /**
   * @brief 重置节点内的所有动态统计数据。
   * * @details
   * 【面向维护者】
   * 在对象池回收该节点，或异步流生命周期结束准备复用该节点时调用。
   * 注意：此函数绝不能去清空 `first_child` 等树形拓扑指针，拓扑指针的清理必须由
   * Service 严格带锁执行。
   */
  void Reset() {
    call_count.store(0, std::memory_order_relaxed);
    total_time_ns.store(0, std::memory_order_relaxed);

    double zero = 0.0, max_init = 999999.0, min_init = -999999.0;
    uint64_t zero_bits, max_init_bits, min_init_bits;
    std::memcpy(&zero_bits, &zero, sizeof(double));
    std::memcpy(&max_init_bits, &max_init, sizeof(double));
    std::memcpy(&min_init_bits, &min_init, sizeof(double));

    max_time_ms_bits.store(zero_bits, std::memory_order_relaxed);
    min_time_ms_bits.store(max_init_bits, std::memory_order_relaxed);
    sum_value_bits.store(zero_bits, std::memory_order_relaxed);
    max_value_bits.store(min_init_bits, std::memory_order_relaxed);
    min_value_bits.store(max_init_bits, std::memory_order_relaxed);
    tag_count.store(0, std::memory_order_relaxed);
  }
};

/**
 * @brief 线程局部缓存状态结构体。
 * * @details
 * 【面向维护者】
 * 每个工作线程（Worker Thread）独享一个该结构体实例。
 * 它的核心作用是维护一个 **Shadow Stack（影子调用栈）**，用于在 O(1) 的时间内，
 * 知道当前探针的“父节点”是谁，从而正确地将自己挂载到多叉树中。
 * 完全无锁化是实现零开销记录的核心保障。
 */
struct ProfilerThreadState {
  AggregatorNode*
      shadow_stack[128]{};  ///< 影子栈，记录当前函数调用的嵌套层级（硬上限
                            ///< 128 层深）
  size_t stack_depth = 0;   ///< 当前影子栈的深度
  AggregatorNode* current_root =
      nullptr;  ///< 当前线程所属的根节点（如关联的 Async Slot 根节点）

  AggregatorNode* thread_roots[64]{};  ///< 当前线程持有的独立根节点集合（用于
                                       ///< Z3Y_PROFILE_ROOT）
  size_t thread_root_count = 0;        ///< 当前线程独立根节点的数量
};

}  // namespace z3y::interfaces::profiler