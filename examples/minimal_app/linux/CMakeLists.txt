include(${CMAKE_SOURCE_DIR}/cmake/apptraverse_embed_distilled_supply.cmake)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK3 REQUIRED IMPORTED_TARGET gtk+-3.0)

set(_minimal_linux_embed_graph_sources
  ${CMAKE_CURRENT_SOURCE_DIR}/minimal_app_graph_linux.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../common/minimal_application.cpp)

set(_minimal_app_linux_sources
  minimal_app_main.cpp
  minimal_app_lifecycle.cpp
  minimal_app_linux_session_automation.cpp
)

add_executable(apptraverse_export_minimal_distilled_graph_linux
  minimal_app_export_linux.cpp
  minimal_app_graph_linux.cpp
)
target_link_libraries(apptraverse_export_minimal_distilled_graph_linux PRIVATE
  minimal_app_common
  apptraverse
  apptraverse_ui_linux
)
target_include_directories(apptraverse_export_minimal_distilled_graph_linux PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/../common
)

add_executable(linux_minimal_app ${_minimal_app_linux_sources})
target_compile_definitions(linux_minimal_app PRIVATE
  APPTRAVERSE_MINIMAL_APP_USE_EMBEDDED_DISTILL
)
apptraverse_embed_distilled_supply(
  linux_minimal_app
  apptraverse_export_minimal_distilled_graph_linux
  embedded_distilled_minimal_app_supply.h
  ${_minimal_linux_embed_graph_sources})
add_dependencies(linux_minimal_app apptraverse_export_minimal_distilled_graph_linux)
target_link_libraries(linux_minimal_app PRIVATE
  minimal_app_common
  apptraverse_ui_linux
  PkgConfig::GTK3
)
target_include_directories(linux_minimal_app PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/../common
)
