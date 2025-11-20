#
# CMakeLists.txt (tools/tool_log_benchmark)
# @brief 生产级日志性能测试与验证工具
#

set(TOOL_SOURCES
  main.cpp
)

add_executable(tool_log_benchmark ${TOOL_SOURCES})

set_target_properties(
  tool_log_benchmark
  PROPERTIES OUTPUT_NAME "tool_log_benchmark${Z3Y_ARCH_SUFFIX}"
)

# 链接框架核心与接口库
target_link_libraries(
  tool_log_benchmark
  PRIVATE
  z3y_plugin_manager  # 框架核心
  interfaces_core     # 日志接口
)

# 安装到 bin 目录
install(
  TARGETS tool_log_benchmark
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# 安装测试用的配置文件
install(
  FILES benchmark_config.json
  DESTINATION ${CMAKE_INSTALL_BINDIR}
)