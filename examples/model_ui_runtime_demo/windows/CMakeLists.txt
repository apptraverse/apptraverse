add_executable(win32_model_ui_runtime_demo
  main.cpp
  win_app.cpp
  win_presenters.cpp
)

target_link_libraries(win32_model_ui_runtime_demo PRIVATE
  model_ui_runtime_demo_common
  user32
  gdi32
)

target_include_directories(win32_model_ui_runtime_demo PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/../common
)

if(MSVC)
  target_compile_options(win32_model_ui_runtime_demo PRIVATE /utf-8)
  set_target_properties(win32_model_ui_runtime_demo PROPERTIES
    WIN32_EXECUTABLE TRUE
  )
  target_link_options(win32_model_ui_runtime_demo PRIVATE
    /ENTRY:mainCRTStartup
  )
endif()

if(MINGW)
  set_target_properties(win32_model_ui_runtime_demo PROPERTIES EXCLUDE_FROM_ALL TRUE)
endif()
