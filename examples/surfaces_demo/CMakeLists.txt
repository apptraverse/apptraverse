add_library(surfaces_demo_model STATIC
  common/surfaces_model.cpp
  common/surfaces_localization.cpp
)

target_include_directories(surfaces_demo_model PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/common
)

target_link_libraries(surfaces_demo_model PUBLIC apptraverse)
target_compile_features(surfaces_demo_model PUBLIC cxx_std_20)
if(MSVC)
  target_compile_options(surfaces_demo_model PRIVATE /bigobj)
endif()

add_library(surfaces_demo_desktop STATIC
  desktop/desktop_surface_presenter.cpp
  desktop/surface_window_presenter.cpp
  desktop/surface_button_presenter.cpp
  desktop/surface_language_button_presenter.cpp
  desktop/surfaces_chrome_wiring.cpp
)
target_include_directories(surfaces_demo_desktop PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/desktop
  ${CMAKE_CURRENT_SOURCE_DIR}/common
)
target_link_libraries(surfaces_demo_desktop PUBLIC
  surfaces_demo_model
)
target_compile_features(surfaces_demo_desktop PUBLIC cxx_std_20)

if(WIN32 AND NOT EMSCRIPTEN)
  add_library(surfaces_demo_win32 STATIC
    windows/win_presenters.cpp
    windows/surface_graph_win.cpp
  )
  target_include_directories(surfaces_demo_win32 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/windows
    ${CMAKE_CURRENT_SOURCE_DIR}/desktop
    ${CMAKE_CURRENT_SOURCE_DIR}/common
    ${CMAKE_SOURCE_DIR}/examples/main_window_runtime_demo/windows
  )
  target_link_libraries(surfaces_demo_win32 PUBLIC
    surfaces_demo_desktop
    apptraverse_ui_windows
    user32
  )
  target_compile_features(surfaces_demo_win32 PUBLIC cxx_std_20)
  set(_surfaces_demo_presentation_lib surfaces_demo_win32)
else()
  add_library(surfaces_demo_presentation STATIC
    common/surface_graph_headless.cpp
  )
  target_include_directories(surfaces_demo_presentation PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/common
    ${CMAKE_CURRENT_SOURCE_DIR}/desktop
  )
  target_link_libraries(surfaces_demo_presentation PUBLIC
    surfaces_demo_desktop
  )
  target_compile_features(surfaces_demo_presentation PUBLIC cxx_std_20)
  set(_surfaces_demo_presentation_lib surfaces_demo_presentation)
endif()

add_library(surfaces_demo_distill STATIC
  common/surfaces_distill.cpp
)
target_link_libraries(surfaces_demo_distill PUBLIC
  surfaces_demo_model
  surfaces_demo_desktop
)
target_include_directories(surfaces_demo_distill PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/desktop
)
target_compile_features(surfaces_demo_distill PUBLIC cxx_std_20)

add_library(surfaces_demo_mobile STATIC
  mobile/mobile_surface_presenter.cpp
)
target_include_directories(surfaces_demo_mobile PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/mobile
  ${CMAKE_CURRENT_SOURCE_DIR}/common
)
target_link_libraries(surfaces_demo_mobile PUBLIC
  surfaces_demo_model
)
target_compile_features(surfaces_demo_mobile PUBLIC cxx_std_20)

# Platform hosts are mutually exclusive. Android builds via its own Gradle/CMake
# entry (add_subdirectory of the repo root) and is not listed here.
if(WIN32 AND NOT EMSCRIPTEN)
  add_subdirectory(windows)
endif()

if(EMSCRIPTEN)
  add_subdirectory(web)
endif()

if(UNIX AND NOT APPLE AND NOT ANDROID AND NOT EMSCRIPTEN)
  add_subdirectory(linux)
endif()

# APPLE alone is not enough: macOS AppKit vs iOS UIKit must not share hosts.
if(APPLE AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
  add_subdirectory(macos)
endif()

if(IOS)
  add_subdirectory(ios)
endif()
