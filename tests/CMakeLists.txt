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

add_executable(apptraverse_chat_ui_mirror_integration_test
  chat_ui_mirror_integration_test.cpp
)

target_link_libraries(apptraverse_chat_ui_mirror_integration_test PRIVATE
  chat_ui_runtime_demo_common
)

add_test(
  NAME apptraverse_chat_ui_mirror_integration_test
  COMMAND apptraverse_chat_ui_mirror_integration_test
)

add_executable(apptraverse_chat_presentation_headless_test
  chat_presentation_headless_test.cpp
)

target_link_libraries(apptraverse_chat_presentation_headless_test PRIVATE
  chat_ui_runtime_demo_common
)

target_compile_definitions(apptraverse_chat_presentation_headless_test PRIVATE
  "CHAT_PRESENTATION_HEADLESS_SOURCE=\"${CMAKE_SOURCE_DIR}/tests/chat_presentation_headless_test.cpp\""
)

add_test(
  NAME apptraverse_chat_presentation_headless_test
  COMMAND apptraverse_chat_presentation_headless_test
)

add_executable(apptraverse_shared_journal_test
  shared_journal_test.cpp
)

target_link_libraries(apptraverse_shared_journal_test PRIVATE
  chat_ui_runtime_demo_common
)

add_test(
  NAME apptraverse_shared_journal_test
  COMMAND apptraverse_shared_journal_test
)

add_custom_target(apptraverse_chat_headless_check
  COMMAND $<TARGET_FILE:apptraverse_event_sourced_core_test>
  COMMAND $<TARGET_FILE:apptraverse_shared_journal_test>
  COMMAND $<TARGET_FILE:apptraverse_chat_presentation_headless_test>
  DEPENDS
    apptraverse_event_sourced_core_test
    apptraverse_shared_journal_test
    apptraverse_chat_presentation_headless_test
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
  COMMENT "Canonical headless chat/shared/presentation checks"
)

if(WIN32)
  add_executable(apptraverse_chat_p2p_headless_test
    chat_p2p_headless_test.cpp
    ${CMAKE_SOURCE_DIR}/examples/chat_ui_runtime_demo/windows/aether_runtime.cpp
  )

  target_link_libraries(apptraverse_chat_p2p_headless_test PRIVATE
    chat_ui_runtime_demo_common
    user32
    gdi32
    advapi32
  )

  target_include_directories(apptraverse_chat_p2p_headless_test PRIVATE
    ${CMAKE_SOURCE_DIR}/examples/chat_ui_runtime_demo/windows
    ${CMAKE_SOURCE_DIR}/examples/chat_ui_runtime_demo/common
  )

  if(MSVC)
    target_compile_options(apptraverse_chat_p2p_headless_test PRIVATE /utf-8)
  endif()

  add_test(
    NAME apptraverse_chat_p2p_headless_test
    COMMAND apptraverse_chat_p2p_headless_test
  )

  add_executable(apptraverse_chat_win32_ui_smoke_test
    chat_win32_ui_smoke_test.cpp
  )

  target_link_libraries(apptraverse_chat_win32_ui_smoke_test PRIVATE
    chat_ui_runtime_demo_common
    user32
    gdi32
  )

  target_include_directories(apptraverse_chat_win32_ui_smoke_test PRIVATE
    ${CMAKE_SOURCE_DIR}/examples/chat_ui_runtime_demo/common
  )

  if(MSVC)
    target_compile_options(apptraverse_chat_win32_ui_smoke_test PRIVATE /utf-8)
  endif()

  add_test(
    NAME apptraverse_chat_win32_ui_smoke_test
    COMMAND apptraverse_chat_win32_ui_smoke_test
  )

  add_executable(apptraverse_presence_remote_state_test
    presence_remote_state/test_presence_remote_state.cpp
  )

  target_link_libraries(apptraverse_presence_remote_state_test PRIVATE aether)

  target_include_directories(apptraverse_presence_remote_state_test PRIVATE
    ${CMAKE_SOURCE_DIR}
  )

  add_test(
    NAME apptraverse_presence_remote_state_test
    COMMAND apptraverse_presence_remote_state_test
  )
endif()
