if(WIN32)
  add_executable(apptraverse_chat
    main.cpp
    win_chat_app.cpp
    ${CMAKE_SOURCE_DIR}/examples/main_window_runtime_demo/windows/win32_fatal.cpp
  )

  target_link_libraries(apptraverse_chat PRIVATE
    chat_demo_runtime
    user32
    gdi32
    comctl32
    shell32
    ole32
  )

  target_include_directories(apptraverse_chat PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../runtime
    ${CMAKE_CURRENT_SOURCE_DIR}/../aether
    ${CMAKE_CURRENT_SOURCE_DIR}/../common
    ${CMAKE_SOURCE_DIR}/examples/main_window_runtime_demo/windows
  )

  if(MSVC)
    target_compile_options(apptraverse_chat PRIVATE /utf-8)
    set_target_properties(apptraverse_chat PROPERTIES WIN32_EXECUTABLE TRUE)
  endif()
endif()
