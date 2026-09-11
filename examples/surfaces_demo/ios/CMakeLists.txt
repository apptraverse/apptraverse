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
    surfaces_demo_model
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
