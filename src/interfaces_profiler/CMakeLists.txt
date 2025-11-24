# src/interfaces_profiler/CMakeLists.txt

# [设计思想: Interface Library]
# 这是一个 Header-only 的库。它不产生 .lib 或 .a 文件，只提供头文件路径和依赖传递。
# 任何链接此库的目标（如业务插件），都会自动获得头文件搜索路径。
add_library(interfaces_profiler INTERFACE)

# [路径配置]
# $<BUILD_INTERFACE>: 在本项目构建时，指向 src 目录。
# $<INSTALL_INTERFACE>: 在安装后（SDK模式），指向 include 目录。
target_include_directories(
  interfaces_profiler
  INTERFACE
  $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

# [依赖声明]
# 依赖 plugin_manager (为了 IComponent, PluginPtr)
# 依赖 interfaces_core (为了 ILogger)
target_link_libraries(
  interfaces_profiler
  INTERFACE
  z3y_plugin_manager
  interfaces_core
)

# [安装规则]
install(
  DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/interfaces_profiler
  FILES_MATCHING PATTERN "*.h"
)

# [导出目标]
install(TARGETS interfaces_profiler EXPORT z3y_plugin_frameworkTargets)