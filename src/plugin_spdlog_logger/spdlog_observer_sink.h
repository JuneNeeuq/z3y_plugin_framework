#pragma once
#include <spdlog/sinks/base_sink.h>

#include <map>
#include <mutex>

#include "interfaces_core/i_log_service.h"

// 获取跨平台 PID
#ifdef _WIN32
#include <process.h>
#define Z3Y_GET_PID() _getpid()
#else
#include <unistd.h>
#define Z3Y_GET_PID() getpid()
#endif

namespace z3y::plugins::log {

template <typename Mutex>
class spdlog_observer_sink : public spdlog::sinks::base_sink<Mutex> {
 public:
  void add_observer(const std::string& name,
                    z3y::interfaces::core::LogObserverCallback cb) {
    std::lock_guard<Mutex> lock(this->mutex_);
    observers_[name] = cb;
  }

  void remove_observer(const std::string& name) {
    std::lock_guard<Mutex> lock(this->mutex_);
    observers_.erase(name);
  }

 protected:
  void sink_it_(const spdlog::details::log_msg& msg) override {
    if (observers_.empty()) return;  // 没人订阅，直接零开销跳过

    z3y::interfaces::core::LogRecord record{};
    record.struct_size =
        static_cast<uint32_t>(sizeof(z3y::interfaces::core::LogRecord));
    record.version = 1;

    record.timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                              msg.time.time_since_epoch())
                              .count();
    record.process_id = Z3Y_GET_PID();
    record.thread_id = static_cast<uint32_t>(msg.thread_id);
    record.level = static_cast<z3y::interfaces::core::LogLevel>(msg.level);

    // 提取源码位置
    record.file_name = msg.source.filename ? msg.source.filename : "Unknown";
    record.func_name = msg.source.funcname ? msg.source.funcname : "Unknown";
    record.line_number = msg.source.line;

    // 模块名转换
    std::string logger_name_str = fmt::to_string(msg.logger_name);
    record.logger_name = logger_name_str.c_str();

    // 格式化后的完整日志 (包含时间等)
    // 注意：这里由于我们直接给 UI，UI可能想要纯净的 payload，或者带格式的。
    // 这里我们给纯净的消息，UI 自己决定怎么拼。
    std::string payload_str = fmt::to_string(msg.payload);
    record.message = payload_str.c_str();

    // 同步分发给所有 UI
    for (const auto& pair : observers_) {
      pair.second(record);
    }
  }

  void flush_() override {}

 private:
  std::map<std::string, z3y::interfaces::core::LogObserverCallback> observers_;
};

using spdlog_observer_sink_mt = spdlog_observer_sink<std::mutex>;
}  // namespace z3y::plugins::log