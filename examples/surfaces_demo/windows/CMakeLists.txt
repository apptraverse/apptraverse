set(_surfaces_win_sources
  main.cpp
  win_app.cpp
  win_presenters.cpp
  ${CMAKE_SOURCE_DIR}/examples/main_window_runtime_demo/windows/win32_fatal.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../common/surfaces_lifecycle.cpp
)

add_executable(win32_surfaces_demo ${_surfaces_win_sources})
target_compile_definitions(win32_surfaces_demo PRIVATE
  APPTRAVERSE_ENABLE_DISTILLATION
)
target_link_libraries(win32_surfaces_demo PRIVATE
  surfaces_demo_distill
)

add_executable(win32_surfaces_demo_load_only ${_surfaces_win_sources})
# Load-only: model + desktop/win presenters — no distill bootstrap.

foreach(_surfaces_demo win32_surfaces_demo win32_surfaces_demo_load_only)
  target_link_libraries(${_surfaces_demo} PRIVATE
    surfaces_demo_model
    surfaces_demo_desktop
    user32
    gdi32
  )
  target_include_directories(${_surfaces_demo} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../common
    ${CMAKE_CURRENT_SOURCE_DIR}/../desktop
    ${CMAKE_SOURCE_DIR}/examples/main_window_runtime_demo/windows
  )
  if(MSVC)
    target_compile_options(${_surfaces_demo} PRIVATE /utf-8)
    set_target_properties(${_surfaces_demo} PROPERTIES WIN32_EXECUTABLE TRUE)
    target_link_options(${_surfaces_demo} PRIVATE /ENTRY:mainCRTStartup)
  endif()
  if(MINGW)
    set_target_properties(${_surfaces_demo} PROPERTIES EXCLUDE_FROM_ALL TRUE)
  endif()
endforeach()
