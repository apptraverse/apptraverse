# Build-time Release R: examples/window_catalog/release.json (field "release").
# Embedded via cmake/apptraverse_surfaces_release_metadata.cmake (iter 03 probe;
# Surfaces EXE wiring in later iterations).

add_library(window_catalog_presentation STATIC
  common/surface_graph_headless.cpp
)
target_include_directories(window_catalog_presentation PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/common
)
target_link_libraries(window_catalog_presentation PUBLIC apptraverse_surfaces)
target_compile_features(window_catalog_presentation PUBLIC cxx_std_20)

if(WIN32 AND NOT EMSCRIPTEN)
  add_library(window_catalog_win32 STATIC
    windows/surface_graph_win.cpp
  )
  target_include_directories(window_catalog_win32 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/windows
    ${CMAKE_CURRENT_SOURCE_DIR}/common
  )
  target_link_libraries(window_catalog_win32 PUBLIC
    apptraverse_surfaces_windows
  )
  target_compile_features(window_catalog_win32 PUBLIC cxx_std_20)
  set(_window_catalog_presentation_lib window_catalog_win32)
else()
  set(_window_catalog_presentation_lib window_catalog_presentation)
endif()

add_library(window_catalog_evolution STATIC EXCLUDE_FROM_ALL
  common/surfaces_evolution_ops.cpp
  common/surfaces_session_automation.cpp
)
target_link_libraries(window_catalog_evolution PUBLIC apptraverse_surfaces)
target_include_directories(window_catalog_evolution PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/common
)
target_compile_features(window_catalog_evolution PUBLIC cxx_std_20)

add_library(window_catalog_distill STATIC
  common/surfaces_distill.cpp
  common/surfaces_supply_constant_ids.cpp
)
target_link_libraries(window_catalog_distill PUBLIC
  apptraverse_surfaces
  ${_window_catalog_presentation_lib}
)
target_include_directories(window_catalog_distill PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/common
)
target_compile_features(window_catalog_distill PUBLIC cxx_std_20)

add_library(window_catalog_distill_embed STATIC
  common/surfaces_distill.cpp
  common/surfaces_supply_constant_ids.cpp
)
target_link_libraries(window_catalog_distill_embed PUBLIC
  apptraverse_surfaces
  window_catalog_presentation
)
target_include_directories(window_catalog_distill_embed PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/common
)
target_compile_features(window_catalog_distill_embed PUBLIC cxx_std_20)

if(WIN32 AND NOT EMSCRIPTEN)
  add_library(window_catalog_win32_v1 STATIC
    windows/surface_graph_win.cpp
  )
  target_include_directories(window_catalog_win32_v1 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/windows
    ${CMAKE_CURRENT_SOURCE_DIR}/common
  )
  target_link_libraries(window_catalog_win32_v1 PUBLIC
    apptraverse_surfaces_windows_v1
  )
  target_compile_features(window_catalog_win32_v1 PUBLIC cxx_std_20)

  add_library(window_catalog_distill_v1 STATIC
    common/surfaces_distill.cpp
  )
  target_link_libraries(window_catalog_distill_v1 PUBLIC
    apptraverse_surfaces_v1
    window_catalog_win32_v1
  )
  target_include_directories(window_catalog_distill_v1 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/common
  )
  target_compile_features(window_catalog_distill_v1 PUBLIC cxx_std_20)

  add_library(window_catalog_evolution_v1 STATIC EXCLUDE_FROM_ALL
    common/surfaces_evolution_ops.cpp
    common/surfaces_session_automation.cpp
    common/surfaces_v1_package.cpp
  )
  target_link_libraries(window_catalog_evolution_v1 PUBLIC apptraverse_surfaces_v1)
  target_include_directories(window_catalog_evolution_v1 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/common
  )
  target_compile_features(window_catalog_evolution_v1 PUBLIC cxx_std_20)
endif()

add_library(window_catalog_mobile STATIC
  mobile/mobile_surface_presenter.cpp
)
target_include_directories(window_catalog_mobile PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/mobile
  ${CMAKE_CURRENT_SOURCE_DIR}/common
)
target_link_libraries(window_catalog_mobile PUBLIC apptraverse_surfaces)
target_compile_features(window_catalog_mobile PUBLIC cxx_std_20)

add_library(window_catalog_window_catalog_common STATIC
  common/window_catalog_model.cpp
  common/window_catalog_menu.cpp
  common/window_catalog_menu_builder.cpp
  common/window_catalog_localization.cpp
  common/window_catalog_registration.cpp
  common/window_catalog_desktop_registration.cpp
  common/window_catalog_core_registration.cpp
  common/window_catalog_events_registration.cpp
  common/window_catalog_actions.cpp
  common/window_catalog_close_button.cpp
  common/window_catalog_graph_builder.cpp
  common/window_catalog_prepare.cpp
  common/window_catalog_graph_mobile_prefab.cpp
  common/window_catalog_chrome_layout.cpp
  common/window_catalog_list_row_text.cpp
  common/window_catalog_supply_constant_ids.cpp
  common/window_catalog_main_native_frame_queue.cpp
)
target_include_directories(window_catalog_window_catalog_common PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/common
)
target_link_libraries(window_catalog_window_catalog_common PUBLIC apptraverse)
target_compile_features(window_catalog_window_catalog_common PUBLIC cxx_std_20)
if(MSVC)
  target_compile_options(window_catalog_window_catalog_common PRIVATE /utf-8 /bigobj)
endif()

add_library(window_catalog_window_catalog_headless STATIC
  headless/window_catalog_graph_headless.cpp
)
target_include_directories(window_catalog_window_catalog_headless PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/headless
  ${CMAKE_CURRENT_SOURCE_DIR}/common
)
target_link_libraries(window_catalog_window_catalog_headless PUBLIC
  window_catalog_window_catalog_common)
target_compile_features(window_catalog_window_catalog_headless PUBLIC cxx_std_20)
if(MSVC)
  target_compile_options(window_catalog_window_catalog_headless PRIVATE /utf-8)
endif()

if(BUILD_TESTING OR APPTRAVERSE_BUILD_EXAMPLES)
  add_executable(apptraverse_export_surfaces_distilled_graph
    tools/export_surfaces_distilled_graph.cpp
  )
  target_link_libraries(apptraverse_export_surfaces_distilled_graph PRIVATE
    window_catalog_distill_embed
    apptraverse
  )
  target_include_directories(apptraverse_export_surfaces_distilled_graph PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/common
  )
  target_compile_features(apptraverse_export_surfaces_distilled_graph PRIVATE cxx_std_20)
endif()

if(APPTRAVERSE_BUILD_LEGACY_EXAMPLES AND NOT APPLE)
  add_library(surface_generic_ui STATIC common/surface_ui_model.cpp)
  target_include_directories(surface_generic_ui PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/common")
  target_link_libraries(surface_generic_ui PUBLIC apptraverse)
  target_compile_features(surface_generic_ui PUBLIC cxx_std_20)
  if(MSVC)
    target_compile_options(surface_generic_ui PRIVATE /utf-8 /bigobj)
  endif()
endif()

if(WIN32 AND NOT EMSCRIPTEN)
  # Reusable-window catalog consumer (WindowCatalog application).
  add_library(window_catalog_window_catalog STATIC
    windows/window_catalog_graph.cpp
    windows/window_catalog_graph_builder.cpp
  )
  target_include_directories(window_catalog_window_catalog PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/common
    ${CMAKE_CURRENT_SOURCE_DIR}/windows)
  target_link_libraries(window_catalog_window_catalog PUBLIC
    window_catalog_window_catalog_common
    apptraverse_ui_windows)
  target_link_libraries(window_catalog_window_catalog PUBLIC uxtheme)
  target_compile_features(window_catalog_window_catalog PUBLIC cxx_std_20)
  if(MSVC)
    target_compile_options(window_catalog_window_catalog PRIVATE /utf-8)
  endif()

  add_subdirectory(windows)
endif()

if(EMSCRIPTEN)
  add_subdirectory(web)
endif()

if(APPTRAVERSE_BUILD_LINUX_GUI AND UNIX AND NOT APPLE AND NOT ANDROID AND NOT EMSCRIPTEN)
  add_subdirectory(linux)
endif()

if(APPLE AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
  add_subdirectory(macos)
  if(APPTRAVERSE_BUILD_EXAMPLES)
    add_subdirectory(ios/host_export)
  endif()
endif()

if(IOS)
  add_subdirectory(ios)
  include(${PROJECT_SOURCE_DIR}/tests/ios_window_catalog_acceptance_targets.cmake)
endif()

if(NOT ANDROID AND NOT EMSCRIPTEN AND APPTRAVERSE_BUILD_EXAMPLES)
  add_subdirectory(android/host_export)
endif()
