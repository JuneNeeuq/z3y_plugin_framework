# src/plugin_profiler/CMakeLists.txt

set(PLUGIN_SOURCES
  plugin_entry.cpp
  profiler_service.cpp
  profiler_service.h
)

add_library(plugin_profiler SHARED ${PLUGIN_SOURCES})

# 设置输出文件名 (如 plugin_profiler_x64d.dll)
set_target_properties(plugin_profiler PROPERTIES OUTPUT_NAME "plugin_profiler${Z3Y_ARCH_SUFFIX}")

# 链接依赖
target_link_libraries(plugin_profiler
  PRIVATE
  z3y_plugin_manager      # 框架核心
  interfaces_profiler     # 自身接口
  interfaces_core         # 日志和配置接口
  nlohmann_json::nlohmann_json # JSON支持
)

# 安装插件 DLL
install(TARGETS plugin_profiler RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# 安装默认配置文件到 bin/config/
install(FILES profiler_config.json DESTINATION ${CMAKE_INSTALL_BINDIR}/config)