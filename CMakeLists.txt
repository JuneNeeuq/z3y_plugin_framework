#
# CMakeLists.txt (z3y_plugin_framework)
# @brief [根] 项目的顶层 CMake 构建脚本。
# @author Yue Liu
# @date 2025-11-21
#
# [受众：框架维护者 / 编译项目的开发者]
#
# [架构概览]
# 这是整个插件框架的“总指挥部”，负责协调所有子模块的构建、依赖管理和最终的 SDK 打包。
#
# [核心职责]
# 1. **环境标准化**: 强制 C++17 标准，统一 MSVC 编码 (UTF-8) 和运行时库 (MD)。
# 2. **依赖管理**: 集中管理第三方库 (spdlog, json, gtest)，并解决它们的安装导出问题。
# 3. **路径规划**: 定义清晰的 bin/lib 输出目录，确保宿主能直接加载插件。
# 4. **质量保障**: 集成 Google Test 框架，管理单元测试和集成测试。
# 5. **SDK 导出**: 生成 CMake 配置文件，让其他项目能通过 `find_package` 使用本框架。
#

# ==============================================================================
# 1. 项目基础设置
# ==============================================================================

# 设定 CMake 最低版本要求 (3.15+ 支持更现代的 install(EXPORT) 语法)
cmake_minimum_required(VERSION 3.15)

# 定义项目名称和版本号
project(z3y_plugin_framework VERSION 2.0.0)

# [关键] 强制使用 C++17 标准
# 框架使用了 std::shared_mutex, std::filesystem 等 C++17 特性。
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# 禁用编译器特定的扩展 (如 GCC 的 gnu++17)，强制使用标准 ISO C++，保证跨编译器兼容性。
set(CMAKE_CXX_EXTENSIONS OFF)

# ==============================================================================
# 2. 编译器与运行时环境配置 (ABI 兼容性的基石)
# ==============================================================================

if (MSVC)
  # [编码] 强制 MSVC 使用 UTF-8 编码读取源文件。
  # 这解决了中文注释或字符串在不同 locale 下可能导致的 C4819 警告或乱码错误。
  add_compile_options("/utf-8")

  # [ABI 关键] 统一 C++ 运行时库 (CRT)
  #
  # [设计思想]
  # 跨 DLL 边界传递 C++ 标准库对象 (如 std::shared_ptr, std::string) 是极其危险的。
  # 唯一安全的方法是确保 EXE 和所有 DLL 都动态链接到 *同一个* CRT (DLL 版本)。
  #
  # - /MDd = Multi-threaded Debug DLL (Debug 模式)
  # - /MD  = Multi-threaded DLL (Release 模式)
  #
  # 如果不强制指定，CMake 可能会默认使用 /MT (静态链接 CRT)。
  # 这会导致每个模块都有自己的堆 (Heap)，跨模块释放内存会直接导致崩溃 (Heap Corruption)。
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
  
  # [调试] 提高警告等级，开启多处理器编译
  add_compile_options(/W3 /MP)
endif ()

# 引入 CMake 辅助工具 (用于生成版本配置文件等)
include(CMakePackageConfigHelpers)

# ==============================================================================
# 3. 第三方依赖管理 (Third-party Dependencies)
# ==============================================================================
# [设计思想]
# 我们采用 "Vendor" 策略，将第三方源码包含在项目内 (third_party 目录)。
# 这比依赖系统安装的库更可控，确保了所有开发者使用完全相同的版本。

# [技巧] 禁用第三方库的默认安装规则
# 很多第三方库 (如 spdlog) 自带了 install() 指令。如果我们直接 add_subdirectory，
# 它们的安装规则会污染我们的安装目录，甚至生成冲突的 xxxConfig.cmake 文件。
# 所以我们先关闭它们的安装开关，然后由我们手动接管安装逻辑。
set(JSON_Install OFF CACHE INTERNAL "Disable json install") 
set(SPDLOG_INSTALL OFF CACHE INTERNAL "Disable spdlog install") 

# [关键] 开启 spdlog 的宽字符文件名支持 (Windows 专用)
# 如果不开启，spdlog 在 Windows 上默认使用 ANSI 接口打开文件。
# 当日志路径包含中文 (如 "logs/测试.log") 时，会导致创建文件失败。
# 开启后，spdlog 接受 wchar_t* (std::wstring) 路径，完美支持 Unicode。
if(WIN32)
    set(SPDLOG_WCHAR_FILENAMES ON CACHE BOOL "Use wchar for filenames on Windows" FORCE)
endif()

# 添加子目录 (构建第三方库)
add_subdirectory(third_party/nlohmann_json-3.12.0)
add_subdirectory(third_party/spdlog-1.16.0)

# [核心难点解决] 手动导出第三方库目标
#
# 问题：我们的接口库 `interfaces_core` 依赖了 `spdlog`。
# 当外部项目 `find_package(z3y_plugin_framework)` 时，CMake 需要知道 `spdlog` 在哪里。
#
# 解决：我们将 `spdlog` 和 `nlohmann_json` 的目标 (Targets) 
# 强制加入到本项目的导出集 (z3y_plugin_frameworkTargets) 中。
# 这样，它们就变成了本框架 SDK 的一部分，对使用者透明。

# 1. 导出 nlohmann_json (Header-only 库)
install(TARGETS nlohmann_json 
    EXPORT z3y_plugin_frameworkTargets
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

# 2. 导出 spdlog (静态库或 Header-only)
install(TARGETS spdlog 
    EXPORT z3y_plugin_frameworkTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

# ==============================================================================
# 4. 构建输出路径与命名规范
# ==============================================================================

# 架构后缀 (例如 _x64)
if (CMAKE_SIZEOF_VOID_P EQUAL 8)
  set(Z3Y_ARCH_SUFFIX "_x64")
else ()
  set(Z3Y_ARCH_SUFFIX "_x86")
endif ()

# 配置后缀 (例如 d)
# Debug 版本自动追加 'd' 后缀 (如 plugin_x64d.dll)，方便肉眼区分 Release/Debug。
set(CMAKE_DEBUG_POSTFIX "d")
set(CMAKE_RELEASE_POSTFIX "")
set(CMAKE_RELWITHDEBINFO_POSTFIX "")

# 引入 GNU 标准安装目录变量 (CMAKE_INSTALL_BINDIR 等)
include(GNUInstallDirs)

# 设置默认安装前缀 (Install Prefix)
if (NOT CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}/sdk")
endif ()
message(STATUS "SDK Install prefix set to: ${CMAKE_INSTALL_PREFIX}")

# [开发体验优化] 设置构建输出目录
# 我们强制将所有生成物 (DLL, LIB, EXE) 输出到同一个 bin 目录。
# 这样，在 IDE (VS/CLion) 中按 F5 调试时，宿主程序 (EXE) 能直接找到旁边的插件 (DLL)。
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib) # .lib 文件
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin) # .dll / .so 文件
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin) # .exe 文件

# ==============================================================================
# 5. 添加子模块 (按依赖顺序)
# ==============================================================================

# 5.1 框架基础设施
add_subdirectory(src/z3y_plugin_manager)  # 核心管理器 (Core)
add_subdirectory(src/interfaces_core)     # 核心接口定义 (ABI)

# 5.2 基础服务插件
add_subdirectory(src/plugin_spdlog_logger) # 日志服务实现
add_subdirectory(src/plugin_config_manager) # 配置服务

add_subdirectory(src/interfaces_profiler) # 性能分析工具
add_subdirectory(src/plugin_profiler)

# 5.3 演示/示例代码 (Demo & Examples)
add_subdirectory(src/interfaces_demo)
add_subdirectory(src/plugin_demo_core_services)
add_subdirectory(src/plugin_demo_runner)
add_subdirectory(src/plugin_demo_module_core)
add_subdirectory(src/plugin_demo_module_events)
add_subdirectory(src/plugin_demo_module_introspection)

# 5.4 辅助工具
add_subdirectory(tools/tool_log_benchmark)  # 日志性能压测工具

# 5.5 宿主程序
add_subdirectory(src/host_console_demo)   # 控制台宿主示例

# ==============================================================================
# 6. 单元测试与集成测试 (Google Test)
# ==============================================================================
# [设计思想]
# 引入 Google Test (GTest) 框架以支持自动化的功能验证。
# 这与 tool_log_benchmark (压测) 互为补充：GTest 负责功能的正确性，压测负责性能的极限。

# [重要] 强制 GTest 使用共享运行时库 (/MD)
# GTest 默认在 Windows 上可能使用静态运行时 (/MT)。如果这里不强制覆盖，
# 在链接阶段会与我们的框架 (使用 /MD) 发生严重冲突 (LNK2038 错误)。
set(gtest_force_shared_crt ON CACHE BOOL "Force gtest to use the shared C runtime" FORCE)

# 引入 GTest 源码
# 假设用户已将 GTest 解压至 third_party/googletest-1.17.0
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/googletest-1.17.0/CMakeLists.txt")
    add_subdirectory(third_party/googletest-1.17.0)
    
    # 开启 CTest 测试支持
    # 这会在构建目录生成测试配置文件，允许通过 `ctest` 命令运行所有测试。
    enable_testing()

    # 添加测试工程根目录
    # 这里包含了基于 Fixture 的分层测试架构 (tests/common, tests/integration)
    add_subdirectory(tests)
else()
    message(WARNING "GoogleTest not found in third_party/googletest-1.17.0. Unit tests will be skipped.")
endif()

# ==============================================================================
# 7. SDK 生成与导出
# ==============================================================================

# 生成并安装 CMake 配置文件
# 这使得第三方开发者可以在他们的 CMakeLists.txt 中使用：
# find_package(z3y_plugin_framework REQUIRED)
install(
  EXPORT z3y_plugin_frameworkTargets
  FILE z3y_plugin_frameworkTargets.cmake
  NAMESPACE z3y_plugin_framework::  # 命名空间前缀
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/z3y_plugin_framework
)

# (可选) 生成版本配置文件 (z3y_plugin_frameworkConfigVersion.cmake)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/z3y_plugin_frameworkConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(
    FILES "${CMAKE_CURRENT_BINARY_DIR}/z3y_plugin_frameworkConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/z3y_plugin_framework
)

# 将第三方库的头文件拷贝到 SDK 的 include 目录
install(
    DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/third_party/spdlog-1.16.0/include/spdlog
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(
    DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/third_party/nlohmann_json-3.12.0/include/nlohmann
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)