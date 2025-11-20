#
# CMakeLists.txt (src/plugin_spdlog_logger)
# @brief 构建生产级日志插件 (DLL/SO)。
#

set(PLUGIN_SOURCES
  plugin_entry.cpp
  spdlog_provider_service.cpp
  spdlog_provider_service.h
)

add_library(plugin_spdlog_logger SHARED ${PLUGIN_SOURCES})

set_target_properties(
  plugin_spdlog_logger
  PROPERTIES OUTPUT_NAME "plugin_spdlog_logger${Z3Y_ARCH_SUFFIX}"
)

target_link_libraries(
  plugin_spdlog_logger
  PRIVATE
  z3y_plugin_manager      # 框架核心
  interfaces_core         # 日志接口
  spdlog::spdlog          # 实现依赖
  nlohmann_json::nlohmann_json # 配置解析依赖
)

install(
  TARGETS plugin_spdlog_logger
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)