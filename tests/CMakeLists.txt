add_executable(apptraverse_event_sourced_core_test
  event_sourced_core_test.cpp
)

target_link_libraries(apptraverse_event_sourced_core_test PRIVATE apptraverse)

add_test(
  NAME apptraverse_event_sourced_core_test
  COMMAND apptraverse_event_sourced_core_test
)

add_executable(apptraverse_directory_domain_storage_test
  directory_domain_storage_test.cpp
)

target_link_libraries(apptraverse_directory_domain_storage_test PRIVATE apptraverse)

add_test(
  NAME apptraverse_directory_domain_storage_test
  COMMAND apptraverse_directory_domain_storage_test
)

add_executable(apptraverse_overlay_domain_storage_test
  overlay_domain_storage_test.cpp
)

target_link_libraries(apptraverse_overlay_domain_storage_test PRIVATE apptraverse)

add_test(
  NAME apptraverse_overlay_domain_storage_test
  COMMAND apptraverse_overlay_domain_storage_test
)

add_executable(apptraverse_model_ui_runtime_test
  model_ui_runtime_test.cpp
)

target_link_libraries(apptraverse_model_ui_runtime_test PRIVATE
  model_ui_runtime_demo_common
)

target_compile_definitions(apptraverse_model_ui_runtime_test PRIVATE
  "MODEL_UI_RUNTIME_DEMO_SOURCE_DIR=\"${CMAKE_SOURCE_DIR}/examples/model_ui_runtime_demo\""
)

if(WIN32)
  target_sources(apptraverse_model_ui_runtime_test PRIVATE
    ${CMAKE_SOURCE_DIR}/examples/model_ui_runtime_demo/windows/win_presenters.cpp
  )
  target_include_directories(apptraverse_model_ui_runtime_test PRIVATE
    ${CMAKE_SOURCE_DIR}/examples/model_ui_runtime_demo/windows
    ${CMAKE_SOURCE_DIR}/examples/model_ui_runtime_demo/common
  )
  target_link_libraries(apptraverse_model_ui_runtime_test PRIVATE user32 gdi32)
endif()

add_test(
  NAME apptraverse_model_ui_runtime_test
  COMMAND apptraverse_model_ui_runtime_test
)

add_executable(apptraverse_chat_ui_runtime_test
  chat_ui_runtime_test.cpp
)

target_link_libraries(apptraverse_chat_ui_runtime_test PRIVATE
  chat_ui_runtime_demo_common
)

target_compile_definitions(apptraverse_chat_ui_runtime_test PRIVATE
  "CHAT_UI_RUNTIME_DEMO_SOURCE_DIR=\"${CMAKE_SOURCE_DIR}/examples/chat_ui_runtime_demo\""
  "APPTRAVERSE_AETHER_EXPECTED_SHA=\"${APPTRAVERSE_AETHER_GIT_TAG}\""
)

add_test(
  NAME apptraverse_chat_ui_runtime_test
  COMMAND apptraverse_chat_ui_runtime_test
)
