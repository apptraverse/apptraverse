add_library(apptraverse_shared_chat_session STATIC chat_session.cpp)
target_link_libraries(apptraverse_shared_chat_session PUBLIC apptraverse_shared_aether)
target_include_directories(apptraverse_shared_chat_session PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(shared_node_chat WIN32
  main_windows.cpp
  ../../src/platform/windows/win_ui_utf8.cpp
  ../../src/platform/windows/win32_fatal.cpp)
target_link_libraries(shared_node_chat PRIVATE
  apptraverse_shared_chat_session user32 gdi32 shell32)

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
