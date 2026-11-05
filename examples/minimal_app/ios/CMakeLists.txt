enable_language(OBJCXX)

if(NOT CMAKE_Swift_COMPILER)
  execute_process(
    COMMAND xcrun -f swiftc
    OUTPUT_VARIABLE _swiftc_path
    OUTPUT_STRIP_TRAILING_WHITESPACE)
  set(CMAKE_Swift_COMPILER ${_swiftc_path})
endif()
set(CMAKE_Swift_FLAGS "-sdk ${CMAKE_OSX_SYSROOT}")
set(CMAKE_Swift_COMPILER_TARGET
  ${CMAKE_OSX_ARCHITECTURES}-apple-ios${CMAKE_OSX_DEPLOYMENT_TARGET}-simulator
)
enable_language(Swift)

if(NOT APPTRAVERSE_IOS_HOST_EXPORTER)
  message(FATAL_ERROR
    "APPTRAVERSE_IOS_HOST_EXPORTER must point to the macOS-built "
    "apptraverse_export_minimal_distilled_graph_ios executable")
endif()

add_library(minimal_app_ios_graph STATIC minimal_app_graph_ios.cpp)
target_include_directories(minimal_app_ios_graph PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(minimal_app_ios_graph PUBLIC
  minimal_app_common apptraverse_ui_ios_presenters)

set(_ios_supply_dir
  ${CMAKE_CURRENT_BINARY_DIR}/generated_distilled/minimal_app_ios_composition)
file(MAKE_DIRECTORY "${_ios_supply_dir}")
set(_ios_supply_header
  ${_ios_supply_dir}/embedded_distilled_minimal_app_ios_supply.h)

add_custom_command(
  OUTPUT "${_ios_supply_header}"
  COMMAND "${APPTRAVERSE_IOS_HOST_EXPORTER}" "${_ios_supply_header}"
  VERBATIM
  COMMENT "Embed distilled iOS MinimalApp supply via macOS host exporter")

add_library(minimal_app_ios_composition STATIC
  minimal_app_ios_composition.mm
  minimal_app_lifecycle_ios.cpp
  "${_ios_supply_header}")
target_include_directories(minimal_app_ios_composition PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${_ios_supply_dir})
target_link_libraries(minimal_app_ios_composition PUBLIC
  minimal_app_common apptraverse_ui_ios_bridge
  "-framework Foundation")
set_source_files_properties(minimal_app_ios_composition.mm PROPERTIES
  COMPILE_FLAGS "-fobjc-arc")
target_compile_options(minimal_app_ios_composition PRIVATE
  $<$<COMPILE_LANGUAGE:OBJCXX>:-fno-rtti>
  $<$<COMPILE_LANGUAGE:OBJCXX>:-fno-objc-exceptions>)

add_library(minimal_app_ios_entry STATIC MinimalAppIosMain.swift)
set_target_properties(minimal_app_ios_entry PROPERTIES
  Swift_MODULE_NAME MinimalAppIosEntry)
target_link_libraries(minimal_app_ios_entry PRIVATE
  apptraverse_ui_ios_swift)
target_compile_options(minimal_app_ios_entry PRIVATE
  "SHELL:-import-objc-header ${CMAKE_CURRENT_SOURCE_DIR}/minimal_app_ios_composition.h"
  "SHELL:-Xcc -I${PROJECT_SOURCE_DIR}/include")

add_executable(apptraverse_minimal_app_ios minimal_app_ios_link.mm)
set_target_properties(apptraverse_minimal_app_ios PROPERTIES
  LINKER_LANGUAGE OBJCXX
  MACOSX_BUNDLE TRUE
  MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in
  MACOSX_BUNDLE_BUNDLE_NAME apptraverse_minimal_app_ios
  MACOSX_BUNDLE_GUI_IDENTIFIER com.apptraverse.minimalapp.ios
  MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
  MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION})

target_link_libraries(apptraverse_minimal_app_ios PRIVATE
  minimal_app_ios_composition
  minimal_app_ios_entry
  apptraverse_ui_ios)
target_compile_options(apptraverse_minimal_app_ios PRIVATE
  $<$<COMPILE_LANGUAGE:OBJCXX>:-fno-rtti>)

get_filename_component(_swift_bin_dir ${CMAKE_Swift_COMPILER} DIRECTORY)
set(_swift_runtime_dir ${_swift_bin_dir}/../lib/swift/iphonesimulator)
target_link_options(apptraverse_minimal_app_ios PRIVATE
  -L${CMAKE_OSX_SYSROOT}/usr/lib/swift
  -L${_swift_runtime_dir}
  -Wl,-rpath,/usr/lib/swift)
