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

add_library(apptraverse_ui_apple_content STATIC
  ${CMAKE_SOURCE_DIR}/src/platform/apple/CatalogContentView.swift
)
set_target_properties(apptraverse_ui_apple_content PROPERTIES
  Swift_MODULE_NAME apptraverse_ui_apple_content
)
target_compile_options(apptraverse_ui_apple_content PRIVATE
  "SHELL:-sdk ${CMAKE_OSX_SYSROOT}"
  "SHELL:-import-objc-header ${CMAKE_SOURCE_DIR}/include/apptraverse/platform/apple/AppleCatalogBridge.h"
)

set_source_files_properties(window_catalog_main.mm
  PROPERTIES COMPILE_FLAGS "-fobjc-arc")
add_executable(macos_window_catalog_demo
  window_catalog_main.mm
  window_catalog_graph_macos.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../common/window_catalog_lifecycle.cpp
)
target_compile_definitions(macos_window_catalog_demo PRIVATE
  APPTRAVERSE_ENABLE_DISTILLATION
)
target_link_libraries(macos_window_catalog_demo PRIVATE
  apptraverse_ui_macos
  apptraverse_ui_apple_content
  surfaces_demo_window_catalog_common
  "-framework AppKit"
  "-framework Foundation"
)
target_include_directories(macos_window_catalog_demo PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/../common
)
target_compile_options(macos_window_catalog_demo PRIVATE
  $<$<COMPILE_LANGUAGE:OBJCXX>:-fno-rtti>
  $<$<COMPILE_LANGUAGE:OBJCXX>:-fno-objc-exceptions>
)
set_target_properties(macos_window_catalog_demo PROPERTIES
  LINKER_LANGUAGE OBJCXX
  MACOSX_BUNDLE TRUE
  MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/WindowCatalogInfo.plist.in
  MACOSX_BUNDLE_BUNDLE_NAME "WindowCatalog"
  MACOSX_BUNDLE_GUI_IDENTIFIER com.apptraverse.experiment.windowcatalog.macos
  MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
  MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
)
target_link_options(macos_window_catalog_demo PRIVATE
  -L${CMAKE_OSX_SYSROOT}/usr/lib/swift
  -L/usr/lib/swift
  -Wl,-rpath,/usr/lib/swift
  -ObjC
)
