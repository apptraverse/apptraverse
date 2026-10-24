set(_surfaces_win_sources

  main.cpp

  win_app.cpp

  ${CMAKE_CURRENT_SOURCE_DIR}/../common/surfaces_lifecycle.cpp

  ${CMAKE_CURRENT_SOURCE_DIR}/../common/surfaces_shutdown_trace.cpp

)



add_executable(win32_surfaces_demo ${_surfaces_win_sources})

target_compile_definitions(win32_surfaces_demo PRIVATE

  APPTRAVERSE_ENABLE_DISTILLATION

)

add_executable(win32_surfaces_demo_load_only ${_surfaces_win_sources})



target_link_libraries(win32_surfaces_demo PRIVATE

  surfaces_demo_distill

  surfaces_demo_win32_graph

)



target_link_libraries(win32_surfaces_demo_load_only PRIVATE

  surfaces_demo_win32_graph

)



foreach(_surfaces_demo win32_surfaces_demo win32_surfaces_demo_load_only)

  target_link_libraries(${_surfaces_demo} PRIVATE

    apptraverse_surfaces_windows

    user32

    gdi32

  )

  target_include_directories(${_surfaces_demo} PRIVATE

    ${CMAKE_CURRENT_SOURCE_DIR}

    ${CMAKE_CURRENT_SOURCE_DIR}/../common

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



set(_window_catalog_win_sources

  window_catalog_main.cpp

  window_catalog_win_app.cpp

  ${CMAKE_CURRENT_SOURCE_DIR}/../common/window_catalog_lifecycle.cpp

)



add_executable(win32_window_catalog_demo ${_window_catalog_win_sources})

target_compile_definitions(win32_window_catalog_demo PRIVATE

  APPTRAVERSE_ENABLE_DISTILLATION

)

add_executable(win32_window_catalog_demo_load_only ${_window_catalog_win_sources})

foreach(_catalog_demo win32_window_catalog_demo win32_window_catalog_demo_load_only)

  target_link_libraries(${_catalog_demo} PRIVATE
    surfaces_demo_window_catalog
    apptraverse_ui_windows
    user32
    gdi32
  )

  target_include_directories(${_catalog_demo} PRIVATE

    ${CMAKE_CURRENT_SOURCE_DIR}

    ${CMAKE_CURRENT_SOURCE_DIR}/../common

  )

  if(MSVC)

    target_compile_options(${_catalog_demo} PRIVATE /utf-8)

    set_target_properties(${_catalog_demo} PROPERTIES WIN32_EXECUTABLE TRUE)

    target_link_options(${_catalog_demo} PRIVATE /ENTRY:mainCRTStartup)
    target_link_libraries(${_catalog_demo} PRIVATE apptraverse_compile_policy)
    target_link_options(${_catalog_demo} PRIVATE
      "$<$<CONFIG:MinSizeRel>:/LTCG;/OPT:REF;/OPT:ICF=5;/GUARD:NO;/MAP:${CMAKE_CURRENT_BINARY_DIR}/$<TARGET_FILE_BASE_NAME:${_catalog_demo}>.map>"
    )

  endif()

  if(MINGW)

    set_target_properties(${_catalog_demo} PROPERTIES EXCLUDE_FROM_ALL TRUE)

  endif()

endforeach()

