#
# CMakeLists.txt (src/plugin_demo_core_services)
# @brief 构建 'plugin_demo_core_services' 插件 (DLL/SO)。
# @author Yue Liu
# @date 2025-08-02
# @copyright Copyright (c) 2025 Yue Liu
#
# [受众：插件开发者 (作为示例)]
#
# [插件 CMake 清单]
# 1. `set(PLUGIN_SOURCES ...)`: 列出此插件的所有 .cpp 和 .h 文件。
# 2. `add_library(... SHARED)`:
#    将此目标定义为 `SHARED` 库 (即 DLL 或 .so)。
#    这是成为一个“插件”所必需的。
# 3. `set_target_properties(... OUTPUT_NAME ...)`: (推荐)
#    设置一个清晰的输出文件名。
# 4. `target_link_libraries(... PRIVATE ...)`:
#    - `z3y_plugin_manager`: *必须* 链接，以获取 PluginImpl, Z3Y_AUTO_REGISTER_
#      等。
#    - `interfaces_demo`: *必须* 链接，以获取它所实现的接口
#      (如 IDemoLogger) 的定义。
# 5. `install(TARGETS ... RUNTIME ...)`:
#    将编译好的 .dll/.so 文件安装到 SDK 的 bin
#    目录，以便宿主 (host_console_demo.exe)
#    可以在运行时找到并加载它。
#

# 1. 手动列出此插件的所有源文件
set(PLUGIN_SOURCES
  plugin_entry.cpp
  demo_logger_service.cpp
  demo_logger_service.h
  demo_simple_impl_a.cpp
  demo_simple_impl_a.h
  demo_simple_impl_b.cpp
  demo_simple_impl_b.h
  advanced_demo_logger.cpp
  advanced_demo_logger.h
)

# 2. 定义库 (SHARED = .dll / .so)
add_library(plugin_demo_core_services SHARED ${PLUGIN_SOURCES})

# 3. 设置输出名称 (例如
#    plugin_demo_core_services_x64d.dll)
set_target_properties(
  plugin_demo_core_services
  PROPERTIES OUTPUT_NAME
             "plugin_demo_core_services${Z3Y_ARCH_SUFFIX}")

# 4. 设置项目引用 (链接)
target_link_libraries(
  plugin_demo_core_services
  PRIVATE
  # 依赖框架核心 (PluginImpl, 自动注册宏)
  z3y_plugin_manager
  # 依赖接口定义 (IDemoLogger, IDemoSimple, IAdvancedDemoLogger)
  interfaces_demo)

# 5. 安装插件
#    将编译好的 .dll/.so 文件安装到 SDK 的 bin 目录
install(
  TARGETS plugin_demo_core_services
  # RUNTIME 目标 (DLLs, EXEs)
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})