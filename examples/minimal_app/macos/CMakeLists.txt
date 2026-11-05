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

add_library(minimal_app_macos_composition STATIC
  minimal_app_macos_composition.mm
  minimal_app_lifecycle_macos.cpp)
target_include_directories(minimal_app_macos_composition PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(minimal_app_macos_composition PUBLIC
  minimal_app_common apptraverse_ui_macos_bridge
  "-framework Foundation")
set_source_files_properties(minimal_app_macos_composition.mm PROPERTIES
  COMPILE_FLAGS "-fobjc-arc")
target_compile_options(minimal_app_macos_composition PRIVATE
  $<$<COMPILE_LANGUAGE:OBJCXX>:-fno-rtti>
  $<$<COMPILE_LANGUAGE:OBJCXX>:-fno-objc-exceptions>)

include(${PROJECT_SOURCE_DIR}/cmake/apptraverse_embed_distilled_supply.cmake)
apptraverse_embed_distilled_supply(
  minimal_app_macos_composition
  apptraverse_export_minimal_distilled_graph_macos
  embedded_distilled_minimal_app_macos_supply.h)

add_library(minimal_app_macos_entry STATIC MinimalAppMain.swift)
set_target_properties(minimal_app_macos_entry PROPERTIES
  Swift_MODULE_NAME MinimalAppMacEntry)
target_link_libraries(minimal_app_macos_entry PRIVATE
  apptraverse_ui_macos_swift)
target_compile_options(minimal_app_macos_entry PRIVATE
  "SHELL:-import-objc-header ${CMAKE_CURRENT_SOURCE_DIR}/minimal_app_macos_composition.h"
  "SHELL:-Xcc -I${PROJECT_SOURCE_DIR}/include")

add_executable(apptraverse_minimal_app_macos minimal_app_link.mm)
target_link_libraries(apptraverse_minimal_app_macos PRIVATE
  minimal_app_macos_composition
  minimal_app_macos_entry
  apptraverse_ui_macos)
set_target_properties(apptraverse_minimal_app_macos PROPERTIES
  LINKER_LANGUAGE OBJCXX)
target_compile_options(apptraverse_minimal_app_macos PRIVATE
  $<$<COMPILE_LANGUAGE:OBJCXX>:-fno-rtti>)
