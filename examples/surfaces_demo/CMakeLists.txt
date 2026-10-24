# Build-time Release R: examples/surfaces_demo/release.json (field "release").
# Embedded via cmake/apptraverse_surfaces_release_metadata.cmake (iter 03 probe;
# Surfaces EXE wiring in later iterations).

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
    windows/surface_graph_win.cpp
  )
  target_include_directories(surfaces_demo_win32 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/windows
    ${CMAKE_CURRENT_SOURCE_DIR}/common
  )
  target_link_libraries(surfaces_demo_win32 PUBLIC
    apptraverse_surfaces_windows
  )
  target_compile_features(surfaces_demo_win32 PUBLIC cxx_std_20)
  set(_surfaces_demo_presentation_lib surfaces_demo_win32)
else()
  set(_surfaces_demo_presentation_lib surfaces_demo_presentation)
endif()

add_library(surfaces_demo_evolution STATIC
  common/surfaces_evolution_ops.cpp
  common/surfaces_session_automation.cpp
)
target_link_libraries(surfaces_demo_evolution PUBLIC apptraverse_surfaces)
target_include_directories(surfaces_demo_evolution PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/common
)
target_compile_features(surfaces_demo_evolution PUBLIC cxx_std_20)

add_library(surfaces_demo_distill STATIC
  common/surfaces_distill.cpp
  common/surfaces_supply_constant_ids.cpp
)
target_link_libraries(surfaces_demo_distill PUBLIC
  apptraverse_surfaces
  ${_surfaces_demo_presentation_lib}
)
target_include_directories(surfaces_demo_distill PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/common
)
target_compile_features(surfaces_demo_distill PUBLIC cxx_std_20)

add_library(surfaces_demo_distill_embed STATIC
  common/surfaces_distill.cpp
  common/surfaces_supply_constant_ids.cpp
)
target_link_libraries(surfaces_demo_distill_embed PUBLIC
  apptraverse_surfaces
  surfaces_demo_presentation
)
target_include_directories(surfaces_demo_distill_embed PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/common
)
target_compile_features(surfaces_demo_distill_embed PUBLIC cxx_std_20)

if(WIN32 AND NOT EMSCRIPTEN)
  add_library(surfaces_demo_win32_v1 STATIC
    windows/surface_graph_win.cpp
  )
  target_include_directories(surfaces_demo_win32_v1 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/windows
    ${CMAKE_CURRENT_SOURCE_DIR}/common
  )
  target_link_libraries(surfaces_demo_win32_v1 PUBLIC
    apptraverse_surfaces_windows_v1
  )
  target_compile_features(surfaces_demo_win32_v1 PUBLIC cxx_std_20)

  add_library(surfaces_demo_distill_v1 STATIC
    common/surfaces_distill.cpp
  )
  target_link_libraries(surfaces_demo_distill_v1 PUBLIC
    apptraverse_surfaces_v1
    surfaces_demo_win32_v1
  )
  target_include_directories(surfaces_demo_distill_v1 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/common
  )
  target_compile_features(surfaces_demo_distill_v1 PUBLIC cxx_std_20)

  add_library(surfaces_demo_evolution_v1 STATIC
    common/surfaces_evolution_ops.cpp
    common/surfaces_session_automation.cpp
    common/surfaces_v1_package.cpp
  )
  target_link_libraries(surfaces_demo_evolution_v1 PUBLIC apptraverse_surfaces_v1)
  target_include_directories(surfaces_demo_evolution_v1 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/common
  )
  target_compile_features(surfaces_demo_evolution_v1 PUBLIC cxx_std_20)
endif()

add_library(surfaces_demo_mobile STATIC
  mobile/mobile_surface_presenter.cpp
)
target_include_directories(surfaces_demo_mobile PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/mobile
  ${CMAKE_CURRENT_SOURCE_DIR}/common
)
target_link_libraries(surfaces_demo_mobile PUBLIC apptraverse_surfaces)
target_compile_features(surfaces_demo_mobile PUBLIC cxx_std_20)

if(BUILD_TESTING OR APPTRAVERSE_BUILD_EXAMPLES)
  add_executable(apptraverse_export_surfaces_distilled_graph
    tools/export_surfaces_distilled_graph.cpp
  )
  target_link_libraries(apptraverse_export_surfaces_distilled_graph PRIVATE
    surfaces_demo_distill_embed
    apptraverse
  )
  target_include_directories(apptraverse_export_surfaces_distilled_graph PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/common
  )
  target_compile_features(apptraverse_export_surfaces_distilled_graph PRIVATE cxx_std_20)
endif()

if(WIN32 AND NOT EMSCRIPTEN)
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
endif()

if(IOS)
  add_subdirectory(ios)
endif()
