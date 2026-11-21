find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK3 REQUIRED IMPORTED_TARGET gtk+-3.0)

if(APPTRAVERSE_BUILD_LEGACY_EXAMPLES)
  add_executable(linux_window_catalog surface_ui_main.cpp ../common/surface_ui_session.cpp)
  add_executable(linux_window_catalog_load_only surface_ui_main.cpp ../common/surface_ui_session.cpp)
  set(_surface_supply_dir "${CMAKE_CURRENT_BINARY_DIR}/generated_distilled/linux_window_catalog")
  set(_surface_supply "${_surface_supply_dir}/embedded_surface_ui_supply.h")
  file(MAKE_DIRECTORY "${_surface_supply_dir}")
  if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_sources(linux_window_catalog PRIVATE ../common/surface_ui_supply.cpp)
    target_compile_definitions(linux_window_catalog PRIVATE APPTRAVERSE_SURFACE_UI_DEBUG_BOOTSTRAP)
    add_custom_command(OUTPUT "${_surface_supply}"
      COMMAND $<TARGET_FILE:linux_window_catalog> --distill-only "${_surface_supply_dir}"
      DEPENDS linux_window_catalog VERBATIM)
  else()
    set(APPTRAVERSE_SURFACE_UI_DEBUG_SUPPLY_HEADER "" CACHE FILEPATH "Surface S0 supply prepared by Debug")
    if(NOT EXISTS "${APPTRAVERSE_SURFACE_UI_DEBUG_SUPPLY_HEADER}")
      message(FATAL_ERROR "Provide matching Debug Surface supply through APPTRAVERSE_SURFACE_UI_DEBUG_SUPPLY_HEADER")
    endif()
    add_custom_command(OUTPUT "${_surface_supply}"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different "${APPTRAVERSE_SURFACE_UI_DEBUG_SUPPLY_HEADER}" "${_surface_supply}"
      DEPENDS "${APPTRAVERSE_SURFACE_UI_DEBUG_SUPPLY_HEADER}" VERBATIM)
    target_sources(linux_window_catalog PRIVATE "${_surface_supply}")
    target_include_directories(linux_window_catalog PRIVATE "${_surface_supply_dir}")
  endif()
  target_sources(linux_window_catalog_load_only PRIVATE "${_surface_supply}")
  target_include_directories(linux_window_catalog_load_only PRIVATE "${_surface_supply_dir}")
  foreach(_surface_target linux_window_catalog linux_window_catalog_load_only)
    target_link_libraries(${_surface_target} PRIVATE surface_generic_ui apptraverse_ui_linux PkgConfig::GTK3)
  endforeach()
endif()

# Debug prepares a fresh graph; consumers embed the supply prepared by Debug.
add_library(window_catalog_window_catalog_linux STATIC
  window_catalog_graph_linux.cpp
  window_catalog_graph_builder_linux.cpp
  window_catalog_linux_application.cpp
  window_catalog_linux_desktop_zorder.cpp
)
target_include_directories(window_catalog_window_catalog_linux PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/../common)
target_link_libraries(window_catalog_window_catalog_linux PUBLIC
  window_catalog_window_catalog_common apptraverse_ui_linux)
target_compile_features(window_catalog_window_catalog_linux PUBLIC cxx_std_20)

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
    "${PROJECT_SOURCE_DIR}/build/linux-x64-ninja-clang-debug/examples/window_catalog/generated_distilled/embedded_distilled_window_catalog_supply.h"
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
  target_link_libraries(${_catalog_demo} PRIVATE window_catalog_window_catalog_linux)
  target_include_directories(${_catalog_demo} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../common)
endforeach()

include(${PROJECT_SOURCE_DIR}/cmake/apptraverse_catalog_binary_contract.cmake)
foreach(_catalog_demo linux_window_catalog_demo linux_window_catalog_demo_load_only)
  apptraverse_catalog_binary_contract(${_catalog_demo})
endforeach()
