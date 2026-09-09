add_executable(apptraverse_event_sourced_core_test
  event_sourced_core_test.cpp
)

target_link_libraries(apptraverse_event_sourced_core_test PRIVATE apptraverse aether)

add_test(
  NAME apptraverse_event_sourced_core_test
  COMMAND apptraverse_event_sourced_core_test
)

add_executable(apptraverse_directory_domain_storage_test
  directory_domain_storage_test.cpp
)

target_link_libraries(apptraverse_directory_domain_storage_test PRIVATE apptraverse aether)

add_test(
  NAME apptraverse_directory_domain_storage_test
  COMMAND apptraverse_directory_domain_storage_test
)

add_executable(apptraverse_overlay_domain_storage_test
  overlay_domain_storage_test.cpp
)

target_link_libraries(apptraverse_overlay_domain_storage_test PRIVATE apptraverse aether)

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

add_executable(apptraverse_publication_channel_test
  publication_channel_test.cpp
)

target_include_directories(apptraverse_publication_channel_test PRIVATE
  ${CMAKE_SOURCE_DIR}/include
)

add_test(
  NAME apptraverse_publication_channel_test
  COMMAND apptraverse_publication_channel_test
)

add_executable(apptraverse_main_window_lifecycle_test
  main_window_lifecycle_test.cpp
  ${CMAKE_SOURCE_DIR}/examples/main_window_runtime_demo/common/main_window_lifecycle.cpp
)

target_compile_definitions(apptraverse_main_window_lifecycle_test PRIVATE
  APPTRAVERSE_ENABLE_DISTILLATION
)

target_link_libraries(apptraverse_main_window_lifecycle_test PRIVATE
  main_window_runtime_demo_model
)

add_test(
  NAME apptraverse_main_window_lifecycle_test
  COMMAND apptraverse_main_window_lifecycle_test
)

add_executable(apptraverse_main_window_lifecycle_load_only_test
  main_window_lifecycle_load_only_test.cpp
  ${CMAKE_SOURCE_DIR}/examples/main_window_runtime_demo/common/main_window_lifecycle.cpp
)

target_link_libraries(apptraverse_main_window_lifecycle_load_only_test PRIVATE
  main_window_runtime_demo_model
)

add_test(
  NAME apptraverse_main_window_lifecycle_load_only_test
  COMMAND apptraverse_main_window_lifecycle_load_only_test
)

add_executable(apptraverse_main_window_fatal_ndebug_test
  main_window_fatal_ndebug_test.cpp
  ${CMAKE_SOURCE_DIR}/examples/main_window_runtime_demo/common/main_window_lifecycle.cpp
)

# Do not compile this executable with NDEBUG. aether-objects Factory layout
# and RegisterClass are #ifndef NDEBUG; mixing that with Debug-linked
# aether-objects aborts before main. LoadApplication fail-fast is abort()
# (not assert), so missing state is still fatal in this Debug binary.
target_link_libraries(apptraverse_main_window_fatal_ndebug_test PRIVATE
  main_window_runtime_demo_model
)

add_test(
  NAME apptraverse_main_window_fatal_ndebug_test
  COMMAND apptraverse_main_window_fatal_ndebug_test
)

add_custom_target(apptraverse_main_window_headless_check
  COMMAND $<TARGET_FILE:apptraverse_publication_channel_test>
  COMMAND $<TARGET_FILE:apptraverse_main_window_lifecycle_test>
  COMMAND $<TARGET_FILE:apptraverse_main_window_lifecycle_load_only_test>
  COMMAND $<TARGET_FILE:apptraverse_main_window_fatal_ndebug_test>
  DEPENDS
    apptraverse_publication_channel_test
    apptraverse_main_window_lifecycle_test
    apptraverse_main_window_lifecycle_load_only_test
    apptraverse_main_window_fatal_ndebug_test
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
  COMMENT "Main-window lifecycle and publication-channel headless checks"
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
    wininet
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
    ${CMAKE_SOURCE_DIR}/examples/chat_ui_runtime_demo/windows/win_presenters.cpp
    ${CMAKE_SOURCE_DIR}/examples/chat_ui_runtime_demo/windows/aether_runtime.cpp
  )

  target_link_libraries(apptraverse_chat_win32_ui_smoke_test PRIVATE
    chat_ui_runtime_demo_common
    user32
    gdi32
    comctl32
    advapi32
    wininet
  )

  target_include_directories(apptraverse_chat_win32_ui_smoke_test PRIVATE
    ${CMAKE_SOURCE_DIR}/examples/chat_ui_runtime_demo/common
    ${CMAKE_SOURCE_DIR}/examples/chat_ui_runtime_demo/windows
  )

  if(MSVC)
    target_compile_options(apptraverse_chat_win32_ui_smoke_test PRIVATE /utf-8)
  endif()

  add_test(
    NAME apptraverse_chat_win32_ui_smoke_test
    COMMAND apptraverse_chat_win32_ui_smoke_test
  )

  add_executable(apptraverse_main_window_win32_smoke_test
    main_window_win32_smoke_test.cpp
    ${CMAKE_SOURCE_DIR}/examples/main_window_runtime_demo/windows/win_app.cpp
    ${CMAKE_SOURCE_DIR}/examples/main_window_runtime_demo/windows/win_presenters.cpp
    ${CMAKE_SOURCE_DIR}/examples/main_window_runtime_demo/common/main_window_lifecycle.cpp
  )

  target_compile_definitions(apptraverse_main_window_win32_smoke_test PRIVATE
    APPTRAVERSE_ENABLE_DISTILLATION
    "WIN32_MAIN_WINDOW_DEMO_EXE=R\"($<TARGET_FILE:win32_main_window_runtime_demo>)\""
    "WIN32_MAIN_WINDOW_LOAD_ONLY_EXE=R\"($<TARGET_FILE:win32_main_window_runtime_demo_load_only>)\""
  )

  target_link_libraries(apptraverse_main_window_win32_smoke_test PRIVATE
    main_window_runtime_demo_model
    user32
    gdi32
  )

  target_include_directories(apptraverse_main_window_win32_smoke_test PRIVATE
    ${CMAKE_SOURCE_DIR}/examples/main_window_runtime_demo/common
    ${CMAKE_SOURCE_DIR}/examples/main_window_runtime_demo/windows
  )

  add_dependencies(apptraverse_main_window_win32_smoke_test
    win32_main_window_runtime_demo
    win32_main_window_runtime_demo_load_only
  )

  if(MSVC)
    target_compile_options(apptraverse_main_window_win32_smoke_test PRIVATE /utf-8)
  endif()

  add_test(
    NAME apptraverse_main_window_win32_smoke_test
    COMMAND apptraverse_main_window_win32_smoke_test
  )

  add_custom_target(apptraverse_main_window_win32_smoke_check
    COMMAND $<TARGET_FILE:apptraverse_main_window_win32_smoke_test>
    DEPENDS apptraverse_main_window_win32_smoke_test
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Main-window Loading/main HWND smoke"
  )
endif()

