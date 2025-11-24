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

#include <memory>
#include <chrono>
#include <cstring> 
#include <algorithm> // for std::min
#include <spdlog/fmt/fmt.h> // 依赖 fmt 库进行零分配格式化
#include "framework/z3y_service_locator.h"
#include "interfaces_profiler/i_profiler_service.h"

 // --- 编译期辅助函数 ---
namespace z3y::helpers {
    /**
     * @brief [编译期] 从文件路径中提取文件名。
     * @details 用于在编译阶段计算 `__FILE__` 的文件名部分，避免运行时的字符串处理开销。
     */
    constexpr const char* GetFileNameFromPath(const char* path) {
        const char* file = path;
        while (*path) { if (*path == '/' || *path == '\\') file = path + 1; path++; }
        return file;
    }
}

// 如果未定义模块名，默认使用当前源文件名
#ifndef Z3Y_PROFILE_MODULE_NAME
#define Z3Y_PROFILE_MODULE_NAME z3y::helpers::GetFileNameFromPath(__FILE__)
#endif

// 定义栈上 Payload 缓冲区大小 (字节)。超过此大小将被截断。
#ifndef Z3Y_PROFILER_PAYLOAD_SIZE
#define Z3Y_PROFILER_PAYLOAD_SIZE 512 
#endif

namespace z3y::profiler {

    /**
     * @brief [性能核心] 线程局部存储 (TLS) 的 Profiler 服务缓存。
     * @details
     * 每次调用 `GetDefaultService` 都涉及哈希查找和锁。
     * 通过 TLS 缓存 `weak_ptr`，我们可以将热路径上的服务获取开销降至接近零。
     */
    inline thread_local std::weak_ptr<z3y::interfaces::profiler::IProfilerService> g_tls_profiler_weak;

    /**
     * @brief [内部] 安全且高效地获取 Profiler 服务实例。
     * @return 服务指针，若服务不可用则返回 nullptr。
     */
    inline z3y::PluginPtr<z3y::interfaces::profiler::IProfilerService> GetProfilerSafe() {
        // 1. 快速路径：检查 TLS 缓存是否有效
        if (auto ptr = g_tls_profiler_weak.lock()) return ptr;

        // 2. 慢速路径：通过服务定位器查找
        auto [ptr, err] = z3y::TryGetDefaultService<z3y::interfaces::profiler::IProfilerService>();
        if (err == z3y::InstanceError::kSuccess && ptr) {
            g_tls_profiler_weak = ptr; // 更新 TLS 缓存
            return ptr;
        }
        return nullptr;
    }

    /**
     * @brief [测试支持] 重置 TLS 缓存。
     * @details 当 Profiler 服务被卸载或重新加载时，需要调用此函数清除过期的缓存。
     */
    inline void ResetProfilerCache() { g_tls_profiler_weak.reset(); }

    /**
     * @class ScopedTimer
     * @brief [RAII] 作用域计时器辅助类。
     * @details
     * 在构造时记录开始时间，析构时计算耗时并提交给 Profiler 服务。
     * 这是实现 `Z3Y_PROFILE_SCOPE` 等宏的基础。
     */
    class ScopedTimer {
    public:
        /**
         * @brief 构造函数 (全参数)。
         * @param logger 关联的日志器。若耗时超过阈值，将向此 Logger 输出警告。可以为 nullptr。
         * @param name 追踪点名称。
         * @param category 类别。
         * @param payload 附加数据。
         * @param p_type 数据类型。
         * @param file 源文件。
         * @param func 函数名。
         * @param line 行号。
         */
        ScopedTimer(z3y::PluginPtr<z3y::interfaces::core::ILogger> logger,
            const char* name, const char* category, const char* payload,
            z3y::interfaces::profiler::PayloadType p_type,
            const char* file, const char* func, int line)
            : logger_(logger), name_(name), category_(category), payload_(payload),
            p_type_(p_type), loc_{ file, func, line } {

            profiler_ = GetProfilerSafe();
            // 仅当服务存在且全局开关开启时，才获取时间戳，以减少开销
            if (profiler_ && profiler_->IsEnabled()) {
                start_ = std::chrono::steady_clock::now();
                valid_ = true;
            }
        }

        // 重载构造函数 (简化版，无 Logger)
        ScopedTimer(const char* name, const char* category,
            const char* file, const char* func, int line)
            : ScopedTimer(nullptr, name, category, nullptr,
                z3y::interfaces::profiler::PayloadType::Text,
                file, func, line) {
        }

        /**
         * @brief 析构函数。
         * @details 计算耗时并调用 Profiler 的 RecordTime 接口。
         */
        ~ScopedTimer() {
            if (!valid_ || !profiler_) return;
            auto end = std::chrono::steady_clock::now();
            double ms = std::chrono::duration<double, std::milli>(end - start_).count();
            // 提交数据 (传入可能存在的 logger_)
            profiler_->RecordTime(logger_, category_, name_, payload_, p_type_, loc_, ms);
        }

    private:
        z3y::PluginPtr<z3y::interfaces::profiler::IProfilerService> profiler_;
        z3y::PluginPtr<z3y::interfaces::core::ILogger> logger_;
        const char* name_;
        const char* category_;
        const char* payload_;
        z3y::interfaces::profiler::PayloadType p_type_;
        z3y::interfaces::profiler::SourceLocation loc_;
        std::chrono::time_point<std::chrono::steady_clock> start_;
        bool valid_ = false;
    };

    /**
     * @class LinearProfiler
     * @brief [RAII] 线性分段计时器。
     * @details 用于在一个长函数中分段记录多个步骤的耗时，而无需使用多个花括号作用域。
     */
    class LinearProfiler {
    public:
        /**
         * @brief 构造函数 (带 Logger)。
         * @param logger 关联的日志器。
         * @param step_name 初始步骤名称。
         * @param cat 类别。
         * @param file 源文件。
         * @param line 行号。
         */
        LinearProfiler(z3y::PluginPtr<z3y::interfaces::core::ILogger> logger,
            const char* step_name, const char* cat, const char* file, int line)
            : logger_(logger), current_step_(step_name), cat_(cat), loc_{ file, step_name, line } {

            profiler_ = GetProfilerSafe();
            if (profiler_ && profiler_->IsEnabled()) {
                start_ = std::chrono::steady_clock::now();
                last_ = start_;
                valid_ = true;
            }
        }

        // 重载构造函数 (不带 Logger)
        LinearProfiler(const char* step_name, const char* cat, const char* file, int line)
            : LinearProfiler(nullptr, step_name, cat, file, line) {
        }

        ~LinearProfiler() {
            if (valid_ && profiler_) {
                auto now = std::chrono::steady_clock::now();
                double ms = std::chrono::duration<double, std::milli>(now - start_).count();

                // 记录最后一步
                double last_step_ms = std::chrono::duration<double, std::milli>(now - last_).count();
                profiler_->RecordTime(logger_, cat_, current_step_, nullptr, z3y::interfaces::profiler::PayloadType::Text, loc_, last_step_ms);

                // 记录总耗时 (TOTAL)
                profiler_->RecordTime(logger_, cat_, "TOTAL", nullptr, z3y::interfaces::profiler::PayloadType::Text, loc_, ms);
            }
        }

        /**
         * @brief 结束当前步骤，开始下一步骤。
         * @param step 下一步骤的名称。
         * @param payload [可选] 当前步骤的附加信息。
         */
        void Next(const char* step, const char* payload = nullptr) {
            if (!valid_ || !profiler_) return;
            auto now = std::chrono::steady_clock::now();
            double ms = std::chrono::duration<double, std::milli>(now - last_).count();

            // 提交上一步的数据 (使用构造时传入的 logger_)
            profiler_->RecordTime(logger_, cat_, current_step_, payload, z3y::interfaces::profiler::PayloadType::Text, loc_, ms);

            current_step_ = step;
            last_ = now;
        }
    private:
        z3y::PluginPtr<z3y::interfaces::profiler::IProfilerService> profiler_;
        z3y::PluginPtr<z3y::interfaces::core::ILogger> logger_; // 保存 Logger 指针
        const char* cat_;
        const char* current_step_;
        z3y::interfaces::profiler::SourceLocation loc_;
        std::chrono::time_point<std::chrono::steady_clock> start_, last_;
        bool valid_ = false;
    };
}

// =============================================================================
// [用户宏定义]
// =============================================================================

// 宏拼接辅助工具
#define Z3Y_PROF_CAT_I(a, b) a##b
#define Z3Y_PROF_CAT(a, b) Z3Y_PROF_CAT_I(a, b)
#define Z3Y_PROF_VAR(line) Z3Y_PROF_CAT(z3y_prof_timer_, line)
#define Z3Y_PROF_BUF(line) Z3Y_PROF_CAT(z3y_prof_buf_, line)
#define Z3Y_PROF_RES(line) Z3Y_PROF_CAT(z3y_prof_res_, line)
#define Z3Y_FILE_NAME z3y::helpers::GetFileNameFromPath(__FILE__)

// -----------------------------------------------------------------------------
// 1. 基础 Scope (只记录时间)
// -----------------------------------------------------------------------------

/**
 * @brief [宏] 基础作用域计时。
 * @param name 追踪点名称。
 */
#define Z3Y_PROFILE_SCOPE(name) \
        z3y::profiler::ScopedTimer Z3Y_PROF_VAR(__LINE__)( \
            name, Z3Y_PROFILE_MODULE_NAME, \
            Z3Y_FILE_NAME, __FUNCTION__, __LINE__)

 /**
  * @brief [宏] 关联 Logger 的作用域计时。
  * @details 如果耗时超过配置阈值，除了记录 Trace 外，还会向指定的 logger 输出警告日志。
  * @param logger 目标 Logger 指针。
  * @param name 追踪点名称。
  */
#define Z3Y_PROFILE_SCOPE_LOG(logger, name) \
        z3y::profiler::ScopedTimer Z3Y_PROF_VAR(__LINE__)( \
            logger, name, Z3Y_PROFILE_MODULE_NAME, nullptr, \
            z3y::interfaces::profiler::PayloadType::Text, \
            Z3Y_FILE_NAME, __FUNCTION__, __LINE__)

  // -----------------------------------------------------------------------------
  // 2. 带格式化消息的 Scope (Text Payload)
  // -----------------------------------------------------------------------------

  /**
   * @brief [宏] 带格式化消息的作用域计时。
   */
#define Z3Y_PROFILE_SCOPE_MSG(name, format, ...) \
        char Z3Y_PROF_BUF(__LINE__)[Z3Y_PROFILER_PAYLOAD_SIZE]; \
        auto Z3Y_PROF_RES(__LINE__) = fmt::format_to_n( \
            Z3Y_PROF_BUF(__LINE__), \
            Z3Y_PROFILER_PAYLOAD_SIZE - 1, \
            format, ##__VA_ARGS__); \
        Z3Y_PROF_BUF(__LINE__)[(std::min)(Z3Y_PROF_RES(__LINE__).size, size_t(Z3Y_PROFILER_PAYLOAD_SIZE - 1))] = '\0'; \
        z3y::profiler::ScopedTimer Z3Y_PROF_VAR(__LINE__)( \
            nullptr, name, Z3Y_PROFILE_MODULE_NAME, Z3Y_PROF_BUF(__LINE__), \
            z3y::interfaces::profiler::PayloadType::Text, \
            Z3Y_FILE_NAME, __FUNCTION__, __LINE__)

   /**
    * @brief [宏] 带格式化消息的作用域计时 (支持 Logger)。
    * @param logger 目标 Logger 指针。
    */
#define Z3Y_PROFILE_SCOPE_MSG_LOG(logger, name, format, ...) \
        char Z3Y_PROF_BUF(__LINE__)[Z3Y_PROFILER_PAYLOAD_SIZE]; \
        auto Z3Y_PROF_RES(__LINE__) = fmt::format_to_n( \
            Z3Y_PROF_BUF(__LINE__), \
            Z3Y_PROFILER_PAYLOAD_SIZE - 1, \
            format, ##__VA_ARGS__); \
        Z3Y_PROF_BUF(__LINE__)[(std::min)(Z3Y_PROF_RES(__LINE__).size, size_t(Z3Y_PROFILER_PAYLOAD_SIZE - 1))] = '\0'; \
        z3y::profiler::ScopedTimer Z3Y_PROF_VAR(__LINE__)( \
            logger, name, Z3Y_PROFILE_MODULE_NAME, Z3Y_PROF_BUF(__LINE__), \
            z3y::interfaces::profiler::PayloadType::Text, \
            Z3Y_FILE_NAME, __FUNCTION__, __LINE__)

    // -----------------------------------------------------------------------------
    // 3. 带结构化参数的 Scope (JSON Payload)
    // -----------------------------------------------------------------------------

    /**
     * @brief [宏] 带结构化参数 (JSON) 的作用域计时。
     */
#define Z3Y_PROFILE_SCOPE_ARGS(name, format, ...) \
        char Z3Y_PROF_BUF(__LINE__)[Z3Y_PROFILER_PAYLOAD_SIZE]; \
        auto Z3Y_PROF_RES(__LINE__) = fmt::format_to_n( \
            Z3Y_PROF_BUF(__LINE__), \
            Z3Y_PROFILER_PAYLOAD_SIZE - 1, \
            format, ##__VA_ARGS__); \
        Z3Y_PROF_BUF(__LINE__)[(std::min)(Z3Y_PROF_RES(__LINE__).size, size_t(Z3Y_PROFILER_PAYLOAD_SIZE - 1))] = '\0'; \
        z3y::profiler::ScopedTimer Z3Y_PROF_VAR(__LINE__)( \
            nullptr, name, Z3Y_PROFILE_MODULE_NAME, Z3Y_PROF_BUF(__LINE__), \
            z3y::interfaces::profiler::PayloadType::JsonSnippet, \
            Z3Y_FILE_NAME, __FUNCTION__, __LINE__)

     /**
      * @brief [宏] 带结构化参数 (JSON) 的作用域计时 (支持 Logger)。
      * @param logger 目标 Logger 指针。
      */
#define Z3Y_PROFILE_SCOPE_ARGS_LOG(logger, name, format, ...) \
        char Z3Y_PROF_BUF(__LINE__)[Z3Y_PROFILER_PAYLOAD_SIZE]; \
        auto Z3Y_PROF_RES(__LINE__) = fmt::format_to_n( \
            Z3Y_PROF_BUF(__LINE__), \
            Z3Y_PROFILER_PAYLOAD_SIZE - 1, \
            format, ##__VA_ARGS__); \
        Z3Y_PROF_BUF(__LINE__)[(std::min)(Z3Y_PROF_RES(__LINE__).size, size_t(Z3Y_PROFILER_PAYLOAD_SIZE - 1))] = '\0'; \
        z3y::profiler::ScopedTimer Z3Y_PROF_VAR(__LINE__)( \
            logger, name, Z3Y_PROFILE_MODULE_NAME, Z3Y_PROF_BUF(__LINE__), \
            z3y::interfaces::profiler::PayloadType::JsonSnippet, \
            Z3Y_FILE_NAME, __FUNCTION__, __LINE__)

      // -----------------------------------------------------------------------------
      // 4. 便捷宏 (自动函数名)
      // -----------------------------------------------------------------------------

#define Z3Y_PROFILE_FUNCTION() Z3Y_PROFILE_SCOPE(__FUNCTION__)

/**
 * @brief [宏] 自动函数名计时 (支持 Logger)。
 */
#define Z3Y_PROFILE_FUNCTION_LOG(logger) Z3Y_PROFILE_SCOPE_LOG(logger, __FUNCTION__)

#define Z3Y_PROFILE_FUNCTION_MSG(format, ...) Z3Y_PROFILE_SCOPE_MSG(__FUNCTION__, format, ##__VA_ARGS__)

 /**
  * @brief [宏] 带消息的自动函数名计时 (支持 Logger)。
  */
#define Z3Y_PROFILE_FUNCTION_MSG_LOG(logger, format, ...) Z3Y_PROFILE_SCOPE_MSG_LOG(logger, __FUNCTION__, format, ##__VA_ARGS__)

  // -----------------------------------------------------------------------------
  // 5. 线性 Profiler 宏
  // -----------------------------------------------------------------------------

#define Z3Y_PROFILE_LINEAR_BEGIN(name) \
        z3y::profiler::LinearProfiler _z3y_linear_prof( \
            name, Z3Y_PROFILE_MODULE_NAME, \
            Z3Y_FILE_NAME, __LINE__)

/**
 * @brief [宏] 线性计时器开始 (支持 Logger)。
 * @details 该 Logger 将被用于记录该线性流程中的所有步骤 (Next) 以及总耗时 (Total)。
 */
#define Z3Y_PROFILE_LINEAR_BEGIN_LOG(logger, name) \
        z3y::profiler::LinearProfiler _z3y_linear_prof( \
            logger, name, Z3Y_PROFILE_MODULE_NAME, \
            Z3Y_FILE_NAME, __LINE__)

#define Z3Y_PROFILE_NEXT(name) _z3y_linear_prof.Next(name)

#define Z3Y_PROFILE_NEXT_MSG(name, format, ...) \
        do { \
            char _buf[Z3Y_PROFILER_PAYLOAD_SIZE]; \
            auto _res = fmt::format_to_n(_buf, Z3Y_PROFILER_PAYLOAD_SIZE - 1, format, ##__VA_ARGS__); \
            _buf[(std::min)(_res.size, size_t(Z3Y_PROFILER_PAYLOAD_SIZE - 1))] = '\0'; \
            _z3y_linear_prof.Next(name, _buf); \
        } while(0)

 // -----------------------------------------------------------------------------
 // 6. 其他功能宏
 // -----------------------------------------------------------------------------

#define Z3Y_PROFILE_COUNTER(name, value) \
        if (auto _p = z3y::profiler::GetProfilerSafe()) \
            _p->RecordCounter(Z3Y_PROFILE_MODULE_NAME, name, static_cast<double>(value))

#define Z3Y_PROFILE_THREAD_NAME(name) \
        if (auto _p = z3y::profiler::GetProfilerSafe()) _p->SetThreadName(name)

#define Z3Y_PROFILE_MARK_FRAME(name) \
        if (auto _p = z3y::profiler::GetProfilerSafe()) _p->MarkFrame(name)

#define Z3Y_PROFILE_FLOW_START(name, id) \
        if (auto _p = z3y::profiler::GetProfilerSafe()) \
            _p->RecordFlow(name, (uint64_t)(id), z3y::interfaces::profiler::FlowType::Start)

/**
 * @brief [宏] 强制刷新当前线程的 TLS 缓冲区。
 * @details 建议在长生命周期线程的空闲时刻，或短生命周期线程退出前调用。
 */
#define Z3Y_PROFILE_FLUSH() \
        if (auto _p = z3y::profiler::GetProfilerSafe()) _p->FlushCurrentThread()