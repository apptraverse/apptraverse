add_library(surfaces_demo_presentation STATIC
  common/surface_graph_headless.cpp
)
target_include_directories(surfaces_demo_presentation PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/common
)
target_link_libraries(surfaces_demo_presentation PUBLIC apptraverse_surfaces)
target_compile_features(surfaces_demo_presentation PUBLIC cxx_std_20)

if(WIN32 AND NOT EMSCRIPTEN)
  add_library(surfaces_demo_win32 STATIC
    windows/surfaces_demo_win32_attach.cpp
    windows/surfaces_demo_add_button.cpp
    windows/surfaces_demo_close_button.cpp
    windows/surfaces_demo_language_button.cpp
    windows/surfaces_demo_surface_window_host.cpp
    windows/surfaces_demo_win32_child_command.cpp
    windows/surfaces_demo_win32_registration.cpp
  )
  add_library(surfaces_demo_win32_graph STATIC
    windows/surface_graph_win.cpp
  )
  target_include_directories(surfaces_demo_win32_graph PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/windows
    ${CMAKE_CURRENT_SOURCE_DIR}/common
  )
  target_link_libraries(surfaces_demo_win32_graph PUBLIC
    surfaces_demo_win32
  )
  target_compile_features(surfaces_demo_win32_graph PUBLIC cxx_std_20)
  add_library(surfaces_demo_win32_layout_repro STATIC
    windows/surfaces_demo_layout_push_button_presenter.cpp
    windows/surfaces_demo_win32_layout_repro_registration.cpp
  )
  target_include_directories(surfaces_demo_win32_layout_repro PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/windows
    ${CMAKE_CURRENT_SOURCE_DIR}/common
  )
  target_link_libraries(surfaces_demo_win32_layout_repro PUBLIC
    apptraverse_surfaces_windows
    apptraverse_ui_windows
  )
  target_compile_features(surfaces_demo_win32_layout_repro PUBLIC cxx_std_20)
  target_include_directories(surfaces_demo_win32 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/windows
    ${CMAKE_CURRENT_SOURCE_DIR}/common
  )
  target_link_libraries(surfaces_demo_win32 PUBLIC
    apptraverse_surfaces_windows
    apptraverse_ui_windows
  )
  target_compile_features(surfaces_demo_win32 PUBLIC cxx_std_20)
  add_library(surfaces_demo_window_catalog STATIC
    common/window_catalog_model.cpp
    common/window_catalog_localization.cpp
    common/window_catalog_registration.cpp
    windows/window_catalog_graph.cpp
  )
  target_include_directories(surfaces_demo_window_catalog PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/common
    ${CMAKE_CURRENT_SOURCE_DIR}/windows
  )
  target_link_libraries(surfaces_demo_window_catalog PUBLIC
    apptraverse
    apptraverse_ui_windows
  )
  target_compile_features(surfaces_demo_window_catalog PUBLIC cxx_std_20)
  if(MSVC)
    target_compile_options(surfaces_demo_window_catalog PRIVATE /utf-8)
  endif()
  set(_surfaces_demo_presentation_lib surfaces_demo_win32)
else()
  set(_surfaces_demo_presentation_lib surfaces_demo_presentation)
endif()

add_library(surfaces_demo_distill STATIC
  common/surfaces_distill.cpp
)
target_link_libraries(surfaces_demo_distill PUBLIC
  apptraverse_surfaces
  ${_surfaces_demo_presentation_lib}
)
target_include_directories(surfaces_demo_distill PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/common
)
target_compile_features(surfaces_demo_distill PUBLIC cxx_std_20)

add_library(surfaces_demo_mobile STATIC
  mobile/mobile_surface_presenter.cpp
)
target_include_directories(surfaces_demo_mobile PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/mobile
  ${CMAKE_CURRENT_SOURCE_DIR}/common
)
target_link_libraries(surfaces_demo_mobile PUBLIC apptraverse_surfaces)
target_compile_features(surfaces_demo_mobile PUBLIC cxx_std_20)

if(WIN32 AND NOT EMSCRIPTEN)
  add_subdirectory(windows)
endif()

if(EMSCRIPTEN)
  add_subdirectory(web)
endif()

if(UNIX AND NOT APPLE AND NOT ANDROID AND NOT EMSCRIPTEN)
  add_subdirectory(linux)
endif()

if(APPLE AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
  add_subdirectory(macos)
endif()

if(IOS)
  add_subdirectory(ios)
endif()
