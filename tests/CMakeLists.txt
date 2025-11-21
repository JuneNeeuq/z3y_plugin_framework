#
# CMakeLists.txt (tests)
#

# 1. 首先定义目标 (Executable)
#    必须在 target_include_directories 之前调用！
add_executable(z3y_integration_tests
    integration/test_spdlog_plugin.cpp
    # 以后有新测试，直接加在这里
    # integration/test_network_plugin.cpp 
 "integration/test_config_plugin.cpp")

# 2. 注入构建环境信息 (关键步骤)
#    这允许 C++ 代码知道当前的架构后缀 (_x64/_x86) 和构建模式 (Debug/Release)
target_compile_definitions(z3y_integration_tests PRIVATE 
    # 传入架构后缀 (例如 "_x64")，变量来自根目录 CMakeLists.txt
    Z3Y_ARCH_SUFFIX="${Z3Y_ARCH_SUFFIX}"
    
    # 传入 Debug 标志
    # 使用生成器表达式：仅在 Debug 配置下定义 Z3Y_IS_DEBUG_BUILD 宏
    $<$<CONFIG:Debug>:Z3Y_IS_DEBUG_BUILD>
)

# 3. 然后再配置包含路径
#    将当前目录 (tests/) 添加到包含路径，以便源码中使用 #include "common/plugin_test_base.h"
target_include_directories(z3y_integration_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# 4. 链接依赖
target_link_libraries(z3y_integration_tests
    PRIVATE
    GTest::gtest_main      # GTest 框架
    z3y_plugin_manager     # 被测核心
    interfaces_core        # 被测接口
    interfaces_demo        # Demo 接口 (如果需要)
)

# 5. [重要] 将测试程序安装到 bin 目录
#    这样它才能找到同目录下的 plugin_spdlog_logger.dll
install(
    TARGETS z3y_integration_tests
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# 6. 注册到 CTest
#    注意：WORKING_DIRECTORY 设为 bin 目录，确保能加载 DLL
add_test(
    NAME IntegrationTests 
    COMMAND z3y_integration_tests
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)