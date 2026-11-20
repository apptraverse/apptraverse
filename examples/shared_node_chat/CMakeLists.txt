add_library(apptraverse_shared_chat_ui STATIC chat_ui_model.cpp chat_ui_session.cpp chat_ui_supply.cpp)
target_link_libraries(apptraverse_shared_chat_ui PUBLIC apptraverse_shared_chat_session)
target_include_directories(apptraverse_shared_chat_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(apptraverse_shared_chat_ui PUBLIC
  "$<$<CONFIG:Debug>:APPTRAVERSE_CHAT_UI_PRODUCER>")
if(NOT ANDROID AND NOT EMSCRIPTEN AND NOT IOS)
  add_executable(shared_node_chat_headless main_headless.cpp)
  target_link_libraries(shared_node_chat_headless PRIVATE apptraverse_shared_chat_session)
  if(CMAKE_CONFIGURATION_TYPES OR CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_executable(apptraverse_chat_apple_ui_exporter EXCLUDE_FROM_ALL
      export_chat_ui_supply.cpp chat_ui_model.cpp chat_ui_supply.cpp)
    target_link_libraries(apptraverse_chat_apple_ui_exporter PRIVATE apptraverse)
    target_compile_definitions(apptraverse_chat_apple_ui_exporter PRIVATE
      APPTRAVERSE_CHAT_UI_PRODUCER APPTRAVERSE_ENABLE_DISTILLATION)
  endif()
endif()
if(BUILD_TESTING AND (WIN32 OR (APPLE AND NOT IOS)) AND
   (CMAKE_CONFIGURATION_TYPES OR CMAKE_BUILD_TYPE STREQUAL "Debug"))
  # LOCAL isolation acceptance starts the optional SDK; run it explicitly.
  add_executable(apptraverse_shared_chat_ui_checkpoint_test EXCLUDE_FROM_ALL chat_ui_checkpoint_test.cpp)
  target_link_libraries(apptraverse_shared_chat_ui_checkpoint_test PRIVATE apptraverse_shared_chat_ui)
  # The model test owns its Debug producer. Do not build/link a complete SDK
  # runtime merely to exercise deterministic UI state and publication rules.
  add_executable(apptraverse_shared_chat_ui_model_test chat_ui_model_test.cpp chat_ui_model.cpp)
  target_compile_definitions(apptraverse_shared_chat_ui_model_test PRIVATE
    APPTRAVERSE_CHAT_UI_PRODUCER APPTRAVERSE_ENABLE_DISTILLATION)
  target_link_libraries(apptraverse_shared_chat_ui_model_test PRIVATE apptraverse)
  if(WIN32)
    target_sources(apptraverse_shared_chat_ui_model_test PRIVATE chat_windows_presenter.cpp)
    target_link_libraries(apptraverse_shared_chat_ui_model_test PRIVATE apptraverse_ui_windows)
  endif()
  # Visual Studio cannot generate a configuration-dependent EXCLUDE_FROM_ALL.
  # Build this model test explicitly in Debug; omit it from default builds.
  set_target_properties(apptraverse_shared_chat_ui_model_test PROPERTIES
    EXCLUDE_FROM_ALL TRUE
    EXCLUDE_FROM_DEFAULT_BUILD_RELEASE TRUE
    EXCLUDE_FROM_DEFAULT_BUILD_RELWITHDEBINFO TRUE
    EXCLUDE_FROM_DEFAULT_BUILD_MINSIZEREL TRUE)
  add_test(NAME apptraverse_shared_chat_ui_model_test
    COMMAND apptraverse_shared_chat_ui_model_test CONFIGURATIONS Debug)
endif()
if(BUILD_TESTING AND NOT ANDROID AND NOT EMSCRIPTEN AND NOT IOS)
  add_executable(apptraverse_shared_chat_ram_test ../../tests/shared_chat_ram_test.cpp)
  target_link_libraries(apptraverse_shared_chat_ram_test PRIVATE apptraverse_shared_chat_session)
  add_test(NAME apptraverse_shared_chat_ram_test COMMAND apptraverse_shared_chat_ram_test)
endif()
if(ANDROID)
  # The Android parent owns the JNI shared target and packaging.
  return()
endif()
if(EMSCRIPTEN)
  add_subdirectory(web)
  return()
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
add_executable(shared_node_chat WIN32 main_windows.cpp chat_windows_presenter.cpp)
target_link_libraries(shared_node_chat PRIVATE
  apptraverse_shared_chat_ui apptraverse_ui_windows user32 gdi32 shell32)
add_custom_target(apptraverse_generate_chat_ui_supply
  COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_SOURCE_DIR}/generated/windows"
  COMMAND $<TARGET_FILE:shared_node_chat> --distill-only "${CMAKE_CURRENT_SOURCE_DIR}/generated/windows/chat_ui_supply.h"
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
    apptraverse_ui_windows user32 gdi32 oleacc oleaut32 ole32 uuid)
  target_include_directories(apptraverse_shared_chat_win32_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR})
endif()
