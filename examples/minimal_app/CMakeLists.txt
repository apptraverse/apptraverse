add_library(minimal_app_common STATIC
  common/minimal_application.cpp
  common/minimal_app_layout.cpp
  common/minimal_app_controls.cpp
  common/minimal_app_graph_builder.cpp
  common/minimal_app_prepare.cpp
  common/minimal_app_persistence.cpp
  common/minimal_app_directory_persistence.cpp
  common/minimal_app_bootstrap.cpp
  common/minimal_app_window_events.cpp
  common/minimal_app_window_lifecycle.cpp
  common/minimal_app_desktop_close.cpp
)
target_include_directories(minimal_app_common PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/common
)
target_link_libraries(minimal_app_common PUBLIC apptraverse)
set_source_files_properties(common/minimal_app_bootstrap.cpp PROPERTIES
  COMPILE_DEFINITIONS APPTRAVERSE_ENABLE_DISTILLATION)
if(NOT WIN32)
  # Preserve the existing bootstrap selection for the other platform consumers.
  target_compile_definitions(minimal_app_common INTERFACE APPTRAVERSE_ENABLE_DISTILLATION)
endif()
target_compile_features(minimal_app_common PUBLIC cxx_std_20)
if(MSVC)
  target_compile_options(minimal_app_common PRIVATE /utf-8)
endif()

add_library(minimal_app_headless STATIC
  headless/minimal_app_graph_headless.cpp
)
target_include_directories(minimal_app_headless PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/headless
  ${CMAKE_CURRENT_SOURCE_DIR}/common
)
target_link_libraries(minimal_app_headless PUBLIC minimal_app_common)
target_compile_features(minimal_app_headless PUBLIC cxx_std_20)
if(MSVC)
  target_compile_options(minimal_app_headless PRIVATE /utf-8)
endif()

if(WIN32 AND APPTRAVERSE_BUILD_EXAMPLES)
  add_subdirectory(windows)
endif()

if(EMSCRIPTEN AND APPTRAVERSE_BUILD_EXAMPLES)
  add_subdirectory(web)
endif()

if(APPLE AND NOT IOS AND APPTRAVERSE_BUILD_EXAMPLES)
  add_subdirectory(macos)
  add_subdirectory(ios/host_export)
endif()

if(IOS AND APPTRAVERSE_BUILD_EXAMPLES)
  add_subdirectory(ios)
endif()

if(APPTRAVERSE_BUILD_LINUX_GUI AND UNIX AND NOT APPLE AND NOT ANDROID AND
    NOT EMSCRIPTEN AND APPTRAVERSE_BUILD_EXAMPLES)
  add_subdirectory(linux)
endif()

# Android APK is built only via Gradle (examples/minimal_app/android); its
# supply exporter is a build-host tool.
if(NOT ANDROID AND NOT EMSCRIPTEN AND APPTRAVERSE_BUILD_EXAMPLES)
  add_subdirectory(android/host_export)
endif()
