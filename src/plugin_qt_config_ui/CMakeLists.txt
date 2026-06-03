if(NOT Z3Y_BUILD_QT_UI)
    message(STATUS "[z3y] Qt UI plugin is disabled via Z3Y_BUILD_QT_UI=OFF. Skipping.")
    return()
endif()

find_package(Qt6 COMPONENTS Core Widgets Gui REQUIRED)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

set(PLUGIN_SRCS
    plugin_entry.cpp
    config_ui_manager_service.cpp
    event_bridge.cpp
    config_main_window.cpp
    dependency_graph.cpp
    widget_factory.cpp
    scroll_guard.h
    z3y_config_ui.qrc
)

add_library(plugin_qt_config_ui SHARED ${PLUGIN_SRCS})

set_target_properties(plugin_qt_config_ui PROPERTIES OUTPUT_NAME "plugin_qt_config_ui${Z3Y_ARCH_SUFFIX}")

target_link_libraries(plugin_qt_config_ui PRIVATE
    Qt6::Core
    Qt6::Widgets
    Qt6::Gui
    z3y_plugin_manager
    interfaces_core
    interfaces_ui
)

if(Z3Y_ENABLE_INSTALL)
    include(GNUInstallDirs)
    install(TARGETS 
            plugin_qt_config_ui
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    )
endif()