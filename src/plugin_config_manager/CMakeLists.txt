set(PLUGIN_SOURCES
  plugin_entry.cpp
  config_provider_service.cpp
  config_provider_service.h
)
add_library(plugin_config_manager SHARED ${PLUGIN_SOURCES})
set_target_properties(plugin_config_manager PROPERTIES OUTPUT_NAME "plugin_config_manager${Z3Y_ARCH_SUFFIX}")
target_link_libraries(plugin_config_manager PRIVATE z3y_plugin_manager interfaces_core nlohmann_json::nlohmann_json)
install(TARGETS plugin_config_manager RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})