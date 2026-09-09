set(_dynamic_win_sources
  main.cpp
  win_app.cpp
  win_presenters.cpp
  ${CMAKE_SOURCE_DIR}/examples/main_window_runtime_demo/windows/win32_fatal.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../common/dynamic_lifecycle.cpp
)

add_executable(win32_dynamic_objects_demo ${_dynamic_win_sources})
target_compile_definitions(win32_dynamic_objects_demo PRIVATE
  APPTRAVERSE_ENABLE_DISTILLATION
)
target_link_libraries(win32_dynamic_objects_demo PRIVATE
  dynamic_objects_demo_distill
)

add_executable(win32_dynamic_objects_demo_load_only ${_dynamic_win_sources})
# Load-only: model library only — no dynamic_distill / BuildDynamicObjectsGraph.

foreach(_dyn_demo win32_dynamic_objects_demo win32_dynamic_objects_demo_load_only)
  target_link_libraries(${_dyn_demo} PRIVATE
    dynamic_objects_demo_model
    user32
    gdi32
  )
  target_include_directories(${_dyn_demo} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../common
    ${CMAKE_SOURCE_DIR}/examples/main_window_runtime_demo/windows
  )
  if(MSVC)
    target_compile_options(${_dyn_demo} PRIVATE /utf-8)
    set_target_properties(${_dyn_demo} PROPERTIES WIN32_EXECUTABLE TRUE)
    target_link_options(${_dyn_demo} PRIVATE /ENTRY:mainCRTStartup)
  endif()
  if(MINGW)
    set_target_properties(${_dyn_demo} PROPERTIES EXCLUDE_FROM_ALL TRUE)
  endif()
endforeach()
