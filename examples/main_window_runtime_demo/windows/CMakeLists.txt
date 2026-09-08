set(_main_window_win_sources
  main.cpp
  win_app.cpp
  win_presenters.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../common/main_window_lifecycle.cpp
)

add_executable(win32_main_window_runtime_demo ${_main_window_win_sources})
target_compile_definitions(win32_main_window_runtime_demo PRIVATE
  APPTRAVERSE_ENABLE_DISTILLATION
)

add_executable(win32_main_window_runtime_demo_load_only ${_main_window_win_sources})

foreach(_mw_demo win32_main_window_runtime_demo win32_main_window_runtime_demo_load_only)
  target_link_libraries(${_mw_demo} PRIVATE
    main_window_runtime_demo_model
    user32
    gdi32
  )
  target_include_directories(${_mw_demo} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../common
  )
  if(MSVC)
    target_compile_options(${_mw_demo} PRIVATE /utf-8)
    set_target_properties(${_mw_demo} PROPERTIES WIN32_EXECUTABLE TRUE)
    target_link_options(${_mw_demo} PRIVATE /ENTRY:mainCRTStartup)
  endif()
  if(MINGW)
    set_target_properties(${_mw_demo} PROPERTIES EXCLUDE_FROM_ALL TRUE)
  endif()
endforeach()
