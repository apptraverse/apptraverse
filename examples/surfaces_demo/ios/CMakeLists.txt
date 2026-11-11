if(APPTRAVERSE_BUILD_LEGACY_EXAMPLES)
enable_language(OBJCXX)

set(CMAKE_OBJCXX_STANDARD 20)
set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)

# SwiftUI content layer. Swift cannot share a target with ObjC++. swiftc needs
# the SDK spelled out (CMake leaves CMAKE_Swift_COMPILE_OPTIONS_SYSROOT unset)
# and, for the simulator, an explicit triple — the SDK alone would build for the
# host macOS.
if(NOT CMAKE_Swift_COMPILER)
  execute_process(
    COMMAND xcrun -f swiftc
    OUTPUT_VARIABLE _swiftc_path
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )
  set(CMAKE_Swift_COMPILER ${_swiftc_path})
endif()
set(CMAKE_Swift_FLAGS "-sdk ${CMAKE_OSX_SYSROOT}")
set(CMAKE_Swift_COMPILER_TARGET
  ${CMAKE_OSX_ARCHITECTURES}-apple-ios${CMAKE_OSX_DEPLOYMENT_TARGET}-simulator
)
enable_language(Swift)

add_library(surfaces_demo_ios_content STATIC SurfaceContentView.swift)
set_target_properties(surfaces_demo_ios_content PROPERTIES
  Swift_MODULE_NAME surfaces_demo_ios_content
)
# Swift sees only the pure Objective-C actions protocol, never this project's C++.
target_compile_options(surfaces_demo_ios_content PRIVATE
  "SHELL:-import-objc-header ${CMAKE_CURRENT_SOURCE_DIR}/ios_surface_actions.h"
)

# Holds the Swift compatibility archives the autolinked Swift objects reference.
get_filename_component(_swift_bin_dir ${CMAKE_Swift_COMPILER} DIRECTORY)
set(_swift_runtime_dir ${_swift_bin_dir}/../lib/swift/iphonesimulator)

set(_surfaces_ios_sources
  main.mm
  ios_app.mm
  ios_presenters.mm
  ${CMAKE_CURRENT_SOURCE_DIR}/../common/surfaces_lifecycle.cpp
)

# ARC for UIKit ownership in .mm; C++ RTTI stays off via apptraverse_compile_policy.
set_source_files_properties(
  main.mm ios_app.mm ios_presenters.mm
  PROPERTIES COMPILE_FLAGS "-fobjc-arc"
)

add_executable(ios_surfaces_demo ${_surfaces_ios_sources})
target_compile_definitions(ios_surfaces_demo PRIVATE
  APPTRAVERSE_ENABLE_DISTILLATION
)
target_link_libraries(ios_surfaces_demo PRIVATE
  surfaces_demo_distill
)

add_executable(ios_surfaces_demo_load_only ${_surfaces_ios_sources})
# Load-only: model + mobile/iOS presenters — no distill bootstrap.

foreach(_surfaces_demo ios_surfaces_demo ios_surfaces_demo_load_only)
  target_link_libraries(${_surfaces_demo} PRIVATE
    apptraverse_surfaces
    surfaces_demo_mobile
    surfaces_demo_ios_content
    "-framework UIKit"
    "-framework Foundation"
    "-framework CoreGraphics"
  )
  target_include_directories(${_surfaces_demo} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../common
    ${CMAKE_CURRENT_SOURCE_DIR}/../mobile
  )
  # Ensure ObjC++ TUs also get -fno-rtti (policy is on the C++ link interface).
  target_compile_options(${_surfaces_demo} PRIVATE
    $<$<COMPILE_LANGUAGE:OBJCXX>:-fno-rtti>
  )
  # Link with clang++, not swiftc: the Swift driver rejects the C++ policy
  # flags. Swift runtime comes from the autolink entries in the Swift objects.
  target_link_options(${_surfaces_demo} PRIVATE
    -L${CMAKE_OSX_SYSROOT}/usr/lib/swift
    -L${_swift_runtime_dir}
    -Wl,-rpath,/usr/lib/swift
  )
  set_target_properties(${_surfaces_demo} PROPERTIES
    LINKER_LANGUAGE OBJCXX
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in
    MACOSX_BUNDLE_BUNDLE_NAME ${_surfaces_demo}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
  )
endforeach()

set_target_properties(ios_surfaces_demo PROPERTIES
  MACOSX_BUNDLE_GUI_IDENTIFIER com.apptraverse.surfaces
)
set_target_properties(ios_surfaces_demo_load_only PROPERTIES
  MACOSX_BUNDLE_GUI_IDENTIFIER com.apptraverse.surfaces.loadonly
)

endif()

# WindowCatalog: thin mobile graph adapters over the shared application model.
add_library(surfaces_demo_window_catalog_ios STATIC window_catalog_graph_ios.cpp)
target_include_directories(surfaces_demo_window_catalog_ios PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/../common)
target_link_libraries(surfaces_demo_window_catalog_ios PUBLIC
  surfaces_demo_window_catalog_common apptraverse_ui_ios_presenters)

add_library(surfaces_demo_window_catalog_ios_builder STATIC
  window_catalog_graph_builder_ios.cpp)
target_link_libraries(surfaces_demo_window_catalog_ios_builder PUBLIC
  surfaces_demo_window_catalog_ios)

set_source_files_properties(window_catalog_composition.mm PROPERTIES
  COMPILE_FLAGS "-fobjc-arc")
foreach(_composition window_catalog_ios_composition window_catalog_ios_composition_load_only)
  add_library(${_composition} STATIC window_catalog_composition.mm)
  target_include_directories(${_composition} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(${_composition} PUBLIC
    surfaces_demo_window_catalog_ios apptraverse_ui_ios_bridge)
  target_compile_options(${_composition} PRIVATE
    $<$<COMPILE_LANGUAGE:OBJCXX>:-fno-rtti>
    $<$<COMPILE_LANGUAGE:OBJCXX>:-fno-objc-exceptions>)
endforeach()
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  target_compile_definitions(window_catalog_ios_composition PRIVATE
    APPTRAVERSE_WINDOW_CATALOG_DEBUG_BOOTSTRAP)
  target_sources(window_catalog_ios_composition PRIVATE
    ../common/window_catalog_distillation.cpp)
  target_link_libraries(window_catalog_ios_composition PRIVATE
    surfaces_demo_window_catalog_ios_builder)
else()
  target_compile_definitions(window_catalog_ios_composition PRIVATE
    APPTRAVERSE_WINDOW_CATALOG_USE_EMBEDDED_DISTILL)
endif()
target_compile_definitions(window_catalog_ios_composition_load_only PRIVATE
  APPTRAVERSE_WINDOW_CATALOG_USE_EMBEDDED_DISTILL)

# A macOS Debug preparation executable builds the SAME iOS graph before the
# simulator runtime embeds it. Runtime consumers never link these builders.
set(APPTRAVERSE_WINDOW_CATALOG_IOS_HOST_EXPORTER "" CACHE FILEPATH
  "macOS-built apptraverse_export_window_catalog_ios executable")
set(APPTRAVERSE_WINDOW_CATALOG_IOS_DEBUG_SUPPLY_HEADER "" CACHE FILEPATH
  "Existing iOS supply header prepared by the Debug host application")
set(_catalog_ios_embed_dir "${CMAKE_CURRENT_BINARY_DIR}/generated_distilled")
set(_catalog_ios_embed_header
  "${_catalog_ios_embed_dir}/embedded_distilled_window_catalog_supply.h")
file(MAKE_DIRECTORY "${_catalog_ios_embed_dir}")
if(APPTRAVERSE_WINDOW_CATALOG_IOS_HOST_EXPORTER)
  add_custom_command(OUTPUT "${_catalog_ios_embed_header}"
    COMMAND "${APPTRAVERSE_WINDOW_CATALOG_IOS_HOST_EXPORTER}" "${_catalog_ios_embed_dir}"
    DEPENDS "${APPTRAVERSE_WINDOW_CATALOG_IOS_HOST_EXPORTER}"
    VERBATIM COMMENT "Prepare iOS WindowCatalog supply using the Debug host application")
elseif(EXISTS "${APPTRAVERSE_WINDOW_CATALOG_IOS_DEBUG_SUPPLY_HEADER}")
  add_custom_command(OUTPUT "${_catalog_ios_embed_header}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${APPTRAVERSE_WINDOW_CATALOG_IOS_DEBUG_SUPPLY_HEADER}"
      "${_catalog_ios_embed_header}"
    DEPENDS "${APPTRAVERSE_WINDOW_CATALOG_IOS_DEBUG_SUPPLY_HEADER}" VERBATIM)
else()
  message(FATAL_ERROR "Set APPTRAVERSE_WINDOW_CATALOG_IOS_HOST_EXPORTER or APPTRAVERSE_WINDOW_CATALOG_IOS_DEBUG_SUPPLY_HEADER to the iOS supply prepared on the macOS Debug host.")
endif()
add_custom_target(apptraverse_window_catalog_ios_embedded_supply
  DEPENDS "${_catalog_ios_embed_header}")
foreach(_composition window_catalog_ios_composition window_catalog_ios_composition_load_only)
  if(NOT (CMAKE_BUILD_TYPE STREQUAL "Debug" AND _composition STREQUAL "window_catalog_ios_composition"))
    target_sources(${_composition} PRIVATE "${_catalog_ios_embed_header}")
    target_include_directories(${_composition} PRIVATE "${_catalog_ios_embed_dir}")
    add_dependencies(${_composition} apptraverse_window_catalog_ios_embedded_supply)
  endif()
endforeach()

add_library(window_catalog_ios_entry STATIC window_catalog_main.swift)
set_target_properties(window_catalog_ios_entry PROPERTIES Swift_MODULE_NAME WindowCatalogIosEntry)
target_link_libraries(window_catalog_ios_entry PRIVATE apptraverse_ui_ios_swift)
target_compile_options(window_catalog_ios_entry PRIVATE
  "SHELL:-import-objc-header ${CMAKE_CURRENT_SOURCE_DIR}/window_catalog_composition.h"
  "SHELL:-Xcc -I${PROJECT_SOURCE_DIR}/include")

add_executable(ios_window_catalog_demo MACOSX_BUNDLE window_catalog_link.mm)
add_executable(ios_window_catalog_demo_load_only MACOSX_BUNDLE window_catalog_link.mm)
target_link_libraries(ios_window_catalog_demo PRIVATE window_catalog_ios_composition)
target_link_libraries(ios_window_catalog_demo_load_only PRIVATE window_catalog_ios_composition_load_only)
foreach(_app ios_window_catalog_demo ios_window_catalog_demo_load_only)
  target_link_libraries(${_app} PRIVATE window_catalog_ios_entry apptraverse_ui_ios)
  target_compile_options(${_app} PRIVATE $<$<COMPILE_LANGUAGE:OBJCXX>:-fno-rtti>)
  set_target_properties(${_app} PROPERTIES
    LINKER_LANGUAGE OBJCXX
    MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/window_catalog_Info.plist.in
    MACOSX_BUNDLE_BUNDLE_NAME "WindowCatalog"
    MACOSX_BUNDLE_GUI_IDENTIFIER "org.apptraverse.WindowCatalog.ios"
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION})
endforeach()
set_target_properties(ios_window_catalog_demo_load_only PROPERTIES
  MACOSX_BUNDLE_GUI_IDENTIFIER "org.apptraverse.WindowCatalog.ios.loadonly")
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  set_target_properties(ios_window_catalog_demo PROPERTIES
    MACOSX_BUNDLE_BUNDLE_NAME "WindowCatalog Debug")
endif()
