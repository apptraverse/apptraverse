enable_language(OBJC OBJCXX)
set(CMAKE_OBJCXX_STANDARD 20)
set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_Swift_COMPILER)
  execute_process(
    COMMAND xcrun --find swiftc
    OUTPUT_VARIABLE _swiftc_path
    OUTPUT_STRIP_TRAILING_WHITESPACE)
  set(CMAKE_Swift_COMPILER "${_swiftc_path}")
endif()
set(CMAKE_Swift_FLAGS "-sdk ${CMAKE_OSX_SYSROOT}")
enable_language(Swift)

add_library(minimal_app_macos_graph STATIC
  minimal_app_graph_macos.cpp)
target_include_directories(minimal_app_macos_graph PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(minimal_app_macos_graph PUBLIC
  minimal_app_common apptraverse_ui_macos_presenters)

add_executable(apptraverse_export_minimal_distilled_graph_macos
  minimal_app_export_macos.cpp)
target_link_libraries(apptraverse_export_minimal_distilled_graph_macos PRIVATE
  minimal_app_macos_graph)

add_library(minimal_app_macos_bridge STATIC
  ATMinimalNativeBridge.mm
  minimal_app_lifecycle_macos.cpp)
target_include_directories(minimal_app_macos_bridge PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(minimal_app_macos_bridge PUBLIC
  minimal_app_common apptraverse_ui_macos_presenters
  "-framework Foundation")
set_source_files_properties(ATMinimalNativeBridge.mm PROPERTIES
  COMPILE_FLAGS "-fobjc-arc")
target_compile_options(minimal_app_macos_bridge PRIVATE
  $<$<COMPILE_LANGUAGE:OBJCXX>:-fno-rtti>
  $<$<COMPILE_LANGUAGE:OBJCXX>:-fno-objc-exceptions>)

include(${PROJECT_SOURCE_DIR}/cmake/apptraverse_embed_distilled_supply.cmake)
apptraverse_embed_distilled_supply(
  minimal_app_macos_bridge
  apptraverse_export_minimal_distilled_graph_macos
  embedded_distilled_minimal_app_macos_supply.h)

add_library(minimal_app_macos_swift STATIC MinimalApp.swift)
set_target_properties(minimal_app_macos_swift PROPERTIES
  Swift_MODULE_NAME minimal_app_macos_swift)
target_compile_options(minimal_app_macos_swift PRIVATE
  "SHELL:-import-objc-header ${CMAKE_CURRENT_SOURCE_DIR}/ATMinimalNativeBridge.h")

add_executable(apptraverse_minimal_app_macos minimal_app_link.mm)
target_link_libraries(apptraverse_minimal_app_macos PRIVATE
  minimal_app_macos_bridge
  minimal_app_macos_swift
  "-framework AppKit"
  "-framework Foundation"
  "-framework SwiftUI")
set_target_properties(apptraverse_minimal_app_macos PROPERTIES
  LINKER_LANGUAGE OBJCXX)
target_compile_options(apptraverse_minimal_app_macos PRIVATE
  $<$<COMPILE_LANGUAGE:OBJCXX>:-fno-rtti>)
target_link_options(apptraverse_minimal_app_macos PRIVATE
  -L${CMAKE_OSX_SYSROOT}/usr/lib/swift
  -L/usr/lib/swift
  -Wl,-rpath,/usr/lib/swift)
