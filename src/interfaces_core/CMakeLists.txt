#
# CMakeLists.txt (src/interfaces_core)
# @brief 定义 'interfaces_core' 库 (生产级核心插件SDK)。
# @author Yue Liu
# @date 2025
#
# [设计思想]
# 这是一个 INTERFACE 库，它不仅定义了接口 (ILogger)，
# 还打包了必要的依赖 (spdlog::spdlog)，以便消费者能直接使用 Z3Y_LOG_INFO 宏。
#

# 1. 定义一个 INTERFACE 库 (只有头文件)
add_library(interfaces_core INTERFACE)

# 2. 设置包含目录
target_include_directories(
  interfaces_core
  INTERFACE
  $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

# 3. [关键] 声明此SDK的依赖
target_link_libraries(
  interfaces_core
  INTERFACE
  # 依赖纯框架 (为了 IComponent, PluginPtr)
  z3y_plugin_manager
  # 依赖 spdlog (为了 fmt 格式化库，宏实现需要它)
  spdlog::spdlog
)

# 4. 安装接口头文件
install(
  DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/interfaces_core
  FILES_MATCHING
  PATTERN "*.h"
)

# 5. 导出目标
install(TARGETS interfaces_core EXPORT z3y_plugin_frameworkTargets)