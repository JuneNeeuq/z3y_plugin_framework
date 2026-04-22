/**
 * @file profiler_service.h
 * @brief 性能分析插件的具体实现类声明。
 * * @details
 * 【面向维护者】
 * 这里封装了 Profiler 的核心引擎。包括：
 * 1. 中央节点内存池 (master_nodes_, free_nodes_)
 * 2. SLA 校验和报表生成器
 * 3. 插件生命周期拦截器 (is_active_)
 * 在设计上严格遵守了无感拦截、零开销内存复用、以及防插件重载崩溃的工业级要求。
 */

#pragma once
#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

#include "framework/connection.h"
#include "framework/z3y_define_impl.h"
#include "interfaces_core/i_config_service.h"
#include "interfaces_core/i_log_service.h"
#include "interfaces_profiler/i_profiler_service.h"

namespace z3y::plugins::profiler {

/**
 * @brief IProfilerService 的默认实现类。
 */
class ProfilerService
    : public z3y::PluginImpl<ProfilerService,
                             z3y::interfaces::profiler::IProfilerService> {
 public:
  Z3Y_DEFINE_COMPONENT_ID("z3y-core-ProfilerService-Impl-v2");

  ProfilerService();
  ~ProfilerService() override { Shutdown(); }

  void Initialize() override;
  void Shutdown() override;

  bool IsEnabled() const override;

  uint64_t GetInstanceId() const override { return instance_id_; }  // [新增]

  // [新增] 用于给 TLS 回收资源的方法
  void ReturnTlsNodesToGlobal(
      const std::vector<z3y::interfaces::profiler::AggregatorNode*>& nodes);

  z3y::interfaces::profiler::ProfilerThreadState* GetOrCreateThreadState()
      override;
  z3y::interfaces::profiler::AggregatorNode* AcquireNode() override;
  void ReleaseNodeTree(
      z3y::interfaces::profiler::AggregatorNode* node) override;
  void SubmitRootForCheck(z3y::interfaces::profiler::AggregatorNode* root,
                          uint32_t period, double sla_ms) override;

  void AsyncBegin(const char* name, uint64_t frame_id, uint32_t period,
                  double sla_ms) override;
  void AsyncAttach(uint64_t frame_id) override;
  void AsyncCommit(uint64_t frame_id) override;

 private:
  /**
   * @brief 将树状节点数据格式化为字符串报表并写入 Logger。
   */
  void GenerateReportAndLog(z3y::interfaces::profiler::AggregatorNode* root,
                            const std::string& reason);
  /**
   * @brief 递归格式化耗时类型的节点。
   */
  void FormatTimerRecursive(std::string& output,
                            z3y::interfaces::profiler::AggregatorNode* node,
                            int depth, double parent_ms);
  /**
   * @brief 递归格式化数值类型的节点。
   */
  void FormatMetricsRecursive(std::string& output,
                              z3y::interfaces::profiler::AggregatorNode* node);
  /**
   * @brief 安全地断开并释放某节点的所有子节点。
   */
  void ReleaseChildren(z3y::interfaces::profiler::AggregatorNode* parent);

  // 【生命周期安全防线】用于在插件即将卸载时，拦截任何延后的延迟析构和内存访问，防段错误。
  std::atomic<bool> is_active_{false};

  std::atomic<bool> enable_{true};  ///< 总控开关，受 ConfigService 监听控制
  std::atomic<double> default_sla_ms_{0.0};  ///< 全局默认 SLA 阈值

  z3y::PluginPtr<z3y::interfaces::core::ILogger>
      profiler_logger_;                                  ///< 日志组件接口句柄
  z3y::interfaces::core::ConnectionGroup config_conns_;  ///< 配置变更监听连接组

  std::mutex alloc_mutex_;  ///< 全局中央内存池互斥锁（由于有 TLS
                            ///< 局部池，此锁极少发生碰撞）
  std::vector<std::unique_ptr<z3y::interfaces::profiler::AggregatorNode>>
      master_nodes_;  ///< 唯一持有所有节点所有权的容器
  std::vector<z3y::interfaces::profiler::AggregatorNode*>
      free_nodes_;  ///< 全局中央空闲节点池 (FreeList)
  static std::atomic<uint64_t> g_instance_counter_;  // [新增] 全局计数器
  uint64_t instance_id_;                             // [新增] 本实例 ID
};

}  // namespace z3y::plugins::profiler