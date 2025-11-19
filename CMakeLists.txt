#
# CMakeLists.txt (z3y_plugin_framework)
# @brief [根] 项目的顶层 CMake 构建脚本。
# @author Yue Liu
# @date 2025-08-24
# @copyright Copyright (c) 2025 Yue Liu
#
# [受众：框架维护者 / 编译项目的开发者]
#
# [设计思想]
# 这是整个项目的 CMake 根文件，负责：
# 1. 设置项目范围的编译选项 (例如 C++17, MSVC 运行时)。
# 2. 定义 SDK 的安装 (install) 路径和 `find_package` 支持。
# 3. 添加所有子项目 (z3y_plugin_manager, interfaces_demo, 插件, 宿主)。
#

# 1. 基础设置 (C++17)
cmake_minimum_required(VERSION 3.15)
project(z3y_plugin_framework VERSION 1.0)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 2. [编码]
#    强制 MSVC 编译器使用 UTF-8 编码读取源文件。
#    这可以修复因 .cpp 文件被保存为 UTF-8 (带 BOM 或不带)
#    而导致的 C4819 警告/错误。
if (MSVC)
  add_compile_options("/utf-8")

  # [C++ 运行时库]
  # [!! 关键 !!]
  #
  # [受众：框架维护者]
  #
  # [设计思想：ABI 稳定性的基石]
  #
  # 跨 DLL 边界安全地传递 C++ 标准库对象 (如 std::shared_ptr,
  # std::string, std::vector) 的 *唯一* 方法是：
  # *所有* 模块 (EXE 和所有 DLL) *必须* 动态链接 (Dynamic Link)
  # 到 *完全相同* 的 C++ 运行时库。
  #
  # - /MDd = Multi-threaded Debug DLL (用于 Debug 配置)
  # - /MD  =  Multi-threaded DLL (用于 Release 配置)
  #
  # 如果不设置这个， CMake 可能会默认使用 /MTd (静态链接)， 这将导致：
  # 1. host.exe 有一个 C++ 运行时的拷贝 (一个堆)。
  # 2. plugin.dll 有 *另一个* C++ 运行时的拷贝 (另一个堆)。
  # 3. 当 host.exe 调用 `manager->Create()` (在 DLL 中) 时，
  #    `std::make_shared` 在 *DLL 的堆*上分配内存。
  # 4. 当 host.exe 中的 `PluginPtr` (shared_ptr) 析构时，
  #    它试图在 *EXE 的堆* 上释放 DLL 分配的内存， 导致堆损坏和崩溃。
  #
  # [结论]
  # 此设置强制所有目标都使用 /MD 或 /MDd，
  # 确保整个应用程序共享 *一个* C++ 运行时堆。
  set(CMAKE_MSVC_RUNTIME_LIBRARY
      "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif ()

# 3. 包含 CMake 辅助工具 (用于 generate_export_header,
#    虽然本项目未使用，但这是个好习惯)
include(CMakePackageConfigHelpers)

# 4. 架构后缀 (用于 host_console_demo_x64d.exe)
if (CMAKE_SIZEOF_VOID_P EQUAL 8)
  set(Z3Y_ARCH_SUFFIX "_x64")
else ()
  set(Z3Y_ARCH_SUFFIX "_x86")
endif ()

# 5. 配置后缀 (d = Debug)
#    (例如 ..._x64d.dll)
set(CMAKE_DEBUG_POSTFIX "d")
#    (例如 ..._x64.dll)
set(CMAKE_RELEASE_POSTFIX "")
set(CMAKE_RELWITHDEBINFO_POSTFIX "") # (确保 RelWithDebInfo 也没有后缀)

# 6. 包含标准安装路径 (例如 CMAKE_INSTALL_BINDIR, CMAKE_INSTALL_LIBDIR)
#    这提供了跨平台 (Windows/Linux) 的标准目录名 (例如 bin, lib, include)。
include(GNUInstallDirs)

# 7. 设置 SDK 安装目录
#    (如果用户没有通过 cmake -DCMAKE_INSTALL_PREFIX=... 指定)
if (NOT CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}/sdk")
endif ()
message(STATUS "Install prefix set to: ${CMAKE_INSTALL_PREFIX}")

# 8. 设置 F5 调试的输出目录 (在 Visual Studio 或 VSCode 中)
#    [设计思想]
#    我们将所有 .lib, .dll, .exe
#    都输出到同一个地方 (`${CMAKE_BINARY_DIR}/bin`)，
#    以便 host.exe (RUNTIME) 可以在启动时立即找到 .dll (LIBRARY)。
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# 9. [设计思想：现代 CMake]
#    我们不再使用全局的 `include_directories()`。
#    所有包含路径都将由 `target_include_directories`
#    和 `target_link_libraries` (通过 `INTERFACE` 库) 自动传递。

# 10. 添加所有子项目 (CMake 将自动解析它们之间的依赖关系)
add_subdirectory(src/z3y_plugin_manager)
add_subdirectory(src/interfaces_demo)
add_subdirectory(src/plugin_demo_core_services)
add_subdirectory(src/plugin_demo_runner)
add_subdirectory(src/plugin_demo_module_core)
add_subdirectory(src/plugin_demo_module_events)
add_subdirectory(src/plugin_demo_module_introspection)

# (宿主最后添加)
add_subdirectory(src/host_console_demo)

# 11. [受众：框架维护者]
#     安装 CMake 导出文件 (用于 find_package)
#
#     [设计思想]
#     这会生成 `z3y_plugin_frameworkTargets.cmake`
#     文件， 允许 *其他* CMake 项目
#     (在安装此 SDK 后) 通过 `find_package(z3y_plugin_framework)`
#     来使用这个 SDK。
install(
  EXPORT z3y_plugin_frameworkTargets
  FILE z3y_plugin_frameworkTargets.cmake
  NAMESPACE z3y_plugin_framework::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/z3y_plugin_framework)