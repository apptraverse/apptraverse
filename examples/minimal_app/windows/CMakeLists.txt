include(${CMAKE_SOURCE_DIR}/cmake/apptraverse_embed_distilled_supply.cmake)

set(_minimal_embed_graph_sources
  ${CMAKE_CURRENT_SOURCE_DIR}/minimal_app_graph.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../common/minimal_application.cpp)

set(_minimal_app_win_sources
  minimal_app_main.cpp
  minimal_app_lifecycle.cpp
)

add_executable(apptraverse_export_minimal_distilled_graph
  minimal_app_export.cpp
  minimal_app_graph.cpp
)
target_link_libraries(apptraverse_export_minimal_distilled_graph PRIVATE
  minimal_app_common
  apptraverse
  apptraverse_ui_windows
)
target_include_directories(apptraverse_export_minimal_distilled_graph PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/../common
)
if(MSVC)
  target_compile_options(apptraverse_export_minimal_distilled_graph PRIVATE /utf-8)
endif()

add_executable(win32_minimal_app
  ${_minimal_app_win_sources}
  minimal_app_graph.cpp
)
target_compile_definitions(win32_minimal_app PRIVATE
  APPTRAVERSE_MINIMAL_APP_USE_EMBEDDED_DISTILL
)

add_executable(win32_minimal_app_load_only ${_minimal_app_win_sources})
target_compile_definitions(win32_minimal_app_load_only PRIVATE
  APPTRAVERSE_MINIMAL_APP_LOAD_ONLY
  APPTRAVERSE_MINIMAL_APP_USE_EMBEDDED_DISTILL
)

foreach(_minimal_demo win32_minimal_app win32_minimal_app_load_only)
  apptraverse_embed_distilled_supply(
    ${_minimal_demo}
    apptraverse_export_minimal_distilled_graph
    embedded_distilled_minimal_app_supply.h
    ${_minimal_embed_graph_sources})
  add_dependencies(${_minimal_demo} apptraverse_export_minimal_distilled_graph)
  target_link_libraries(${_minimal_demo} PRIVATE
    minimal_app_common
    apptraverse
    apptraverse_ui_windows
    user32
    gdi32
  )
  target_include_directories(${_minimal_demo} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../common
  )
  if(MSVC)
    target_compile_options(${_minimal_demo} PRIVATE /utf-8)
    set_target_properties(${_minimal_demo} PROPERTIES WIN32_EXECUTABLE TRUE)
    target_link_options(${_minimal_demo} PRIVATE /ENTRY:mainCRTStartup)
    target_link_libraries(${_minimal_demo} PRIVATE apptraverse_compile_policy)
    target_link_options(${_minimal_demo} PRIVATE
      "$<$<CONFIG:MinSizeRel>:/LTCG;/OPT:REF;/OPT:ICF=5;/GUARD:NO;/MAP:${CMAKE_CURRENT_BINARY_DIR}/$<TARGET_FILE_BASE_NAME:${_minimal_demo}>.map>"
    )
  endif()
  if(MINGW)
    set_target_properties(${_minimal_demo} PROPERTIES EXCLUDE_FROM_ALL TRUE)
  endif()
endforeach()
