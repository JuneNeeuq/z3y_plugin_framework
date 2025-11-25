/*
* Copyright [2025] [Yue Liu]
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
* http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*/

#pragma once
#include "framework/z3y_define_impl.h"
#include "interfaces_profiler/i_profiler_service.h"
#include "interfaces_core/i_log_service.h"
#include "interfaces_core/i_config_service.h"

#include <nlohmann/json.hpp>
#include <fstream>
#include <mutex>
#include <shared_mutex> 
#include <vector>
#include <string>
#include <atomic>
#include <thread>
#include <queue>
#include <condition_variable>
#include <filesystem>

namespace z3y::plugins::profiler {

    /**
     * @struct ProfilerRule
     * @brief 性能监控规则配置。
     * @details 允许针对不同的模块 (Matcher) 设置不同的报警阈值。
     */
    struct ProfilerRule {
        std::string matcher; //!< 匹配器字符串 (前缀匹配)
        double threshold_ms; //!< 报警阈值 (毫秒)

        // JSON 序列化绑定
        NLOHMANN_DEFINE_TYPE_INTRUSIVE(ProfilerRule, matcher, threshold_ms);
    };

    /**
     * @struct ProfilerConfig
     * @brief 服务的整体配置结构。
     * @details 支持从 JSON 配置文件加载，支持热更新。
     */
    struct ProfilerConfig {
        double alert_threshold_ms = 30.0; //!< 默认报警阈值
        bool global_enable = true;        //!< 全局总开关
        bool enable_console_log = false;  //!< 是否同时输出到控制台 (开发用)
        bool enable_tracing = false;      //!< 是否开启 Chrome Tracing 文件输出
        std::string trace_file = "logs/trace.json"; //!< Trace 文件路径
        size_t max_trace_file_mb = 500;   //!< Trace 文件最大大小 (MB)
        size_t max_queue_size = 2048;     //!< 异步队列最大深度 (块数)
        std::vector<ProfilerRule> rules;  //!< 自定义规则列表

        /**
         * @brief 校验配置合法性。
         * @return 始终返回 true (目前暂无严格校验逻辑)。
         */
        bool Validate(std::string&) { return true; }

        // JSON 序列化绑定 (带默认值)
        NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(ProfilerConfig,
            alert_threshold_ms, global_enable, enable_console_log,
            enable_tracing, trace_file, max_trace_file_mb, max_queue_size, rules);
    };

    /**
     * @typedef TraceBufferChunk
     * @brief 追踪数据缓冲区块。
     * @details 使用 vector<char> 预分配内存，减少 string 构造开销，提高吞吐量。
     */
    using TraceBufferChunk = std::vector<char>;

    /**
     * @class ProfilerService
     * @brief 性能分析服务具体实现类。
     *
     * @details
     * [设计思想 - 高性能]
     * 1. **RCU (Read-Copy-Update)**: 使用 `std::shared_ptr<const Config>` 和 `atomic_load`
     * 实现配置的无锁读取，确保热重载时不会阻塞高频的 `RecordTime` 调用。
     * 2. **TLS Buffer**: 每个线程拥有独立的缓冲区，满后批量提交，大幅减少锁竞争。
     * 3. **原子开关**: `global_enable_cache_` 提供纳秒级的全局开关检查。
     * 4. **内存池**: `free_pool_` 复用 `TraceBufferChunk`，避免频繁的内存分配与释放。
     *
     * [设计思想 - 高可靠]
     * 1. **原子配置更新**: 保证配置切换的原子性，不会出现读到“半个配置”的情况。
     * 2. **写入线程分离**: 文件 IO 操作完全在独立线程 (`writer_thread_`) 执行，不阻塞业务。
     * 3. **队列溢出保护**: 当生产速度远超消费速度时，主动丢弃数据并统计，防止内存耗尽。
     * 4. **中文路径支持**: 内置 `Utf8ToPath`，解决 Windows 下中文路径乱码问题。
     */
    class ProfilerService : public PluginImpl<ProfilerService, z3y::interfaces::profiler::IProfilerService> {
    public:
        //! 组件唯一 ID
        Z3Y_DEFINE_COMPONENT_ID("z3y-core-ProfilerService-Impl-UUID-P001");

        ProfilerService();
        ~ProfilerService() override;

        // --- 生命周期钩子 ---
        void Initialize() override;
        void Shutdown() override;

        // --- IProfilerService 接口实现 ---

        /**
         * @brief 记录耗时 (接口实现)。
         * @details
         * 1. 原子检查全局开关。
         * 2. 如果开启 Tracing，将事件写入 TLS 缓冲区。
         * 3. 检查阈值，如果超时，向 Logger 写入警告日志。
         * @note 这是一个热路径函数 (Hot Path)，实现必须极度高效。
         */
        void RecordTime(z3y::PluginPtr<z3y::interfaces::core::ILogger> logger, const char* category, const char* name, const char* payload, z3y::interfaces::profiler::PayloadType payload_type, const z3y::interfaces::profiler::SourceLocation& loc, double ms) noexcept override;

        void RecordCounter(const char* category, const char* name, double value) noexcept override;
        void SetThreadName(const char* name) noexcept override;
        void MarkFrame(const char* name) noexcept override;
        void RecordFlow(const char* name, uint64_t flow_id, z3y::interfaces::profiler::FlowType type) noexcept override;

        /**
         * @brief 检查是否启用 (高性能实现)。
         * @return 直接读取 atomic 缓存，无锁。
         */
        bool IsEnabled() const noexcept override { return global_enable_cache_.load(std::memory_order_relaxed); }

        void FlushCurrentThread() noexcept override;

        // --- 内部机制 (供 ThreadBuffer 调用) ---

        /**
         * @brief 提交一个填满的数据块到全局队列。
         * @param chunk 移动语义传递的数据块。
         */
        void SubmitChunk(TraceBufferChunk&& chunk);

        /**
         * @brief 从内存池申请一个空闲的数据块。
         * @return 空闲块，若池为空则分配新块。
         */
        TraceBufferChunk AcquireBuffer();

        /**
         * @brief 获取当前服务实例的唯一 ID。
         * @details 用于 ThreadBuffer 校验绑定的服务是否已失效 (防止访问已卸载的 DLL)。
         */
        uint64_t GetInstanceId() const { return instance_id_; }

    private:
        /**
         * @brief 初始化 Tracing 系统 (打开文件，启动写线程)。
         */
        void InitTracing();

        /**
         * @brief 关闭 Tracing 系统 (Flush 数据，关闭文件，停止线程)。
         */
        void CloseTracing();

        /**
         * @brief 写入线程的主循环函数。
         */
        void FileWriterLoop();

        /**
         * @brief 响应配置热重载事件。
         * @param e 配置重载事件。
         */
        void OnConfigReloaded(const z3y::interfaces::core::ConfigurationReloadedEvent& e);

        // --- 内部 Trace 辅助函数 ---
        // 将各类事件序列化为 JSON 格式并写入 TLS 缓冲区
        void TraceEventToBuffer(const char* cat, const char* name, double ms, size_t tid, const char* payload, z3y::interfaces::profiler::PayloadType p_type);
        void TraceCounterToBuffer(const char* cat, const char* name, double value, size_t tid);
        void TraceMetadataToBuffer(const char* name, size_t tid);
        void TraceInstantToBuffer(const char* name, char scope, size_t tid);
        void TraceFlowToBuffer(const char* name, uint64_t flow_id, char phase, size_t tid);

        /**
         * @brief JSON 字符串转义辅助函数。
         * @details 将输入字符串中的特殊字符转义，写入输出迭代器。
         */
        template <typename OutputIt>
        static void EscapeJsonTo(OutputIt out, const char* input);

        // --- 成员变量 ---

        // [RCU 配置管理]
        // 使用 shared_ptr 实现配置的原子替换，读操作无锁。
        // mutex 仅用于写操作 (Reload)。
        std::shared_ptr<const ProfilerConfig> config_ptr_;
        std::mutex config_update_mutex_;

        // [原子缓存] 
        // 用于极速判断是否需要记录，避免访问 config_ptr_
        std::atomic<bool> global_enable_cache_{ true };
        std::atomic<size_t> cached_max_queue_size_{ 2048 };

        // 配置变更事件的连接句柄
        z3y::ScopedConnection config_conn_;

        // [日志服务]
        // 弱依赖，初始化时获取。RecordTime 中需检查是否有效。
        z3y::PluginPtr<z3y::interfaces::core::ILogManagerService> log_service_;
        std::mutex log_service_write_mutex_; // 保护 log_service_ 的赋值/重置

        // [Tracing IO]
        std::ofstream trace_ofs_;       //!< Trace 文件流
        size_t current_file_size_ = 0;  //!< 当前文件大小统计
        bool file_full_ = false;        //!< 文件是否已满标记

        std::thread writer_thread_;     //!< 独立写入线程
        std::mutex queue_mutex_;        //!< 保护 chunk_queue_ 和 free_pool_
        std::condition_variable queue_cv_; //!< 用于唤醒写入线程

        std::queue<TraceBufferChunk> chunk_queue_; //!< 待写入的数据块队列
        std::vector<TraceBufferChunk> free_pool_;  //!< 空闲数据块内存池

        // [状态位]
        std::atomic<bool> writer_running_{ false }; //!< 写入线程是否运行中
        std::atomic<bool> accepting_data_{ false }; //!< 是否接受新数据 (Shutdown 时置 false)
        std::atomic<size_t> dropped_chunks_{ 0 };   //!< 因队列满而丢弃的块数统计

        // [实例标识]
        uint64_t instance_id_ = 0; //!< 当前实例 ID
        static std::atomic<uint64_t> s_instance_counter_; //!< 全局递增计数器
    };
}