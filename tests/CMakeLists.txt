# Tests for the only active application: WindowCatalog and its reusable UI.
function(apptraverse_add_test name source)
  add_executable(${name} ${source})
  target_link_libraries(${name} PRIVATE apptraverse ${ARGN})
  add_test(NAME ${name} COMMAND ${name})
endfunction()

apptraverse_add_test(apptraverse_application_contract_headless_test
  application_contract_headless_test.cpp)

apptraverse_add_test(apptraverse_application_model_runtime_test
  application_model_runtime_test.cpp)

apptraverse_add_test(apptraverse_application_ui_session_test
  application_ui_session_test.cpp)

apptraverse_add_test(apptraverse_ui_desktop_window_geometry_test
  ui/ui_desktop_window_geometry_test.cpp)

if(WIN32 AND APPTRAVERSE_BUILD_EXAMPLES)
  foreach(name apptraverse_window_catalog_model_test
               apptraverse_window_catalog_win32_acceptance_test)
    string(REPLACE "apptraverse_" "" source ${name})
    apptraverse_add_test(${name} ${source}.cpp surfaces_demo_window_catalog)
    if(MSVC)
      target_compile_options(${name} PRIVATE /utf-8)
    endif()
    set_tests_properties(${name} PROPERTIES LABELS "window_catalog")
  endforeach()
  target_compile_definitions(apptraverse_window_catalog_model_test PRIVATE
    APPTRAVERSE_ENABLE_DISTILLATION)
  add_dependencies(apptraverse_window_catalog_win32_acceptance_test
    win32_window_catalog_demo win32_window_catalog_demo_load_only)

  # All TUs including these presenters must share the test-only ABI.
  target_compile_definitions(apptraverse_ui_windows PUBLIC
    APPTRAVERSE_WIN_DESKTOP_FRAME_SYNC_TEST=1)
  apptraverse_add_test(apptraverse_win_application_message_host_test
    win_application_message_host_test.cpp apptraverse_ui_windows)
  apptraverse_add_test(apptraverse_win_ui_native_push_button_test
    win_ui_native_push_button_test.cpp apptraverse_ui_windows)
  apptraverse_add_test(apptraverse_presenter_win_desktop_frame_sync_test
    presenter_win_desktop_frame_sync_test.cpp apptraverse_ui_windows)
  apptraverse_add_test(apptraverse_win_ui_command_routing_test
    win_ui_command_routing_test.cpp apptraverse_ui_windows_extended)
endif()
