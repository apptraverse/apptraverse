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
  ${CMAKE_CURRENT_SOURCE_DIR}/../common/surfaces_shutdown_trace.cpp
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

add_library(window_catalog_macos_composition STATIC
  window_catalog_macos_composition.mm
  window_catalog_lifecycle_macos.cpp)
target_include_directories(window_catalog_macos_composition PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/../common
  ${APPTRAVERSE_WINDOW_CATALOG_EMBED_INCLUDE_DIR})
target_link_libraries(window_catalog_macos_composition PUBLIC
  surfaces_demo_window_catalog_common
  surfaces_demo_window_catalog_macos
  apptraverse_ui_macos_bridge
  "-framework Foundation")
set_source_files_properties(window_catalog_macos_composition.mm PROPERTIES
  COMPILE_FLAGS "-fobjc-arc")
target_compile_options(window_catalog_macos_composition PRIVATE
  $<$<COMPILE_LANGUAGE:OBJCXX>:-fno-rtti>
  $<$<COMPILE_LANGUAGE:OBJCXX>:-fno-objc-exceptions>)
target_compile_definitions(window_catalog_macos_composition PRIVATE
  APPTRAVERSE_WINDOW_CATALOG_USE_EMBEDDED_DISTILL)

include(${PROJECT_SOURCE_DIR}/cmake/apptraverse_embed_distilled_supply.cmake)
if(TARGET apptraverse_window_catalog_embedded_supply_macos)
  add_dependencies(window_catalog_macos_composition
    apptraverse_window_catalog_embedded_supply_macos)
elseif(TARGET apptraverse_window_catalog_embedded_supply)
  add_dependencies(window_catalog_macos_composition
    apptraverse_window_catalog_embedded_supply)
endif()
apptraverse_embed_distilled_supply(
  window_catalog_macos_composition
  apptraverse_export_window_catalog_distilled_graph_macos
  embedded_distilled_window_catalog_supply.h
  ${CMAKE_CURRENT_SOURCE_DIR}/window_catalog_graph_macos.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../common/window_catalog_graph_builder.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../common/window_catalog_model.cpp)

add_library(window_catalog_macos_entry STATIC WindowCatalogMain.swift)
set_target_properties(window_catalog_macos_entry PROPERTIES
  Swift_MODULE_NAME WindowCatalogMacEntry)
target_link_libraries(window_catalog_macos_entry PRIVATE
  apptraverse_ui_macos_swift)
target_compile_options(window_catalog_macos_entry PRIVATE
  "SHELL:-import-objc-header ${CMAKE_CURRENT_SOURCE_DIR}/window_catalog_macos_composition.h"
  "SHELL:-Xcc -I${PROJECT_SOURCE_DIR}/include"
  "SHELL:-Xcc -I${CMAKE_CURRENT_SOURCE_DIR}/../common")

add_executable(macos_window_catalog_demo
  window_catalog_link.mm)
set_target_properties(macos_window_catalog_demo PROPERTIES
  MACOSX_BUNDLE TRUE
  MACOSX_BUNDLE_GUI_IDENTIFIER com.apptraverse.windowcatalog.macos
  MACOSX_BUNDLE_BUNDLE_NAME macos_window_catalog_demo
  OUTPUT_NAME macos_window_catalog_demo)
target_link_libraries(macos_window_catalog_demo PRIVATE
  window_catalog_macos_composition
  window_catalog_macos_entry
  apptraverse_ui_macos)
set_target_properties(macos_window_catalog_demo PROPERTIES LINKER_LANGUAGE OBJCXX)
target_link_options(macos_window_catalog_demo PRIVATE
  -L${CMAKE_OSX_SYSROOT}/usr/lib/swift
  -L/usr/lib/swift
  -Wl,-rpath,/usr/lib/swift
  -Wl,-rpath,${CMAKE_OSX_SYSROOT}/usr/lib/swift)
