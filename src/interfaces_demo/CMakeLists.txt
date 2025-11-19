#
# CMakeLists.txt (src/interfaces_demo)
# @brief 定义 'interfaces_demo' 库。
# @author Yue Liu
# @date 2025-07-20
# @copyright Copyright (c) 2025 Yue Liu
#
# [受众：框架维护者]
#
# [设计思想：INTERFACE 库]
# 'interfaces_demo' 是一个 "INTERFACE" 库。
# 这种库类型在 CMake 中表示它“没有源文件”，“只包含头文件”。
#
# 它的唯一作用是：
# 1. 定义 `target_include_directories`：
#    告诉 CMake，任何链接了 `interfaces_demo` 的目标
#    (例如 host_console_demo, plugin_demo_core_services)，
#    都应该 *自动* 将 `src/` (或安装后的 `include/`)
#    目录添加到它们的包含路径中。
#    `$<BUILD_INTERFACE:...>` 用于在本项目中构建时。
#    `$<INSTALL_INTERFACE:...>` 用于本项目被 "install"
#    为一个 SDK 并被外部 `find_package()` 使用时。
#
# 2. `target_link_libraries`：
#    声明 `interfaces_demo` *自身* 依赖于 `z3y_plugin_manager`。
#    这创建了一个依赖传递：
#    如果 `plugin_demo_runner` 链接了 `interfaces_demo`，
#    它也会 *自动* 链接 `z3y_plugin_manager`。
#
# 3. `install`：
#    将这个目录下的所有 `.h`
#    文件安装到 SDK 的 `include/interfaces_demo` 目录中。

# 1. 定义一个 INTERFACE 库 (没有源文件，只有头文件)
add_library(interfaces_demo INTERFACE)

# 2. 设置此接口库的包含目录
target_include_directories(
  interfaces_demo
  INTERFACE
  # 当在本项目中构建时 (例如 VSCode F5 调试)
  # 我们添加 src/ 目录，这样 #include "interfaces_demo/i_demo_logger.h" 就能工作
  $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src>
  # 当被外部 find_package 使用时 (发布 SDK)
  # 我们添加 <prefix>/include 目录，
  # 这样 #include "interfaces_demo/i_demo_logger.h" 也能工作
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

# 3. 接口库依赖框架
target_link_libraries(interfaces_demo INTERFACE z3y_plugin_manager)

# 4. 安装接口头文件
install(
  DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/interfaces_demo
  FILES_MATCHING
  # 只安装 .h 文件
  PATTERN "*.h")

# 5. 导出 INTERFACE 目标，
#    以便外部项目可以通过 find_package(z3y_plugin_framework) 找到它
install(TARGETS interfaces_demo EXPORT z3y_plugin_frameworkTargets)