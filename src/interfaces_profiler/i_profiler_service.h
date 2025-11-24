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
#include "framework/z3y_define_interface.h"
#include "interfaces_core/i_log_service.h"

namespace z3y::interfaces::profiler {

    /**
     * @struct SourceLocation
     * @brief 源代码位置信息结构体。
     * @details 用于在性能数据中记录埋点的具体位置。
     * @note 这是一个 POD (Plain Old Data) 类型，可以安全地跨 DLL 边界传递。
     */
    struct SourceLocation {
        const char* file; //!< 源文件名 (通常由 __FILE__ 宏提供)
        const char* func; //!< 函数名 (通常由 __FUNCTION__ 宏提供)
        int line;         //!< 行号 (通常由 __LINE__ 宏提供)
    };

    /**
     * @enum FlowType
     * @brief 跨线程/跨进程的流式追踪事件类型。
     * @details 用于描述一个长链路操作（如网络请求处理）的生命周期阶段。
     */
    enum class FlowType {
        Start, //!< 流开始
        Step,  //!< 流中间步骤
        End    //!< 流结束
    };

    /**
     * @enum PayloadType
     * @brief 附加数据的类型。
     * @details 指示 Profiler 如何处理 `payload` 字符串。
     */
    enum class PayloadType {
        Text,       //!< 普通文本，将被转义并作为 "args":{"msg":"..."} 存储
        JsonSnippet //!< 预格式化的 JSON 片段 (如 "\"key\":123")，将直接嵌入到 args 对象中
    };

    /**
     * @class IProfilerService
     * @brief [核心服务] 性能分析服务接口。
     * @details
     * 这是性能分析模块的核心抽象接口。它提供了一组低开销的 API，用于记录
     * 函数耗时、计数器、线程元数据及跨线程流追踪。
     *
     * [面向使用者]
     * 1. **不要直接调用**: 除非你是硬核用户，否则请使用 `profiler_macros.h` 中提供的宏 (如 `Z3Y_PROFILE_SCOPE`)。
     * 宏提供了自动上下文捕获、RAII 管理和编译期优化。
     * 2. **线程安全**: 所有接口均为线程安全 (Thread-Safe) 且 `noexcept` (不抛出异常)。
     * 3. **性能**: 接口设计经过极致优化，但仍建议避免在极高频 (如每秒百万次) 的循环内部调用。
     *
     * [面向维护者]
     * 1. **ABI 稳定性**: 本接口继承自 `IComponent`，遵循框架的 ABI 规范。
     * 所有字符串参数均使用 `const char*` 传递，避免 `std::string` 跨 DLL 边界的内存分配问题。
     * 2. **扩展性**: 如需新增功能，请优先考虑增加新的虚函数（追加到末尾）或升级接口版本。
     */
    class IProfilerService : public virtual IComponent {
    public:
        //! 定义接口 ID 和版本号 (v1.0)
        Z3Y_DEFINE_INTERFACE(IProfilerService, "z3y-core-IProfilerService-IID-P001", 1, 0);

        /**
         * @brief 记录一段代码的执行耗时。
         * @param logger [可选] 关联的日志器。如果提供，且耗时超过阈值，可能会输出文本警告日志。
         * @param category 类别名称 (用于过滤和分组)，通常是模块名。
         * @param name 追踪点名称 (如函数名)。
         * @param payload [可选] 附加数据字符串。
         * @param payload_type 附加数据的类型 (文本或 JSON 片段)。
         * @param loc 源代码位置信息。
         * @param ms 耗时 (毫秒)。
         */
        virtual void RecordTime(
            z3y::PluginPtr<z3y::interfaces::core::ILogger> logger,
            const char* category,
            const char* name,
            const char* payload,
            PayloadType payload_type,
            const SourceLocation& loc,
            double ms
        ) noexcept = 0;

        /**
         * @brief 记录一个数值型计数器。
         * @details 用于追踪随时间变化的数值，如内存使用量、队列长度、FPS 等。
         * @param category 类别名称。
         * @param name 计数器名称。
         * @param value 当前数值。
         */
        virtual void RecordCounter(const char* category, const char* name, double value) noexcept = 0;

        /**
         * @brief 设置当前线程的名称。
         * @details 该名称将显示在性能分析工具 (如 Chrome Tracing) 的线程视图中。
         * @param name 线程名称 (如 "RenderThread")。
         */
        virtual void SetThreadName(const char* name) noexcept = 0;

        /**
         * @brief 标记一个瞬时事件 (Frame Mark)。
         * @details 通常用于标记一帧的开始或结束，在时间轴上显示为一个小竖条。
         * @param name 事件名称 (如 "VBlank")。
         */
        virtual void MarkFrame(const char* name) noexcept = 0;

        /**
         * @brief 记录流式追踪事件 (Flow Event)。
         * @details 用于关联不同线程或时间段的逻辑操作。
         * @param name 流名称。
         * @param flow_id 流的唯一标识符 (ID)，用于匹配 Start/Step/End。
         * @param type 事件类型 (开始、步骤、结束)。
         */
        virtual void RecordFlow(const char* name, uint64_t flow_id, FlowType type) noexcept = 0;

        /**
         * @brief 检查性能分析是否全局开启。
         * @details 这是一个高性能的检查接口，用于在执行昂贵的数据准备操作前快速跳过。
         * @return true 如果全局启用，false 如果禁用。
         */
        virtual bool IsEnabled() const noexcept = 0;

        /**
         * @brief 强制刷新当前线程的 TLS 缓冲区。
         * @details
         * 性能分析器使用线程局部存储 (TLS) 缓冲数据以减少锁竞争。
         * 在某些场景下（如单元测试结束、线程即将销毁且无法自动感知时），
         * 调用此函数可以确保缓冲区内剩余的数据立即提交到全局队列，防止数据丢失。
         */
        virtual void FlushCurrentThread() noexcept = 0;
    };

} // namespace