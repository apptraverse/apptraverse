set(_surfaces_win_sources
  main.cpp
  win_app.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../common/surfaces_lifecycle.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../common/surfaces_shutdown_trace.cpp
)

function(apptraverse_add_surfaces_demo_executable target distill_lib evolution_lib windows_lib)
  add_executable(${target} ${_surfaces_win_sources})
  target_compile_definitions(${target} PRIVATE APPTRAVERSE_ENABLE_DISTILLATION)
  target_link_libraries(${target} PRIVATE
    ${distill_lib}
    ${evolution_lib}
    ${windows_lib}
    user32
    gdi32
  )
  target_include_directories(${target} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../common
    ${CMAKE_SOURCE_DIR}/examples/main_window_runtime_demo/windows
  )
  if(MSVC)
    target_compile_options(${target} PRIVATE /utf-8)
    set_target_properties(${target} PROPERTIES WIN32_EXECUTABLE TRUE)
    target_link_options(${target} PRIVATE /ENTRY:mainCRTStartup)
  endif()
  if(MINGW)
    set_target_properties(${target} PROPERTIES EXCLUDE_FROM_ALL TRUE)
  endif()
endfunction()

apptraverse_add_surfaces_demo_executable(win32_surfaces_demo
  surfaces_demo_distill surfaces_demo_evolution surfaces_demo_win32)
apptraverse_add_surfaces_demo_executable(win32_surfaces_demo_v1
  surfaces_demo_distill_v1 surfaces_demo_evolution_v1 surfaces_demo_win32_v1)

add_executable(win32_surfaces_demo_load_only ${_surfaces_win_sources})
target_link_libraries(win32_surfaces_demo_load_only PRIVATE
  surfaces_demo_win32
  surfaces_demo_evolution
  user32
  gdi32
)
target_include_directories(win32_surfaces_demo_load_only PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/../common
  ${CMAKE_SOURCE_DIR}/examples/main_window_runtime_demo/windows
)
if(MSVC)
  target_compile_options(win32_surfaces_demo_load_only PRIVATE /utf-8)
  set_target_properties(win32_surfaces_demo_load_only PROPERTIES WIN32_EXECUTABLE TRUE)
  target_link_options(win32_surfaces_demo_load_only PRIVATE /ENTRY:mainCRTStartup)
endif()

if(WIN32 AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/surface_v1_package")
  foreach(_demo_target win32_surfaces_demo_v1)
    add_custom_command(TARGET ${_demo_target} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/surface_v1_package"
        "$<TARGET_FILE_DIR:${_demo_target}>/surface_v1_package"
      COMMENT "Copy bundled Surface v1 package next to ${_demo_target}"
    )
  endforeach()
endif()

# WindowCatalog application executables.
set(_window_catalog_win_sources
  window_catalog_main.cpp
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
