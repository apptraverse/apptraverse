if(APPTRAVERSE_BUILD_LEGACY_EXAMPLES)
enable_language(OBJCXX)

set(CMAKE_OBJCXX_STANDARD 20)
set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)

# SwiftUI content layer. Swift cannot share a target with ObjC++, and swiftc
# needs the SDK passed explicitly (CMAKE_OSX_SYSROOT alone is not forwarded).
if(NOT CMAKE_Swift_COMPILER)
  execute_process(
    COMMAND xcrun -f swiftc
    OUTPUT_VARIABLE _swiftc_path
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )
  set(CMAKE_Swift_COMPILER ${_swiftc_path})
endif()
set(CMAKE_Swift_FLAGS "-sdk ${CMAKE_OSX_SYSROOT}")
enable_language(Swift)

add_library(surfaces_demo_macos_content STATIC SurfaceContentView.swift)
set_target_properties(surfaces_demo_macos_content PROPERTIES
  Swift_MODULE_NAME surfaces_demo_macos_content
)
# Swift sees only the pure Objective-C actions protocol, never this project's C++.
target_compile_options(surfaces_demo_macos_content PRIVATE
  "SHELL:-import-objc-header ${CMAKE_CURRENT_SOURCE_DIR}/mac_surface_actions.h"
)

set(_surfaces_mac_sources
  main.mm
  mac_app.mm
  mac_presenters.mm
  ${CMAKE_CURRENT_SOURCE_DIR}/../common/surfaces_lifecycle.cpp
)

# ARC for AppKit ownership in .mm; C++ RTTI stays off via apptraverse_compile_policy.
set_source_files_properties(
  main.mm mac_app.mm mac_presenters.mm
  PROPERTIES COMPILE_FLAGS "-fobjc-arc"
)

add_executable(macos_surfaces_demo ${_surfaces_mac_sources})
target_compile_definitions(macos_surfaces_demo PRIVATE
  APPTRAVERSE_ENABLE_DISTILLATION
)
target_link_libraries(macos_surfaces_demo PRIVATE
  surfaces_demo_distill
)

add_executable(macos_surfaces_demo_load_only ${_surfaces_mac_sources})
# Load-only: model + desktop/mac presenters — no distill bootstrap.

foreach(_surfaces_demo macos_surfaces_demo macos_surfaces_demo_load_only)
  target_link_libraries(${_surfaces_demo} PRIVATE
    apptraverse_surfaces
    surfaces_demo_macos_content
    "-framework AppKit"
    "-framework Foundation"
  )
  target_include_directories(${_surfaces_demo} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../common
  )
  # Ensure ObjC++ TUs also get -fno-rtti (policy is on the C++ link interface).
  target_compile_options(${_surfaces_demo} PRIVATE
    $<$<COMPILE_LANGUAGE:OBJCXX>:-fno-rtti>
  )
  # Link with clang++, not swiftc: the Swift driver rejects the C++ policy
  # flags. Swift runtime comes from the autolink entries in the Swift objects.
  set_target_properties(${_surfaces_demo} PROPERTIES LINKER_LANGUAGE OBJCXX)
  target_link_options(${_surfaces_demo} PRIVATE
    -L${CMAKE_OSX_SYSROOT}/usr/lib/swift
    -L/usr/lib/swift
    -Wl,-rpath,/usr/lib/swift
  )
endforeach()

endif()

# The application composes the common model/template with the macOS backend.
add_library(surfaces_demo_window_catalog_macos STATIC
  window_catalog_graph_macos.cpp)
target_include_directories(surfaces_demo_window_catalog_macos PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/../common)
target_link_libraries(surfaces_demo_window_catalog_macos PUBLIC
  surfaces_demo_window_catalog_common apptraverse_ui_macos_presenters)

# Builders are linked only by the Debug preparation application.
add_library(surfaces_demo_window_catalog_macos_builder STATIC
  window_catalog_graph_builder_macos.cpp)
target_link_libraries(surfaces_demo_window_catalog_macos_builder PUBLIC
  surfaces_demo_window_catalog_macos)

add_library(window_catalog_macos_entry STATIC window_catalog_main.swift)
set_target_properties(window_catalog_macos_entry PROPERTIES
  Swift_MODULE_NAME WindowCatalogMacEntry)
target_link_libraries(window_catalog_macos_entry PRIVATE apptraverse_ui_macos_swift)
target_compile_options(window_catalog_macos_entry PRIVATE
  "SHELL:-import-objc-header ${CMAKE_CURRENT_SOURCE_DIR}/window_catalog_composition.h"
  "SHELL:-Xcc -I${PROJECT_SOURCE_DIR}/include")

set_source_files_properties(window_catalog_composition.mm PROPERTIES
  COMPILE_FLAGS "-fobjc-arc")
foreach(_composition window_catalog_macos_composition window_catalog_macos_composition_load_only)
  add_library(${_composition} STATIC window_catalog_composition.mm
    ../common/window_catalog_lifecycle.cpp)
  target_include_directories(${_composition} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(${_composition} PUBLIC surfaces_demo_window_catalog_macos
    apptraverse_ui_macos_bridge)
  target_compile_options(${_composition} PRIVATE
    $<$<COMPILE_LANGUAGE:OBJCXX>:-fno-rtti>
    $<$<COMPILE_LANGUAGE:OBJCXX>:-fno-objc-exceptions>)
endforeach()

add_executable(macos_window_catalog_demo MACOSX_BUNDLE window_catalog_link.mm)
set_target_properties(macos_window_catalog_demo PROPERTIES
  MACOSX_BUNDLE_BUNDLE_NAME "WindowCatalog"
  MACOSX_BUNDLE_GUI_IDENTIFIER "org.apptraverse.WindowCatalog"
  MACOSX_BUNDLE_BUNDLE_VERSION "1"
  MACOSX_BUNDLE_SHORT_VERSION_STRING "0.1")
add_executable(macos_window_catalog_demo_load_only window_catalog_link.mm)
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  target_compile_definitions(window_catalog_macos_composition PRIVATE
    APPTRAVERSE_WINDOW_CATALOG_DEBUG_BOOTSTRAP)
  target_sources(window_catalog_macos_composition PRIVATE ../common/window_catalog_distillation.cpp)
  target_link_libraries(window_catalog_macos_composition PRIVATE
    surfaces_demo_window_catalog_macos_builder)
else()
  target_compile_definitions(window_catalog_macos_composition PRIVATE
    APPTRAVERSE_WINDOW_CATALOG_USE_EMBEDDED_DISTILL)
endif()
target_compile_definitions(window_catalog_macos_composition_load_only PRIVATE
  APPTRAVERSE_WINDOW_CATALOG_USE_EMBEDDED_DISTILL APPTRAVERSE_WINDOW_CATALOG_LOAD_ONLY)

set(_catalog_macos_embed_dir "${CMAKE_CURRENT_BINARY_DIR}/generated_distilled")
set(_catalog_macos_embed_header
  "${_catalog_macos_embed_dir}/embedded_distilled_window_catalog_supply.h")
file(MAKE_DIRECTORY "${_catalog_macos_embed_dir}")
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  add_custom_command(OUTPUT "${_catalog_macos_embed_header}"
    COMMAND $<TARGET_FILE:macos_window_catalog_demo> --distill-only "${_catalog_macos_embed_dir}"
    DEPENDS macos_window_catalog_demo VERBATIM
    COMMENT "Prepare macOS WindowCatalog supply with the Debug application")
else()
  set(APPTRAVERSE_WINDOW_CATALOG_MACOS_DEBUG_SUPPLY_HEADER "" CACHE FILEPATH
    "macOS WindowCatalog supply header prepared by Debug --distill-only")
  if(NOT EXISTS "${APPTRAVERSE_WINDOW_CATALOG_MACOS_DEBUG_SUPPLY_HEADER}")
    message(FATAL_ERROR "Build Debug macos_window_catalog_demo_load_only first; set APPTRAVERSE_WINDOW_CATALOG_MACOS_DEBUG_SUPPLY_HEADER to its generated header.")
  endif()
  add_custom_command(OUTPUT "${_catalog_macos_embed_header}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${APPTRAVERSE_WINDOW_CATALOG_MACOS_DEBUG_SUPPLY_HEADER}"
      "${_catalog_macos_embed_header}"
    DEPENDS "${APPTRAVERSE_WINDOW_CATALOG_MACOS_DEBUG_SUPPLY_HEADER}" VERBATIM)
endif()
add_custom_target(apptraverse_window_catalog_macos_embedded_supply
  DEPENDS "${_catalog_macos_embed_header}")
foreach(_composition window_catalog_macos_composition window_catalog_macos_composition_load_only)
  if(NOT (CMAKE_BUILD_TYPE STREQUAL "Debug" AND _composition STREQUAL "window_catalog_macos_composition"))
    target_sources(${_composition} PRIVATE "${_catalog_macos_embed_header}")
    target_include_directories(${_composition} PRIVATE "${_catalog_macos_embed_dir}")
    add_dependencies(${_composition} apptraverse_window_catalog_macos_embedded_supply)
  endif()
endforeach()
target_link_libraries(macos_window_catalog_demo PRIVATE window_catalog_macos_composition)
target_link_libraries(macos_window_catalog_demo_load_only PRIVATE window_catalog_macos_composition_load_only)
foreach(_catalog_demo macos_window_catalog_demo macos_window_catalog_demo_load_only)
  target_link_libraries(${_catalog_demo} PRIVATE window_catalog_macos_entry apptraverse_ui_macos)
  target_compile_options(${_catalog_demo} PRIVATE $<$<COMPILE_LANGUAGE:OBJCXX>:-fno-rtti>)
  set_target_properties(${_catalog_demo} PROPERTIES LINKER_LANGUAGE OBJCXX)
endforeach()
