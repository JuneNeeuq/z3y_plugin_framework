#
# CMakeLists.txt (src/z3y_plugin_manager)
# @brief 构建 'z3y_plugin_manager' 核心框架库 (DLL/SO)。
# @author Yue Liu
# @date 2025-07-05
# @copyright Copyright (c) 2025 Yue Liu
#
# [受众：框架维护者]
#
# [设计思想]
# 这是 *最核心* 的库，
# 也是所有其他插件和宿主都依赖的库。
# 1. 它被构建为 `SHARED` 库 (DLL/SO)。
#
# 2. [关键] 它定义了 `Z3Y_PLUGIN_MANAGER_AS_DLL` 宏。
#    `z3y_framework_api.h` 头文件会检测这个宏，
#	 并将其所有 API (如 `PluginManager` 类) 标记为 `dllexport` (导出)。
#    (而宿主和插件在 #include 时不会定义此宏，`Z3Y_FRAMEWORK_API` 会自动变为`dllimport`)
#
# 3. 它链接了 `pthread` 和 `dl` (在POSIX 上)，这是动态加载
#    (`dlopen`) 和 `std::thread` 所必需的。
#
# 4. 它安装 (install) 了 `z3y_plugin_manager` 的 .dll, .lib, .so 文件，
#    以及 *所有*`framework` 头文件， 以供 SDK 使用。
#

# 1. 手动列出源文件
set(LIB_SOURCES
  plugin_manager.cpp
  event_bus_impl.cpp
  plugin_manager_pimpl.h
  connection.cpp
)
# 平台特定的源文件 (通过 #ifdef _WIN32 / #if !defined(_WIN32)
# 自动切换)
if (WIN32)
  list(APPEND LIB_SOURCES platform_win.cpp)
else ()
  list(APPEND LIB_SOURCES platform_posix.cpp)
endif ()

# 2. 定义库 (SHARED)
add_library(z3y_plugin_manager SHARED ${LIB_SOURCES})

# [关键]
# 定义一个宏，告诉 z3y_framework_api.h
# 我们正在 *构建* 这个 DLL (使用 dllexport)
target_compile_definitions(
  z3y_plugin_manager
  PRIVATE
  Z3Y_PLUGIN_MANAGER_AS_DLL)

# 3. 设置输出名称 (例如 z3y_plugin_manager_x64d.dll)
set_target_properties(
  z3y_plugin_manager
  PROPERTIES OUTPUT_NAME "z3y_plugin_manager${Z3Y_ARCH_SUFFIX}")

# 4. 设置公共/私有包含目录
target_include_directories(
  z3y_plugin_manager
  # PUBLIC:
  # 任何链接了本库 (z3y_plugin_manager) 的目标，
  # 都会 *自动* 包含项目根目录。
  # 这样 #include "framework/i_component.h" 就能工作。
  PUBLIC
  # 当在本项目中构建时
  $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}>
  # (当被安装为 SDK 并被 find_package
  # 使用时，自动包含 <prefix>/include)
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>

  # PRIVATE: 仅本库编译时需要 (用于 pimpl.h)
  PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${PROJECT_SOURCE_DIR}  # (pimpl.h 也需要 #include "framework/...")
)

# 5. 链接 Linux 依赖 (dl: dlopen/dlsym, pthread: std::thread)
if (NOT WIN32)
  target_link_libraries(z3y_plugin_manager PRIVATE pthread dl)
endif ()

# 6. 安装 (将 z3y_plugin_manager 安装到 SDK)
install(
  TARGETS z3y_plugin_manager
  # 导出目标，供 find_package 使用
  EXPORT z3y_plugin_frameworkTargets
  # .lib (Windows)
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  # .dll (Windows) / .so (Linux)
  # 确保 DLL 也被安装到 bin 目录 (与 host.exe 在一起)
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  # .so (Linux, 当不作为 SHARED 时)
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})

# 7. 安装 'framework' 目录下的 *所有* 头文件
install(
  DIRECTORY ${PROJECT_SOURCE_DIR}/framework/
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/framework  # 安装到
  # "include/framework"
)