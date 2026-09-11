add_executable(apptraverse_event_sourced_core_test
  event_sourced_core_test.cpp
)

target_link_libraries(apptraverse_event_sourced_core_test PRIVATE apptraverse)

add_test(
  NAME apptraverse_event_sourced_core_test
  COMMAND apptraverse_event_sourced_core_test
)

add_executable(apptraverse_journal_retention_test
  journal_retention_test.cpp
)

target_link_libraries(apptraverse_journal_retention_test PRIVATE apptraverse)

add_test(
  NAME apptraverse_journal_retention_test
  COMMAND apptraverse_journal_retention_test
)

add_executable(apptraverse_model_runtime_stop_test
  model_runtime_stop_test.cpp
)

target_link_libraries(apptraverse_model_runtime_stop_test PRIVATE apptraverse)

add_test(
  NAME apptraverse_model_runtime_stop_test
  COMMAND apptraverse_model_runtime_stop_test
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

if(APPTRAVERSE_BUILD_AETHER_DEMOS)
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
  COMMAND $<TARGET_FILE:apptraverse_journal_retention_test>
  COMMAND $<TARGET_FILE:apptraverse_chat_presentation_headless_test>
  DEPENDS
    apptraverse_event_sourced_core_test
    apptraverse_shared_journal_test
    apptraverse_journal_retention_test
    apptraverse_chat_presentation_headless_test
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
  COMMENT "Canonical headless chat/shared/presentation checks"
)
endif() # APPTRAVERSE_BUILD_AETHER_DEMOS

add_executable(apptraverse_publication_channel_test
  publication_channel_test.cpp
)

target_include_directories(apptraverse_publication_channel_test PRIVATE
  ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(apptraverse_publication_channel_test PRIVATE
  apptraverse_compile_policy
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

add_executable(apptraverse_main_window_window_changed_test
  main_window_window_changed_test.cpp
  ${CMAKE_SOURCE_DIR}/examples/main_window_runtime_demo/common/main_window_lifecycle.cpp
)

target_compile_definitions(apptraverse_main_window_window_changed_test PRIVATE
  APPTRAVERSE_ENABLE_DISTILLATION
)

target_link_libraries(apptraverse_main_window_window_changed_test PRIVATE
  main_window_runtime_demo_model
)

add_test(
  NAME apptraverse_main_window_window_changed_test
  COMMAND apptraverse_main_window_window_changed_test
)

add_custom_target(apptraverse_main_window_headless_check
  COMMAND $<TARGET_FILE:apptraverse_publication_channel_test>
  COMMAND $<TARGET_FILE:apptraverse_main_window_lifecycle_test>
  COMMAND $<TARGET_FILE:apptraverse_main_window_lifecycle_load_only_test>
  COMMAND $<TARGET_FILE:apptraverse_main_window_window_changed_test>
  DEPENDS
    apptraverse_publication_channel_test
    apptraverse_main_window_lifecycle_test
    apptraverse_main_window_lifecycle_load_only_test
    apptraverse_main_window_window_changed_test
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
  COMMENT "Main-window lifecycle and publication-channel headless checks"
)

add_executable(apptraverse_dynamic_objects_add_test
  dynamic_objects_add_test.cpp
  ${CMAKE_SOURCE_DIR}/examples/dynamic_objects_demo/common/dynamic_lifecycle.cpp
)

target_compile_definitions(apptraverse_dynamic_objects_add_test PRIVATE
  APPTRAVERSE_ENABLE_DISTILLATION
  "APPTRAVERSE_SOURCE_ROOT=R\"(${CMAKE_SOURCE_DIR})\""
)

target_link_libraries(apptraverse_dynamic_objects_add_test PRIVATE
  dynamic_objects_demo_distill
)

target_include_directories(apptraverse_dynamic_objects_add_test PRIVATE
  ${CMAKE_SOURCE_DIR}/examples/dynamic_objects_demo/common
)

add_test(
  NAME apptraverse_dynamic_objects_add_test
  COMMAND apptraverse_dynamic_objects_add_test
)

add_executable(apptraverse_surfaces_model_test
  surfaces_model_test.cpp
  ${CMAKE_SOURCE_DIR}/examples/surfaces_demo/common/surfaces_lifecycle.cpp
)

target_compile_definitions(apptraverse_surfaces_model_test PRIVATE
  APPTRAVERSE_ENABLE_DISTILLATION
)

target_link_libraries(apptraverse_surfaces_model_test PRIVATE
  surfaces_demo_distill
)

target_include_directories(apptraverse_surfaces_model_test PRIVATE
  ${CMAKE_SOURCE_DIR}/examples/surfaces_demo/common
)

add_test(
  NAME apptraverse_surfaces_model_test
  COMMAND apptraverse_surfaces_model_test
)

add_custom_target(apptraverse_surfaces_headless_check
  COMMAND $<TARGET_FILE:apptraverse_surfaces_model_test>
  DEPENDS apptraverse_surfaces_model_test
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
  COMMENT "Surfaces common model headless checks"
)

if(WIN32)
  add_executable(apptraverse_surfaces_win32_smoke_test
    surfaces_win32_smoke_test.cpp
    ${CMAKE_SOURCE_DIR}/examples/surfaces_demo/windows/win_app.cpp
    ${CMAKE_SOURCE_DIR}/examples/surfaces_demo/windows/win_presenters.cpp
    ${CMAKE_SOURCE_DIR}/examples/main_window_runtime_demo/windows/win32_fatal.cpp
    ${CMAKE_SOURCE_DIR}/examples/surfaces_demo/common/surfaces_lifecycle.cpp
  )
  target_compile_definitions(apptraverse_surfaces_win32_smoke_test PRIVATE
    APPTRAVERSE_ENABLE_DISTILLATION
  )
  target_link_libraries(apptraverse_surfaces_win32_smoke_test PRIVATE
    surfaces_demo_distill
    surfaces_demo_desktop
    user32
    gdi32
  )
  target_include_directories(apptraverse_surfaces_win32_smoke_test PRIVATE
    ${CMAKE_SOURCE_DIR}/examples/surfaces_demo/common
    ${CMAKE_SOURCE_DIR}/examples/surfaces_demo/desktop
    ${CMAKE_SOURCE_DIR}/examples/surfaces_demo/windows
    ${CMAKE_SOURCE_DIR}/examples/main_window_runtime_demo/windows
  )
  if(MSVC)
    target_compile_options(apptraverse_surfaces_win32_smoke_test PRIVATE /utf-8)
  endif()
  add_test(
    NAME apptraverse_surfaces_win32_smoke_test
    COMMAND apptraverse_surfaces_win32_smoke_test
  )
  add_custom_target(apptraverse_surfaces_win32_smoke_check
    COMMAND $<TARGET_FILE:apptraverse_surfaces_win32_smoke_test>
    DEPENDS apptraverse_surfaces_win32_smoke_test
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Surfaces Windows multi-window smoke"
  )
endif()

if(UNIX AND NOT APPLE AND NOT ANDROID AND NOT EMSCRIPTEN)
  find_package(X11 REQUIRED)
  add_executable(apptraverse_surfaces_linux_smoke_test
    surfaces_linux_smoke_test.cpp
    ${CMAKE_SOURCE_DIR}/examples/surfaces_demo/linux/linux_app.cpp
    ${CMAKE_SOURCE_DIR}/examples/surfaces_demo/linux/linux_presenters.cpp
    ${CMAKE_SOURCE_DIR}/examples/surfaces_demo/linux/linux_fatal.cpp
    ${CMAKE_SOURCE_DIR}/examples/surfaces_demo/common/surfaces_lifecycle.cpp
  )
  target_compile_definitions(apptraverse_surfaces_linux_smoke_test PRIVATE
    APPTRAVERSE_ENABLE_DISTILLATION
  )
  target_link_libraries(apptraverse_surfaces_linux_smoke_test PRIVATE
    surfaces_demo_distill
    surfaces_demo_desktop
    X11::X11
  )
  target_include_directories(apptraverse_surfaces_linux_smoke_test PRIVATE
    ${CMAKE_SOURCE_DIR}/examples/surfaces_demo/common
    ${CMAKE_SOURCE_DIR}/examples/surfaces_demo/desktop
    ${CMAKE_SOURCE_DIR}/examples/surfaces_demo/linux
  )
  add_test(
    NAME apptraverse_surfaces_linux_smoke_test
    COMMAND apptraverse_surfaces_linux_smoke_test
  )
  add_custom_target(apptraverse_surfaces_linux_smoke_check
    COMMAND $<TARGET_FILE:apptraverse_surfaces_linux_smoke_test>
    DEPENDS apptraverse_surfaces_linux_smoke_test
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Surfaces Linux multi-window smoke"
  )
endif()

if(APPLE AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
  enable_language(OBJCXX)
  set_source_files_properties(
    surfaces_macos_smoke_test.mm
    ${CMAKE_SOURCE_DIR}/examples/surfaces_demo/macos/mac_app.mm
    ${CMAKE_SOURCE_DIR}/examples/surfaces_demo/macos/mac_presenters.mm
    PROPERTIES COMPILE_FLAGS "-fobjc-arc"
  )
  add_executable(apptraverse_surfaces_macos_smoke_test
    surfaces_macos_smoke_test.mm
    ${CMAKE_SOURCE_DIR}/examples/surfaces_demo/macos/mac_app.mm
    ${CMAKE_SOURCE_DIR}/examples/surfaces_demo/macos/mac_presenters.mm
    ${CMAKE_SOURCE_DIR}/examples/surfaces_demo/common/surfaces_lifecycle.cpp
  )
  target_compile_definitions(apptraverse_surfaces_macos_smoke_test PRIVATE
    APPTRAVERSE_ENABLE_DISTILLATION
  )
  target_link_libraries(apptraverse_surfaces_macos_smoke_test PRIVATE
    surfaces_demo_distill
    surfaces_demo_desktop
    surfaces_demo_macos_content
    "-framework AppKit"
    "-framework Foundation"
  )
  target_include_directories(apptraverse_surfaces_macos_smoke_test PRIVATE
    ${CMAKE_SOURCE_DIR}/examples/surfaces_demo/common
    ${CMAKE_SOURCE_DIR}/examples/surfaces_demo/desktop
    ${CMAKE_SOURCE_DIR}/examples/surfaces_demo/macos
  )
  target_compile_options(apptraverse_surfaces_macos_smoke_test PRIVATE
    $<$<COMPILE_LANGUAGE:OBJCXX>:-fno-rtti>
  )
  # Same as the macOS host: clang++ links, Swift runtime via autolink entries.
  set_target_properties(apptraverse_surfaces_macos_smoke_test PROPERTIES
    LINKER_LANGUAGE OBJCXX
  )
  target_link_options(apptraverse_surfaces_macos_smoke_test PRIVATE
    -L${CMAKE_OSX_SYSROOT}/usr/lib/swift
    -L/usr/lib/swift
    -Wl,-rpath,/usr/lib/swift
  )
  add_test(
    NAME apptraverse_surfaces_macos_smoke_test
    COMMAND apptraverse_surfaces_macos_smoke_test
  )
  add_custom_target(apptraverse_surfaces_macos_smoke_check
    COMMAND $<TARGET_FILE:apptraverse_surfaces_macos_smoke_test>
    DEPENDS apptraverse_surfaces_macos_smoke_test
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Surfaces macOS multi-window smoke"
  )
endif()

add_executable(apptraverse_presenter_load_order_test
  presenter_load_order_test.cpp
)

target_link_libraries(apptraverse_presenter_load_order_test PRIVATE apptraverse)

add_test(
  NAME apptraverse_presenter_load_order_test
  COMMAND apptraverse_presenter_load_order_test
)

add_custom_target(apptraverse_dynamic_objects_headless_check
  COMMAND $<TARGET_FILE:apptraverse_dynamic_objects_add_test>
  COMMAND $<TARGET_FILE:apptraverse_publication_channel_test>
  COMMAND $<TARGET_FILE:apptraverse_presenter_load_order_test>
  DEPENDS
    apptraverse_dynamic_objects_add_test
    apptraverse_publication_channel_test
    apptraverse_presenter_load_order_test
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
  COMMENT "Dynamic objects Add Item headless checks"
)

if(WIN32)
  # Windows CreateProcess parent. Debug binary (not NDEBUG): aether-objects
  # Factory layout is #ifndef NDEBUG. Missing Application is still abort().
  add_executable(apptraverse_main_window_missing_load_test
    main_window_missing_load_test.cpp
    ${CMAKE_SOURCE_DIR}/examples/main_window_runtime_demo/common/main_window_lifecycle.cpp
  )
  target_link_libraries(apptraverse_main_window_missing_load_test PRIVATE
    main_window_runtime_demo_model
  )
  add_test(
    NAME apptraverse_main_window_missing_load_test
    COMMAND apptraverse_main_window_missing_load_test
  )

  # Isolated FatalWin32 + WriteFatalStderr with NDEBUG. No aether-objects.
  add_executable(apptraverse_win32_fatal_ndebug_child
    win32_fatal_ndebug_child.cpp
    ${CMAKE_SOURCE_DIR}/examples/main_window_runtime_demo/windows/win32_fatal.cpp
    ${CMAKE_SOURCE_DIR}/src/noninteractive_crt.cpp
  )
  target_compile_definitions(apptraverse_win32_fatal_ndebug_child PRIVATE
    NDEBUG
  )
  target_include_directories(apptraverse_win32_fatal_ndebug_child PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/examples/main_window_runtime_demo/windows
  )
  target_link_libraries(apptraverse_win32_fatal_ndebug_child PRIVATE
    apptraverse_compile_policy
  )

  add_executable(apptraverse_win32_fatal_ndebug_test
    win32_fatal_ndebug_test.cpp
  )
  target_compile_definitions(apptraverse_win32_fatal_ndebug_test PRIVATE
    "WIN32_FATAL_NDEBUG_CHILD=R\"($<TARGET_FILE:apptraverse_win32_fatal_ndebug_child>)\""
  )
  target_link_libraries(apptraverse_win32_fatal_ndebug_test PRIVATE
    apptraverse_compile_policy
  )
  add_dependencies(apptraverse_win32_fatal_ndebug_test
    apptraverse_win32_fatal_ndebug_child
  )
  add_test(
    NAME apptraverse_win32_fatal_ndebug_test
    COMMAND apptraverse_win32_fatal_ndebug_test
  )

  add_custom_command(TARGET apptraverse_main_window_headless_check POST_BUILD
    COMMAND $<TARGET_FILE:apptraverse_main_window_missing_load_test>
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
  )
  add_dependencies(apptraverse_main_window_headless_check
    apptraverse_main_window_missing_load_test
  )

  add_custom_target(apptraverse_win32_fatal_ndebug_check
    COMMAND $<TARGET_FILE:apptraverse_win32_fatal_ndebug_test>
    DEPENDS apptraverse_win32_fatal_ndebug_test
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Isolated FatalWin32 NDEBUG helper check"
  )

  if(APPTRAVERSE_BUILD_AETHER_DEMOS)
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
  endif() # APPTRAVERSE_BUILD_AETHER_DEMOS

  add_executable(apptraverse_main_window_win32_smoke_test
    main_window_win32_smoke_test.cpp
    ${CMAKE_SOURCE_DIR}/examples/main_window_runtime_demo/windows/win_app.cpp
    ${CMAKE_SOURCE_DIR}/examples/main_window_runtime_demo/windows/win_presenters.cpp
    ${CMAKE_SOURCE_DIR}/examples/main_window_runtime_demo/windows/win32_fatal.cpp
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

  add_executable(apptraverse_dynamic_objects_win32_smoke_test
    dynamic_objects_win32_smoke_test.cpp
    ${CMAKE_SOURCE_DIR}/examples/dynamic_objects_demo/windows/win_app.cpp
    ${CMAKE_SOURCE_DIR}/examples/dynamic_objects_demo/windows/win_presenters.cpp
    ${CMAKE_SOURCE_DIR}/examples/main_window_runtime_demo/windows/win32_fatal.cpp
    ${CMAKE_SOURCE_DIR}/examples/dynamic_objects_demo/common/dynamic_lifecycle.cpp
  )

  target_compile_definitions(apptraverse_dynamic_objects_win32_smoke_test PRIVATE
    APPTRAVERSE_ENABLE_DISTILLATION
    "WIN32_DYNAMIC_OBJECTS_DEMO_EXE=R\"($<TARGET_FILE:win32_dynamic_objects_demo>)\""
    "WIN32_DYNAMIC_OBJECTS_LOAD_ONLY_EXE=R\"($<TARGET_FILE:win32_dynamic_objects_demo_load_only>)\""
  )

  target_link_libraries(apptraverse_dynamic_objects_win32_smoke_test PRIVATE
    dynamic_objects_demo_distill
    user32
    gdi32
  )

  target_include_directories(apptraverse_dynamic_objects_win32_smoke_test PRIVATE
    ${CMAKE_SOURCE_DIR}/examples/dynamic_objects_demo/common
    ${CMAKE_SOURCE_DIR}/examples/dynamic_objects_demo/windows
    ${CMAKE_SOURCE_DIR}/examples/main_window_runtime_demo/windows
  )

  add_dependencies(apptraverse_dynamic_objects_win32_smoke_test
    win32_dynamic_objects_demo
    win32_dynamic_objects_demo_load_only
  )

  if(MSVC)
    target_compile_options(apptraverse_dynamic_objects_win32_smoke_test PRIVATE /utf-8)
  endif()

  add_test(
    NAME apptraverse_dynamic_objects_win32_smoke_test
    COMMAND apptraverse_dynamic_objects_win32_smoke_test
  )

  add_custom_target(apptraverse_dynamic_objects_win32_smoke_check
    COMMAND $<TARGET_FILE:apptraverse_dynamic_objects_win32_smoke_test>
    DEPENDS apptraverse_dynamic_objects_win32_smoke_test
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Dynamic objects Add Item Win32 smoke"
  )

  add_executable(apptraverse_dynamic_two_main_win32_test
    dynamic_two_main_win32_test.cpp
    ${CMAKE_SOURCE_DIR}/examples/dynamic_objects_demo/windows/win_presenters.cpp
    ${CMAKE_SOURCE_DIR}/examples/main_window_runtime_demo/windows/win32_fatal.cpp
  )
  target_link_libraries(apptraverse_dynamic_two_main_win32_test PRIVATE
    dynamic_objects_demo_model
    user32
    gdi32
  )
  target_include_directories(apptraverse_dynamic_two_main_win32_test PRIVATE
    ${CMAKE_SOURCE_DIR}/examples/dynamic_objects_demo/common
    ${CMAKE_SOURCE_DIR}/examples/dynamic_objects_demo/windows
    ${CMAKE_SOURCE_DIR}/examples/main_window_runtime_demo/windows
  )
  if(MSVC)
    target_compile_options(apptraverse_dynamic_two_main_win32_test PRIVATE /utf-8)
  endif()
  add_test(
    NAME apptraverse_dynamic_two_main_win32_test
    COMMAND apptraverse_dynamic_two_main_win32_test
  )
endif()

