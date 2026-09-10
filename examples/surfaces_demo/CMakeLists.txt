add_library(surfaces_demo_model STATIC
  common/surfaces_model.cpp
)

target_include_directories(surfaces_demo_model PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/common
)

target_link_libraries(surfaces_demo_model PUBLIC apptraverse)
target_compile_features(surfaces_demo_model PUBLIC cxx_std_20)

add_library(surfaces_demo_distill STATIC
  common/surfaces_distill.cpp
)
target_link_libraries(surfaces_demo_distill PUBLIC
  surfaces_demo_model
)
target_compile_features(surfaces_demo_distill PUBLIC cxx_std_20)

add_library(surfaces_demo_desktop STATIC
  desktop/desktop_surface_presenter.cpp
)
target_include_directories(surfaces_demo_desktop PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/desktop
  ${CMAKE_CURRENT_SOURCE_DIR}/common
)
target_link_libraries(surfaces_demo_desktop PUBLIC
  surfaces_demo_model
)
target_compile_features(surfaces_demo_desktop PUBLIC cxx_std_20)

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
