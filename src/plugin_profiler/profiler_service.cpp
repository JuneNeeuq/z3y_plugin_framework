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

#include "profiler_service.h"
#include "interfaces_core/z3y_log_macros.h"

#include <iostream>
#include <thread>
#include <filesystem>
#include <vector>
#include <algorithm>

#ifdef _WIN32
#include <Windows.h>
#endif

 // 自动注册 ProfilerService 为默认服务
Z3Y_AUTO_REGISTER_SERVICE(z3y::plugins::profiler::ProfilerService, "System.Profiler", true);

namespace z3y::plugins::profiler {

    using namespace z3y::interfaces::core;
    using namespace z3y::interfaces::profiler;

    // 初始化全局实例计数器
    std::atomic<uint64_t> ProfilerService::s_instance_counter_{ 1 };
    // 单个 Trace Buffer 的大小阈值 (字节)，超过此值将提交到全局队列
    static const size_t kTraceBufferThreshold = 4096;

    // -----------------------------------------------------------------------------
    // [内部类] ThreadBuffer (线程局部存储)
    // -----------------------------------------------------------------------------
    /**
     * @class ThreadBuffer
     * @brief 线程独享的 Trace 数据缓冲区。
     */
    class ThreadBuffer {
    public:
        void BindService(std::weak_ptr<ProfilerService> service_weak, uint64_t instance_id) {
            service_weak_ = service_weak;
            bound_instance_id_ = instance_id;
            EnsureBuffer();
        }

        bool IsBoundTo(uint64_t current_instance_id) const { return bound_instance_id_ == current_instance_id; }

        size_t GetCachedTid() {
            if (cached_tid_ == 0) {
                cached_tid_ = std::hash<std::thread::id>{}(std::this_thread::get_id());
            }
            return cached_tid_;
        }

        ~ThreadBuffer() {
            Flush();
        }

        auto GetInserter() { return std::back_inserter(buffer_); }

        void CommitWrite() {
            buffer_.push_back(','); buffer_.push_back('\n');
            if (buffer_.size() >= kTraceBufferThreshold) Flush();
        }

        void Flush() {
            if (buffer_.empty()) return;
            if (auto service = service_weak_.lock()) {
                if (service->GetInstanceId() == bound_instance_id_) {
                    service->SubmitChunk(std::move(buffer_));
                }
            }
            buffer_.clear();
            EnsureBuffer();
        }
    private:
        void EnsureBuffer() {
            if (buffer_.capacity() < kTraceBufferThreshold) {
                if (auto service = service_weak_.lock()) {
                    if (service->GetInstanceId() == bound_instance_id_) {
                        buffer_ = service->AcquireBuffer();
                    }
                }
                if (buffer_.capacity() < kTraceBufferThreshold) buffer_.reserve(kTraceBufferThreshold * 2);
            }
        }

        std::weak_ptr<ProfilerService> service_weak_;
        uint64_t bound_instance_id_ = 0;
        TraceBufferChunk buffer_;
        size_t cached_tid_ = 0;
    };

    static thread_local ThreadBuffer t_trace_buffer;

    // -----------------------------------------------------------------------------
    // [实现] ProfilerService
    // -----------------------------------------------------------------------------

    ProfilerService::ProfilerService() {
        instance_id_ = s_instance_counter_.fetch_add(1);
    }

    ProfilerService::~ProfilerService() {
        Shutdown();
    }

    void ProfilerService::Initialize() {
        auto mutable_config = std::make_shared<ProfilerConfig>();

        if (auto [cfg_svc, err] = z3y::TryGetDefaultService<IConfigManagerService>(); err == InstanceError::kSuccess) {
            cfg_svc->LoadConfig("profiler_config", "/settings", *mutable_config);
        }

        global_enable_cache_.store(mutable_config->global_enable);
        cached_max_queue_size_.store(mutable_config->max_queue_size);

        std::atomic_store(&config_ptr_, std::shared_ptr<const ProfilerConfig>(mutable_config));

        {
            std::lock_guard<std::mutex> lock(log_service_write_mutex_);
            if (auto [log_mgr, err] = z3y::TryGetDefaultService<ILogManagerService>(); err == InstanceError::kSuccess) {
                log_service_ = log_mgr;
            }
        }

        if (auto [bus, err] = z3y::TryGetService<IEventBus>(clsid::kEventBus); err == InstanceError::kSuccess) {
            config_conn_ = bus->SubscribeGlobal<ConfigurationReloadedEvent>(
                shared_from_this(), &ProfilerService::OnConfigReloaded
            );
        }

        auto cfg = std::atomic_load(&config_ptr_);
        if (cfg->enable_tracing) InitTracing();
    }

    void ProfilerService::Shutdown() {
        accepting_data_ = false;
        CloseTracing();
        {
            std::lock_guard<std::mutex> lock(log_service_write_mutex_);
            log_service_.reset();
        }
        std::lock_guard<std::mutex> q_lock(queue_mutex_);
        free_pool_.clear();
    }

    void ProfilerService::OnConfigReloaded(const ConfigurationReloadedEvent& e) {
        if (e.domain != "profiler_config") return;

        auto mutable_config = std::make_shared<ProfilerConfig>();

        if (auto [cfg_svc, err] = z3y::TryGetDefaultService<IConfigManagerService>(); err == InstanceError::kSuccess) {
            if (cfg_svc->LoadConfig("profiler_config", "/settings", *mutable_config) == ConfigStatus::Success) {

                std::lock_guard<std::mutex> lock(config_update_mutex_);
                auto old_config = std::atomic_load(&config_ptr_);

                // 更新原子缓存
                global_enable_cache_.store(mutable_config->global_enable);
                cached_max_queue_size_.store(mutable_config->max_queue_size);

                // 状态比对
                bool tracing_was_on = old_config ? old_config->enable_tracing : false;
                bool tracing_is_on = mutable_config->enable_tracing;

                // [关键修复] 检查文件名是否变更
                bool filename_changed = false;
                if (old_config && old_config->trace_file != mutable_config->trace_file) {
                    filename_changed = true;
                }

                // RCU 更新配置指针
                std::atomic_store(&config_ptr_, std::shared_ptr<const ProfilerConfig>(mutable_config));

                // 状态机处理
                if (tracing_is_on) {
                    if (!tracing_was_on) {
                        // 0 -> 1: 启动
                        InitTracing();
                    } else if (filename_changed) {
                        // 1 -> 1 (File Changed): 重启
                        // 必须先 Close 再 Init，否则 Init 发现 writer_running_ 会直接返回
                        CloseTracing();
                        InitTracing();
                    }
                } else {
                    if (tracing_was_on) {
                        // 1 -> 0: 关闭
                        CloseTracing();
                    }
                }
            }
        }
    }

    void ProfilerService::FlushCurrentThread() noexcept {
        t_trace_buffer.Flush();
    }

    void ProfilerService::RecordTime(PluginPtr<ILogger> logger, const char* category, const char* name, const char* payload, PayloadType p_type, const SourceLocation& loc, double ms) noexcept {
        try {
            if (!global_enable_cache_.load(std::memory_order_relaxed)) return;

            auto current_cfg = std::atomic_load(&config_ptr_);
            if (!current_cfg) return;

            if (current_cfg->enable_tracing) {
                size_t tid = t_trace_buffer.GetCachedTid();
                TraceEventToBuffer(category, name, ms, tid, payload, p_type);
            }

            double effective_threshold = current_cfg->alert_threshold_ms;
            std::string_view cat_view(category);
            for (const auto& rule : current_cfg->rules) {
                if (cat_view.size() >= rule.matcher.size() && cat_view.substr(0, rule.matcher.size()) == rule.matcher) {
                    effective_threshold = rule.threshold_ms; break;
                }
            }

            if (ms < effective_threshold) return;

            PluginPtr<ILogger> target_logger = logger;
            if (!target_logger) {
                auto svc = log_service_;
                if (svc) {
                    target_logger = svc->GetLogger(category);
                }
            }

            if (target_logger) {
                std::string msg;
                if (payload && *payload) {
                    msg = fmt::format("Step '{}' took {:.3f} ms | Data: {} (Threshold: {:.1f} ms)", name, ms, payload, effective_threshold);
                } else {
                    msg = fmt::format("Step '{}' took {:.3f} ms", name, ms);
                }
                target_logger->Log(LogSourceLocation{ loc.file, loc.line, loc.func }, LogLevel::Warn, msg);
            }

        } catch (...) {}
    }

    void ProfilerService::RecordCounter(const char* cat, const char* name, double val) noexcept { try { if (!global_enable_cache_.load(std::memory_order_relaxed)) return; auto cfg = std::atomic_load(&config_ptr_); if (cfg && cfg->enable_tracing) { size_t tid = t_trace_buffer.GetCachedTid(); TraceCounterToBuffer(cat, name, val, tid); } } catch (...) {} }
    void ProfilerService::SetThreadName(const char* name) noexcept { try { if (!global_enable_cache_.load(std::memory_order_relaxed)) return; auto cfg = std::atomic_load(&config_ptr_); if (cfg && cfg->enable_tracing) { size_t tid = t_trace_buffer.GetCachedTid(); TraceMetadataToBuffer(name, tid); } } catch (...) {} }
    void ProfilerService::MarkFrame(const char* name) noexcept { try { if (!global_enable_cache_.load(std::memory_order_relaxed)) return; auto cfg = std::atomic_load(&config_ptr_); if (cfg && cfg->enable_tracing) { size_t tid = t_trace_buffer.GetCachedTid(); TraceInstantToBuffer(name, 'p', tid); } } catch (...) {} }
    void ProfilerService::RecordFlow(const char* name, uint64_t id, FlowType type) noexcept { try { if (!global_enable_cache_.load(std::memory_order_relaxed)) return; auto cfg = std::atomic_load(&config_ptr_); if (cfg && cfg->enable_tracing) { char p = 's'; if (type == FlowType::Step)p = 't'; if (type == FlowType::End)p = 'f'; size_t tid = t_trace_buffer.GetCachedTid(); TraceFlowToBuffer(name, id, p, tid); } } catch (...) {} }

    void ProfilerService::SubmitChunk(TraceBufferChunk&& chunk) {
        if (!accepting_data_ && !writer_running_) return;

        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            if (chunk_queue_.size() >= cached_max_queue_size_.load(std::memory_order_relaxed)) {
                dropped_chunks_++;
                if (free_pool_.size() < 256) {
                    chunk.clear();
                    free_pool_.push_back(std::move(chunk));
                }
                return;
            }
            chunk_queue_.push(std::move(chunk));
        }
        queue_cv_.notify_one();
    }

    TraceBufferChunk ProfilerService::AcquireBuffer() {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        if (!free_pool_.empty()) {
            TraceBufferChunk buf = std::move(free_pool_.back());
            free_pool_.pop_back();
            return buf;
        }
        TraceBufferChunk buf;
        buf.reserve(kTraceBufferThreshold * 2);
        return buf;
    }

    void ProfilerService::FileWriterLoop() {
        auto cfg = std::atomic_load(&config_ptr_);
        size_t max_bytes = cfg->max_trace_file_mb * 1024 * 1024;
        int counter = 0;

        while (true) {
            TraceBufferChunk chunk;
            size_t q_size = 0;
            {
                std::unique_lock<std::mutex> lock(queue_mutex_);
                queue_cv_.wait(lock, [this] { return !chunk_queue_.empty() || !writer_running_; });

                if (chunk_queue_.empty() && !writer_running_) break;

                if (!chunk_queue_.empty()) {
                    chunk = std::move(chunk_queue_.front());
                    chunk_queue_.pop();
                    q_size = chunk_queue_.size();
                }
            }

            if (!chunk.empty() && trace_ofs_.is_open()) {
                if (!file_full_) {
                    trace_ofs_.write(chunk.data(), chunk.size());
                    current_file_size_ += chunk.size();
                    if (current_file_size_ >= max_bytes) file_full_ = true;

                    if (++counter > 100) {
                        counter = 0;
                        auto now = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
                        std::string msg = fmt::format("{{\"name\":\"Z3Y_Profiler_Queue\",\"cat\":\"__internal__\",\"ph\":\"C\",\"ts\":{},\"pid\":0,\"tid\":0,\"args\":{{\"size\":{}}}}},\n", now, q_size);
                        trace_ofs_.write(msg.data(), msg.size());
                    }
                } else {
                    dropped_chunks_++;
                }

                {
                    std::lock_guard<std::mutex> lock(queue_mutex_);
                    if (free_pool_.size() < 256) { chunk.clear(); free_pool_.push_back(std::move(chunk)); }
                }
            }
        }
    }

    void ProfilerService::InitTracing() {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        if (writer_running_) return;

        auto cfg = std::atomic_load(&config_ptr_);
        // 使用 Utf8ToPath 替代 u8path，解决 Windows 中文路径问题
        auto fs_path = z3y::utils::Utf8ToPath(cfg->trace_file);

        if (fs_path.has_parent_path()) {
            std::error_code ec;
            std::filesystem::create_directories(fs_path.parent_path(), ec);
        }

        trace_ofs_.clear();
        trace_ofs_.open(fs_path, std::ios::out | std::ios::trunc | std::ios::binary);
        if (trace_ofs_.is_open()) {
            trace_ofs_ << "{\"traceEvents\": [\n";
            trace_ofs_ << "{\"name\":\"process_name\",\"ph\":\"M\",\"pid\":0,\"tid\":0,\"args\":{\"name\":\"Host App\"}},\n";

            // 强制刷新，确保Header落盘
            trace_ofs_.flush();

            writer_running_ = true;
            accepting_data_ = true;
            writer_thread_ = std::thread(&ProfilerService::FileWriterLoop, this);
        } else {
            std::cerr << "[ProfilerService] ERROR: Failed to open trace file: " << fs_path.string() << std::endl;
        }
    }

    void ProfilerService::CloseTracing() {
        accepting_data_ = false;
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            writer_running_ = false;
        }
        queue_cv_.notify_all();

        if (writer_thread_.joinable()) writer_thread_.join();

        if (trace_ofs_.is_open()) {
            size_t dropped = dropped_chunks_.load();
            if (dropped > 0) {
                std::string msg = fmt::format("{{\"name\":\"DROPPED_CHUNKS\",\"ph\":\"i\",\"ts\":0,\"s\":\"g\",\"args\":{{\"count\":{}}}}},\n", dropped);
                trace_ofs_.write(msg.data(), msg.size());
            }

            trace_ofs_.clear();
            trace_ofs_ << "{}\n]}";

            // 强制刷新并关闭
            trace_ofs_.flush();
            trace_ofs_.close();
        }
    }

    template <typename OutputIt> void ProfilerService::EscapeJsonTo(OutputIt out, const char* input) {
        if (!input) return;
        for (const char* p = input; *p; ++p) {
            switch (*p) {
            case '\"':*out++ = '\\'; *out++ = '\"'; break;
            case '\\':*out++ = '\\'; *out++ = '\\'; break;
            case '\b':*out++ = '\\'; *out++ = 'b'; break;
            case '\f':*out++ = '\\'; *out++ = 'f'; break;
            case '\n':*out++ = '\\'; *out++ = 'n'; break;
            case '\r':*out++ = '\\'; *out++ = 'r'; break;
            case '\t':*out++ = '\\'; *out++ = 't'; break;
            default:if (static_cast<unsigned char>(*p) < 0x20) { fmt::format_to(out, "\\u{:04x}", static_cast<unsigned char>(*p)); } else { *out++ = *p; }break;
            }
        }
    }

#define CHECK_TLS_BINDING() \
        if (!t_trace_buffer.IsBoundTo(instance_id_)) { \
            t_trace_buffer.Flush(); \
            t_trace_buffer.BindService(weak_from_this(), instance_id_); \
        }

    void ProfilerService::TraceEventToBuffer(const char* cat, const char* name, double ms, size_t tid, const char* payload, PayloadType p_type) {
        CHECK_TLS_BINDING();
        auto now = std::chrono::steady_clock::now();
        auto ts_us = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
        long long dur_us = static_cast<long long>(ms * 1000.0);
        long long start_us = ts_us - dur_us;

        auto out = t_trace_buffer.GetInserter();
        fmt::format_to(out, "{{\"name\":\""); EscapeJsonTo(out, name);
        fmt::format_to(out, "\",\"cat\":\""); EscapeJsonTo(out, cat);
        fmt::format_to(out, "\",\"ph\":\"X\",\"ts\":{},\"dur\":{},\"pid\":0,\"tid\":{}", start_us, dur_us, tid);
        if (payload && *payload) {
            if (p_type == PayloadType::JsonSnippet) { fmt::format_to(out, ",\"args\":{{{}}}", payload); } else { fmt::format_to(out, ",\"args\":{{\"msg\":\""); EscapeJsonTo(out, payload); fmt::format_to(out, "\"}}"); }
        }
        fmt::format_to(out, "}}");
        t_trace_buffer.CommitWrite();
    }

    void ProfilerService::TraceCounterToBuffer(const char* cat, const char* name, double val, size_t tid) { CHECK_TLS_BINDING(); auto ts_us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); auto out = t_trace_buffer.GetInserter(); fmt::format_to(out, "{{\"name\":\""); EscapeJsonTo(out, name); fmt::format_to(out, "\",\"cat\":\""); EscapeJsonTo(out, cat); fmt::format_to(out, "\",\"ph\":\"C\",\"ts\":{},\"pid\":0,\"tid\":{},\"args\":{{\"", ts_us, tid); EscapeJsonTo(out, name); fmt::format_to(out, "\":{}}}}}", val); t_trace_buffer.CommitWrite(); }
    void ProfilerService::TraceMetadataToBuffer(const char* name, size_t tid) { CHECK_TLS_BINDING(); auto out = t_trace_buffer.GetInserter(); fmt::format_to(out, "{{\"name\":\"thread_name\",\"ph\":\"M\",\"pid\":0,\"tid\":{},\"args\":{{\"name\":\"{}\"}}}}", tid, name); t_trace_buffer.CommitWrite(); }
    void ProfilerService::TraceInstantToBuffer(const char* name, char scope, size_t tid) { CHECK_TLS_BINDING(); auto ts_us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); auto out = t_trace_buffer.GetInserter(); fmt::format_to(out, "{{\"name\":\""); EscapeJsonTo(out, name); fmt::format_to(out, "\",\"ph\":\"i\",\"ts\":{},\"pid\":0,\"tid\":{},\"s\":\"{}\"}}", ts_us, tid, scope); t_trace_buffer.CommitWrite(); }
    void ProfilerService::TraceFlowToBuffer(const char* name, uint64_t id, char phase, size_t tid) { CHECK_TLS_BINDING(); auto ts_us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); auto out = t_trace_buffer.GetInserter(); fmt::format_to(out, "{{\"name\":\""); EscapeJsonTo(out, name); fmt::format_to(out, "\",\"cat\":\"flow\",\"ph\":\"{}\",\"ts\":{},\"pid\":0,\"tid\":{},\"id\":{}}}", phase, ts_us, tid, id); t_trace_buffer.CommitWrite(); }

} // namespace