if(ANDROID OR EMSCRIPTEN)
  message(FATAL_ERROR "SharedNode Chat has no Android/Web presenter yet; use a supported native target")
endif()
add_library(apptraverse_shared_chat_ui STATIC chat_ui_model.cpp chat_ui_session.cpp)
target_link_libraries(apptraverse_shared_chat_ui PUBLIC apptraverse_shared_chat_session)
target_include_directories(apptraverse_shared_chat_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(apptraverse_shared_chat_ui PUBLIC
  "$<$<CONFIG:Debug>:APPTRAVERSE_CHAT_UI_PRODUCER>")
if(BUILD_TESTING AND (WIN32 OR (APPLE AND NOT IOS)) AND
   (CMAKE_CONFIGURATION_TYPES OR CMAKE_BUILD_TYPE STREQUAL "Debug"))
  # Real-cloud checkpoint acceptance is explicit, never part of default CTest.
  add_executable(apptraverse_shared_chat_ui_checkpoint_test EXCLUDE_FROM_ALL chat_ui_checkpoint_test.cpp)
  target_link_libraries(apptraverse_shared_chat_ui_checkpoint_test PRIVATE apptraverse_shared_chat_ui)
  # The model test owns its Debug producer. Do not build/link a complete SDK
  # runtime merely to exercise deterministic UI state and publication rules.
  add_executable(apptraverse_shared_chat_ui_model_test chat_ui_model_test.cpp chat_ui_model.cpp)
  target_compile_definitions(apptraverse_shared_chat_ui_model_test PRIVATE
    APPTRAVERSE_CHAT_UI_PRODUCER APPTRAVERSE_ENABLE_DISTILLATION)
  target_link_libraries(apptraverse_shared_chat_ui_model_test PRIVATE apptraverse)
  set_target_properties(apptraverse_shared_chat_ui_model_test PROPERTIES
    EXCLUDE_FROM_ALL "$<NOT:$<CONFIG:Debug>>"
    EXCLUDE_FROM_DEFAULT_BUILD_RELEASE TRUE
    EXCLUDE_FROM_DEFAULT_BUILD_RELWITHDEBINFO TRUE
    EXCLUDE_FROM_DEFAULT_BUILD_MINSIZEREL TRUE)
  add_test(NAME apptraverse_shared_chat_ui_model_test
    COMMAND apptraverse_shared_chat_ui_model_test CONFIGURATIONS Debug)
endif()
if(IOS)
  add_subdirectory(ios)
  return()
endif()
if(APPLE AND NOT IOS)
  add_subdirectory(macos)
  return()
endif()
if(UNIX AND NOT APPLE AND NOT ANDROID AND NOT EMSCRIPTEN)
  add_subdirectory(linux)
  return()
endif()
add_executable(shared_node_chat WIN32
  main_windows.cpp
  ../../src/platform/windows/win_ui_utf8.cpp
  ../../src/platform/windows/win32_fatal.cpp)
target_link_libraries(shared_node_chat PRIVATE
  apptraverse_shared_chat_ui user32 gdi32 shell32)
add_custom_target(apptraverse_generate_chat_ui_supply
  COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_SOURCE_DIR}/generated"
  COMMAND $<TARGET_FILE:shared_node_chat> --distill-only "${CMAKE_CURRENT_SOURCE_DIR}/generated/chat_ui_supply.h"
  DEPENDS shared_node_chat VERBATIM)

# Cloud validation is explicit; normal CTest must not create SDK identities.
if(BUILD_TESTING)
  add_executable(apptraverse_shared_chat_latency_worker EXCLUDE_FROM_ALL
    ../../tests/shared_chat_latency_worker.cpp)
  target_link_libraries(apptraverse_shared_chat_latency_worker PRIVATE
    apptraverse_shared_chat_session)
  add_executable(apptraverse_shared_chat_win32_test
    ../../tests/shared_chat_win32_test.cpp)
  target_link_libraries(apptraverse_shared_chat_win32_test PRIVATE
    apptraverse_compile_policy user32)
  target_include_directories(apptraverse_shared_chat_win32_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR})
endif()
