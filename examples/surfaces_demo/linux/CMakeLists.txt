find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK3 REQUIRED IMPORTED_TARGET gtk+-3.0)

if(APPTRAVERSE_BUILD_LEGACY_EXAMPLES)
set(_surfaces_linux_sources
  main.cpp
  linux_app.cpp
  linux_presenters.cpp
  linux_fatal.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../common/surfaces_lifecycle.cpp
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
endif()

# Debug prepares a fresh graph; consumers embed the supply prepared by Debug.
add_library(surfaces_demo_window_catalog_linux STATIC
  window_catalog_graph_linux.cpp
  window_catalog_graph_builder_linux.cpp
  window_catalog_linux_application.cpp
  window_catalog_linux_desktop_zorder.cpp
)
target_include_directories(surfaces_demo_window_catalog_linux PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/../common)
target_link_libraries(surfaces_demo_window_catalog_linux PUBLIC
  surfaces_demo_window_catalog_common apptraverse_ui_linux)
target_compile_features(surfaces_demo_window_catalog_linux PUBLIC cxx_std_20)

set(_window_catalog_linux_sources
  window_catalog_main.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../common/window_catalog_lifecycle.cpp)

add_executable(linux_window_catalog_demo ${_window_catalog_linux_sources})
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  target_compile_definitions(linux_window_catalog_demo PRIVATE
    APPTRAVERSE_WINDOW_CATALOG_DEBUG_BOOTSTRAP)
  target_sources(linux_window_catalog_demo PRIVATE
    ../common/window_catalog_distillation.cpp)
else()
  target_compile_definitions(linux_window_catalog_demo PRIVATE
    APPTRAVERSE_WINDOW_CATALOG_USE_EMBEDDED_DISTILL)
endif()

add_executable(linux_window_catalog_demo_load_only ${_window_catalog_linux_sources})
target_compile_definitions(linux_window_catalog_demo_load_only PRIVATE
  APPTRAVERSE_WINDOW_CATALOG_USE_EMBEDDED_DISTILL
  APPTRAVERSE_WINDOW_CATALOG_LOAD_ONLY)

set(_window_catalog_embed_dir "${CMAKE_CURRENT_BINARY_DIR}/../generated_distilled")
set(_window_catalog_embed_header
  "${_window_catalog_embed_dir}/embedded_distilled_window_catalog_supply.h")
file(MAKE_DIRECTORY "${_window_catalog_embed_dir}")
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  add_custom_command(OUTPUT "${_window_catalog_embed_header}"
    COMMAND $<TARGET_FILE:linux_window_catalog_demo>
            --distill-only "${_window_catalog_embed_dir}"
    DEPENDS linux_window_catalog_demo
    VERBATIM COMMENT "Prepare Linux WindowCatalog supply with the Debug application")
else()
  set(APPTRAVERSE_WINDOW_CATALOG_DEBUG_SUPPLY_HEADER
    "${PROJECT_SOURCE_DIR}/build/linux-x64-ninja-clang-debug/examples/surfaces_demo/generated_distilled/embedded_distilled_window_catalog_supply.h"
    CACHE FILEPATH "Linux WindowCatalog supply prepared by the Debug application")
  if(NOT EXISTS "${APPTRAVERSE_WINDOW_CATALOG_DEBUG_SUPPLY_HEADER}")
    message(FATAL_ERROR
      "Build Debug linux_window_catalog_demo_load_only first, or set APPTRAVERSE_WINDOW_CATALOG_DEBUG_SUPPLY_HEADER to the Linux header prepared by Debug --distill-only.")
  endif()
  add_custom_command(OUTPUT "${_window_catalog_embed_header}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${APPTRAVERSE_WINDOW_CATALOG_DEBUG_SUPPLY_HEADER}"
            "${_window_catalog_embed_header}"
    DEPENDS "${APPTRAVERSE_WINDOW_CATALOG_DEBUG_SUPPLY_HEADER}"
    VERBATIM COMMENT "Embed the Linux WindowCatalog supply prepared by Debug")
endif()
add_custom_target(apptraverse_window_catalog_embedded_supply
  DEPENDS "${_window_catalog_embed_header}")
set(APPTRAVERSE_WINDOW_CATALOG_EMBED_INCLUDE_DIR
    "${_window_catalog_embed_dir}" CACHE INTERNAL "")

foreach(_catalog_demo linux_window_catalog_demo linux_window_catalog_demo_load_only)
  if(NOT (CMAKE_BUILD_TYPE STREQUAL "Debug" AND _catalog_demo STREQUAL "linux_window_catalog_demo"))
    target_sources(${_catalog_demo} PRIVATE "${_window_catalog_embed_header}")
    target_include_directories(${_catalog_demo} PRIVATE "${_window_catalog_embed_dir}")
    add_dependencies(${_catalog_demo} apptraverse_window_catalog_embedded_supply)
  endif()
  target_link_libraries(${_catalog_demo} PRIVATE surfaces_demo_window_catalog_linux)
  target_include_directories(${_catalog_demo} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../common)
endforeach()
