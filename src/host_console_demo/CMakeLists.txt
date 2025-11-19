#
# CMakeLists.txt (src/host_console_demo)
# @brief 构建 'host_console_demo' 宿主可执行文件 (EXE)。
# @author Yue Liu
# @date 2025-08-23
# @copyright Copyright (c) 2025 Yue Liu
#
# [受众：框架使用者 (宿主开发者)]
#
# [宿主 CMake 清单]
# 1. `set(HOST_SOURCES ...)`: 列出此可执行文件的所有 .cpp 和 .h 文件。
# 2. `add_executable(...)`:
#    将此目标定义为可执行文件 (EXE)。
# 3. `set_target_properties(... OUTPUT_NAME ...)`: (推荐)
#    设置一个清晰的输出文件名。
# 4. `target_link_libraries(... PRIVATE ...)`:
#    - `z3y_plugin_manager`: *必须* 链接，以获取 PluginManager::Create,
#      z3y::GetService 等 API。
#    - `interfaces_demo`: *必须* 链接，
#      以获取宿主 *直接* 调用的接口 (如 IDemoRunner) 的定义。
# 5. `install(TARGETS ... RUNTIME ...)`:
#    将编译好的 .exe 文件安装到 SDK 的 bin 目录，
#    (与所有插件 .dll/.so 放在同一目录)。
#

# 1. 手动列出此可执行文件的所有源文件
set(HOST_SOURCES
  main.cpp
  host_event_listener.cpp
  host_event_listener.h)

# 2. 定义可执行文件 (EXE)
add_executable(host_console_demo ${HOST_SOURCES})

# 3. 设置输出名称 (例如 host_console_demo_x64d.exe)
set_target_properties(
  host_console_demo
  PROPERTIES OUTPUT_NAME "host_console_demo${Z3Y_ARCH_SUFFIX}")

# 4. 设置项目引用 (链接)
target_link_libraries(
  host_console_demo
  PRIVATE
  # [关键] 宿主必须链接框架核心
  z3y_plugin_manager
  # [关键] 宿主必须链接它需要调用的接口
  # (IDemoRunner, IDemoLogger)
  interfaces_demo)

# 5. 安装可执行文件
#    将编译好的 .exe 文件安装到 SDK 的 bin 目录
install(
  TARGETS host_console_demo
  # RUNTIME 目标 (DLLs, EXEs)
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})