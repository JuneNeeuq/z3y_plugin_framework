/**
 * @file i_profiler_service.h
 * @brief 性能分析插件的纯虚接口定义。
 * * @details
 * 【面向使用者】
 * 除非你需要编写自定义的报表导出工具，否则你不应该直接调用此接口。
 * 业务代码请始终使用 profiler_macros.h 中的宏。
 * * 【面向维护者】
 * 遵循 z3y_plugin_framework 规范，继承 IComponent。
 * 接口设计极为精简，核心就是分配节点、回收节点、以及提交根节点进行 SLA 校验。
 */

#pragma once
#include "framework/i_component.h"
#include "framework/z3y_define_interface.h"
#include "profiler_types.h"

namespace z3y::interfaces::profiler {

/**
 * @brief Profiler 插件服务对外提供的接口规范。
 */
class IProfilerService : public virtual z3y::IComponent {
 public:
  Z3Y_DEFINE_INTERFACE(IProfilerService, "z3y-core-IProfilerService-v2", 2, 0);

  /**
   * @brief 检查当前性能分析器是否处于启用状态。
   * @return true 启用，false 禁用。
   */
  virtual bool IsEnabled() const = 0;

  virtual uint64_t GetInstanceId() const = 0;

  /**
   * @brief 获取当前线程的 Profiler 缓存状态，如果不存在则自动创建。
   * @return 指向当前线程独占的 ProfilerThreadState 的指针。
   */
  virtual ProfilerThreadState* GetOrCreateThreadState() = 0;

  /**
   * @brief 向中央内存池申请一个新的空闲聚合节点。
   * @details
   * 【面向维护者】
   * 必须保证分配过程是线程安全的。在内部实现中，这通常会触发 TLS
   * 本地池到全局池的交互。
   * @return 空闲节点的指针。如果触发了硬上限保护，可能返回 nullptr。
   */
  virtual AggregatorNode* AcquireNode() = 0;

  /**
   * @brief 深度释放一棵多叉子树，将其归还给内存池。
   * @param node 需要释放的子树根节点。
   * @details
   * 【面向维护者】
   * 此函数会将传入的节点及其所有子节点递归地放入 FreeList
   * 对象池，实现无缝垃圾回收 (GC)。
   */
  virtual void ReleaseNodeTree(AggregatorNode* node) = 0;

  /**
   * @brief 提交一个根节点，供 Service 后台校验 SLA (Service Level Agreement)
   * 及输出报表。
   * @param root 要检查的根节点。
   * @param period 周期性输出的次数阈值。
   * @param sla_ms 最大容忍耗时（超时将触发强力警告日志）。
   */
  virtual void SubmitRootForCheck(AggregatorNode* root, uint32_t period,
                                  double sla_ms) = 0;

  /**
   * @brief 开启一个全异步跨线程的分析流。
   * @param name 异步流的名称。
   * @param frame_id 唯一的帧 ID 或流水号，用于后续的 Attach 和 Commit 关联。
   * @param period 日志输出周期。
   * @param sla_ms SLA 容忍度。
   */
  virtual void AsyncBegin(const char* name, uint64_t frame_id, uint32_t period,
                          double sla_ms) = 0;

  /**
   * @brief 工作线程将自身挂载到指定的异步流中。
   * @param frame_id 需要挂载的帧 ID。
   */
  virtual void AsyncAttach(uint64_t frame_id) = 0;

  /**
   * @brief 结束一个异步流，计算总耗时并生成报告。
   * @param frame_id 需要结束的帧 ID。
   */
  virtual void AsyncCommit(uint64_t frame_id) = 0;
};

}  // namespace z3y::interfaces::profiler