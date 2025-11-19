#
# CMakeLists.txt (src/plugin_demo_module_core)
# @brief 构建 'plugin_demo_module_core' 插件 (DLL/SO)。
# @author Yue Liu
# @date 2025-08-10
# @copyright Copyright (c) 2025 Yue Liu
#
# [受众：插件开发者 (作为示例)]
#
# [插件 CMake 清单]
# 1. `set(PLUGIN_SOURCES ...)`: 列出此插件的所有 .cpp 和 .h 文件。
# 2. `add_library(... SHARED)`:
#    将此目标定义为 `SHARED` 库 (即 DLL 或 .so)。
# 3. `set_target_properties(... OUTPUT_NAME ...)`: (推荐)
#    设置一个清晰的输出文件名。
# 4. `target_link_libraries(... PRIVATE ...)`:
#    - `z3y_plugin_manager`: *必须* 链接，以获取 PluginImpl, Z3Y_AUTO_REGISTER_ 等。
#    - `interfaces_demo`: *必须* 链接，
#      以获取它所实现的接口 (IDemoModule)
#      和它所 *依赖* 的接口 (IDemoLogger, IDemoSimple) 的定义。
# 5. `install(TARGETS ... RUNTIME ...)`:
#    将编译好的 .dll/.so 文件安装到 SDK 的 bin 目录，以便宿主 (host_console_demo.exe)
#    可以找到并加载它。
#

# 1. 手动列出此插件的所有源文件
set(PLUGIN_SOURCES
  plugin_entry.cpp
  demo_core_features.cpp
  demo_core_features.h)

# 2. 定义库 (SHARED = .dll / .so)
add_library(plugin_demo_module_core SHARED ${PLUGIN_SOURCES})

# 3. 设置输出名称 (例如 plugin_demo_module_core_x64d.dll)
set_target_properties(
  plugin_demo_module_core
  PROPERTIES OUTPUT_NAME "plugin_demo_module_core${Z3Y_ARCH_SUFFIX}")

# 4. 设置项目引用 (链接)
target_link_libraries(
  plugin_demo_module_core
  PRIVATE
  # 依赖框架核心
  z3y_plugin_manager
  # 依赖接口定义(IDemoModule, IDemoLogger, IDemoSimple)
  interfaces_demo)

# 5. 安装插件
#    将编译好的 .dll/.so 文件安装到 SDK 的 bin 目录
install(
  TARGETS plugin_demo_module_core
  # RUNTIME 目标 (DLLs, EXEs)
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})