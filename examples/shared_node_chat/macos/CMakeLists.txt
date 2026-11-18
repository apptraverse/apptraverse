add_library(apptraverse_chat_apple_composition STATIC ../apple/AppleComposition.cpp)
target_link_libraries(apptraverse_chat_apple_composition PUBLIC apptraverse)
add_executable(apptraverse_chat_apple_ui_exporter EXCLUDE_FROM_ALL
  ../apple/export_ui_supply.cpp ../chat_ui_model.cpp)
target_compile_definitions(apptraverse_chat_apple_ui_exporter PRIVATE APPTRAVERSE_CHAT_UI_PRODUCER)
target_compile_options(apptraverse_chat_apple_ui_exporter PRIVATE -O0)
target_link_libraries(apptraverse_chat_apple_ui_exporter PRIVATE apptraverse_chat_apple_composition)
set(_apple_supply "${CMAKE_CURRENT_BINARY_DIR}/generated/apple_chat_ui_supply.h")
add_custom_command(OUTPUT "${_apple_supply}"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/generated"
  COMMAND $<TARGET_FILE:apptraverse_chat_apple_ui_exporter> "${_apple_supply}"
  DEPENDS apptraverse_chat_apple_ui_exporter VERBATIM)
add_custom_target(apptraverse_chat_apple_ui_supply DEPENDS "${_apple_supply}")
add_dependencies(apptraverse_shared_chat_ui apptraverse_chat_apple_ui_supply)
target_compile_definitions(apptraverse_shared_chat_ui PRIVATE
  "APPTRAVERSE_CHAT_UI_SUPPLY_HEADER=\"${_apple_supply}\"")
set_source_files_properties(../apple/ChatBridge.mm AcceptanceBridge.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc -fno-objc-exceptions")
add_library(apptraverse_chat_macos_bridge STATIC ../apple/ChatBridge.mm)
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/../apple")
target_link_libraries(apptraverse_chat_macos_bridge PRIVATE apptraverse_shared_chat_ui apptraverse_chat_apple_composition "-framework Foundation" "-framework AppKit")
foreach(_kind product acceptance)
  if(_kind STREQUAL "acceptance" AND NOT BUILD_TESTING)
    continue()
  endif()
  set(_swift apptraverse_chat_macos_${_kind}_swift)
  add_library(${_swift} STATIC ../apple/ChatPush.swift ../apple/ChatPresenter.swift ChatMain.swift ChatHost.swift Acceptance.swift)
  set_target_properties(${_swift} PROPERTIES Swift_MODULE_NAME ChatMac_${_kind})
  target_compile_options(${_swift} PRIVATE "SHELL:-import-objc-header ${CMAKE_CURRENT_SOURCE_DIR}/../apple/ChatBridge.h")
  if(_kind STREQUAL "product")
    set(_app shared_node_chat)
    add_executable(${_app} MACOSX_BUNDLE link.mm)
    set_target_properties(${_app} PROPERTIES
      MACOSX_BUNDLE_BUNDLE_NAME "SharedNode Chat"
      MACOSX_BUNDLE_GUI_IDENTIFIER "org.apptraverse.SharedNodeChat.macos"
      MACOSX_BUNDLE_BUNDLE_VERSION "1" MACOSX_BUNDLE_SHORT_VERSION_STRING "0.1")
  else()
    target_compile_options(${_swift} PRIVATE -DCHAT_ACCEPTANCE)
    set(_app apptraverse_shared_chat_macos_acceptance)
    add_executable(${_app} EXCLUDE_FROM_ALL link.mm AcceptanceBridge.mm)
  endif()
  target_link_libraries(${_app} PRIVATE ${_swift} apptraverse_chat_macos_bridge
    apptraverse_shared_chat_session "-framework AppKit" "-framework SwiftUI" "-framework UserNotifications")
  set_target_properties(${_app} PROPERTIES LINKER_LANGUAGE OBJCXX)
  target_link_options(${_app} PRIVATE "$<$<CONFIG:Release>:LINKER:-dead_strip>")
endforeach()
if(BUILD_TESTING)
  add_executable(apptraverse_shared_chat_latency_worker EXCLUDE_FROM_ALL ../../../tests/shared_chat_latency_worker.cpp)
  target_link_libraries(apptraverse_shared_chat_latency_worker PRIVATE apptraverse_shared_chat_session)
endif()

set(_push_plist Contents/Info.plist)
include(${PROJECT_SOURCE_DIR}/cmake/apple_chat_push.cmake)

add_executable(apptraverse_chat_apple_supply_test EXCLUDE_FROM_ALL ../apple/ui_supply_test.cpp)
target_link_libraries(apptraverse_chat_apple_supply_test PRIVATE apptraverse_shared_chat_ui apptraverse_chat_apple_composition)
target_compile_options(apptraverse_chat_apple_supply_test PRIVATE -O0)
target_compile_definitions(apptraverse_chat_apple_supply_test PRIVATE
  "APPTRAVERSE_CHAT_UI_SUPPLY_HEADER=\"${_apple_supply}\"")
