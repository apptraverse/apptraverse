find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK3 REQUIRED IMPORTED_TARGET gtk+-3.0)

set(_minimal_app_linux_sources
  minimal_app_main.cpp
  minimal_app_lifecycle.cpp
  minimal_app_graph_linux.cpp
)

add_executable(linux_minimal_app ${_minimal_app_linux_sources})
target_compile_definitions(linux_minimal_app PRIVATE
  APPTRAVERSE_ENABLE_DISTILLATION
)
target_link_libraries(linux_minimal_app PRIVATE
  minimal_app_common
  apptraverse_ui_linux
  PkgConfig::GTK3
)
target_include_directories(linux_minimal_app PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/../common
)
