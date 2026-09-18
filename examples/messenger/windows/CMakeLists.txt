set(_messenger_win_sources
  main.cpp
  win_app.cpp
  win_presenters.cpp
  ${CMAKE_SOURCE_DIR}/examples/main_window_runtime_demo/windows/win32_fatal.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../common/messenger_lifecycle.cpp
)

add_executable(win32_messenger ${_messenger_win_sources})
target_compile_definitions(win32_messenger PRIVATE
  APPTRAVERSE_ENABLE_DISTILLATION
)
target_link_libraries(win32_messenger PRIVATE
  messenger_distill
)

foreach(_messenger_demo win32_messenger)
  target_link_libraries(${_messenger_demo} PRIVATE
    messenger_model
    messenger_desktop
    user32
    gdi32
  )
  target_include_directories(${_messenger_demo} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../common
    ${CMAKE_CURRENT_SOURCE_DIR}/../desktop
    ${CMAKE_SOURCE_DIR}/examples/main_window_runtime_demo/windows
  )
  if(MSVC)
    target_compile_options(${_messenger_demo} PRIVATE /utf-8)
    set_target_properties(${_messenger_demo} PROPERTIES WIN32_EXECUTABLE TRUE)
    target_link_options(${_messenger_demo} PRIVATE /ENTRY:mainCRTStartup)
  endif()
  if(MINGW)
    set_target_properties(${_messenger_demo} PROPERTIES EXCLUDE_FROM_ALL TRUE)
  endif()
endforeach()
