find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK3 REQUIRED IMPORTED_TARGET gtk+-3.0)

set(_surfaces_linux_sources
  main.cpp
  linux_app.cpp
  linux_presenters.cpp
  linux_fatal.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../common/surfaces_lifecycle.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../common/surfaces_shutdown_trace.cpp
)

add_executable(linux_surfaces_demo ${_surfaces_linux_sources})
target_compile_definitions(linux_surfaces_demo PRIVATE
  APPTRAVERSE_ENABLE_DISTILLATION
)
target_link_libraries(linux_surfaces_demo PRIVATE
  surfaces_demo_distill
)

add_executable(linux_surfaces_demo_load_only ${_surfaces_linux_sources})
# Load-only: model + desktop/linux presenters — no distill bootstrap.

foreach(_surfaces_demo linux_surfaces_demo linux_surfaces_demo_load_only)
  target_link_libraries(${_surfaces_demo} PRIVATE
    apptraverse_surfaces
    PkgConfig::GTK3
  )
  target_include_directories(${_surfaces_demo} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../common
  )
endforeach()

# WindowCatalog GTK consumer (embedded BACKING + USER overlay).
include(${CMAKE_SOURCE_DIR}/cmake/apptraverse_embed_distilled_supply.cmake)

add_library(surfaces_demo_window_catalog_linux STATIC
  window_catalog_graph_linux.cpp
)
target_include_directories(surfaces_demo_window_catalog_linux PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/../common
)
target_link_libraries(surfaces_demo_window_catalog_linux PUBLIC
  surfaces_demo_window_catalog_common
  apptraverse_ui_linux
)
target_compile_features(surfaces_demo_window_catalog_linux PUBLIC cxx_std_20)

set(_window_catalog_linux_embed_graph_sources
  ${CMAKE_CURRENT_SOURCE_DIR}/window_catalog_graph_linux.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../common/window_catalog_graph_builder.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../common/window_catalog_model.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../common/window_catalog_chrome_layout.cpp
)

add_executable(apptraverse_export_window_catalog_distilled_graph_linux
  window_catalog_export_linux.cpp
)
target_link_libraries(apptraverse_export_window_catalog_distilled_graph_linux PRIVATE
  surfaces_demo_window_catalog_linux
  apptraverse
)
target_include_directories(apptraverse_export_window_catalog_distilled_graph_linux PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/../common
)

set(_window_catalog_linux_sources
  window_catalog_main.cpp
  window_catalog_linux_desktop_zorder.cpp
  window_catalog_linux_session_automation.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../common/window_catalog_lifecycle.cpp
)

add_executable(linux_window_catalog_demo ${_window_catalog_linux_sources})
target_compile_definitions(linux_window_catalog_demo PRIVATE
  APPTRAVERSE_WINDOW_CATALOG_USE_EMBEDDED_DISTILL
)
apptraverse_embed_distilled_supply(
  linux_window_catalog_demo
  apptraverse_export_window_catalog_distilled_graph_linux
  embedded_distilled_window_catalog_supply.h
  ${_window_catalog_linux_embed_graph_sources})
add_dependencies(linux_window_catalog_demo
  apptraverse_export_window_catalog_distilled_graph_linux)
target_link_libraries(linux_window_catalog_demo PRIVATE
  surfaces_demo_window_catalog_linux
  apptraverse_ui_linux
  PkgConfig::GTK3
)
target_include_directories(linux_window_catalog_demo PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/../common
)

if(BUILD_TESTING)
  add_custom_target(apptraverse_linux_window_catalog_smoke_check
    COMMAND python3
            ${CMAKE_SOURCE_DIR}/tools/linux_window_catalog_l3_smoke.py
            $<TARGET_FILE:linux_window_catalog_demo>
    DEPENDS linux_window_catalog_demo
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Linux WindowCatalog embedded USER cold-load smoke"
  )
endif()
