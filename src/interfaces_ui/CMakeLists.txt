# 这是一个纯 INTERFACE 库，不产生实体二进制，只提供头文件契约
add_library(interfaces_ui INTERFACE)
target_include_directories(interfaces_ui INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
# 依赖核心框架的接口 (为了拿到 config_types 等)
target_link_libraries(interfaces_ui INTERFACE z3y_plugin_manager)