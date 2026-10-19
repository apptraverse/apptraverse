cmake_minimum_required(VERSION 3.20)

project(AppTraverse
  VERSION 0.1.0
  LANGUAGES CXX C
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Emscripten pthreads require atomics/bulk-memory on every TU that ends up in
# a -pthread link. Apply once for the whole tree (including aether-objects).
if(EMSCRIPTEN)
  add_compile_options(-pthread -fexceptions)
  add_link_options(-pthread)
endif()

include(cmake/CPM.cmake)
include(cmake/aether_version.cmake)
include(cmake/apptraverse_compile_policy.cmake)

# Quiet Aether telemetry unless the caller passes USER_CONFIG.
if(NOT USER_CONFIG OR USER_CONFIG STREQUAL "")
  get_filename_component(_apptraverse_aether_user_config
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/aether_user_config.h"
    ABSOLUTE)
  set(USER_CONFIG "${_apptraverse_aether_user_config}"
      CACHE PATH "Path to user provided configuration header file" FORCE)
endif()

# Fetch dependncies
# For using CPM.cmake projects with external package managers, such as conan or vcpkg,
# setting the variable CPM_USE_LOCAL_PACKAGES will make CPM.cmake try to add a package through
# find_package first, and add it from source if it doesn't succeed.
# It is possible to override the consumer's dependency with the version by supplying
# the CMake option CPM_<dependency name>_SOURCE set to the absolute path of the local library.
# All dependencies must be EXCLUDE_FROM_ALL FALSE to make them install together with the apptraverse.
# To avoid extra downloads it is recommend to set the CPM_SOURCE_CACHE environmental variable
# Also it required to used PATCHES
#
if (NOT CPM_SOURCE_CACHE)
  set(CPM_SOURCE_CACHE "${CMAKE_CURRENT_BINARY_DIR}/cpm.cache")
endif()

CPMAddPackage(
  NAME aether-miscpp
  GITHUB_REPOSITORY aethernetio/aether-miscpp
  GIT_TAG ${APPTRAVERSE_AETHER_MISCPP_GIT_TAG}
  EXCLUDE_FROM_ALL NO
  OPTIONS
    "AE_INSTALL OFF"
    "AE_BUILD_TESTS OFF"
)
CPMAddPackage(
  NAME aether-tele
  GITHUB_REPOSITORY aethernetio/aether-tele
  GIT_TAG ${APPTRAVERSE_AETHER_TELE_GIT_TAG}
  EXCLUDE_FROM_ALL NO
  OPTIONS
    "AE_TELE_INSTALL OFF"
    "AE_TELE_BUILD_TESTS OFF"
)
CPMAddPackage(
  NAME aether-objects
  GITHUB_REPOSITORY aethernetio/aether-objects
  GIT_TAG ${APPTRAVERSE_AETHER_OBJECTS_GIT_TAG}
  EXCLUDE_FROM_ALL NO
  PATCHES "${APPTRAVERSE_AETHER_OBJECTS_LOCAL_PERSISTENT_LOAD_PATCH}"
          "${APPTRAVERSE_AETHER_OBJECTS_DOMAIN_STORAGE_ACCESS_PATCH}"
          "${APPTRAVERSE_AETHER_OBJECTS_CHECKPOINT_SHELL_PATCH}"
  OPTIONS
    "AE_INSTALL OFF"
    "AE_BUILD_TESTS OFF"
)

CPMAddPackage(
  NAME aether-client-cpp
  GITHUB_REPOSITORY aethernetio/aether-client-cpp
  GIT_TAG ${APPTRAVERSE_AETHER_GIT_TAG}
  EXCLUDE_FROM_ALL NO
  OPTIONS
    "AE_INSTALL OFF"
    "AE_BUILD_TESTS ${APPTRAVERSE_BUILD_AETHER_DEMOS}"
)

include(cmake/apptraverse_library_targets.cmake)

if(WIN32)
  add_library(apptraverse_ui_windows STATIC
    src/platform/windows/win_ui_utf8.cpp
    src/platform/windows/win_ui_frame.cpp
    src/platform/windows/win_ui_presenters.cpp
    src/platform/windows/win_ui_registration.cpp
    src/platform/windows/win32_fatal.cpp
  )
  target_link_libraries(apptraverse_ui_windows PUBLIC apptraverse_application_runtime user32 gdi32 comctl32)
  target_include_directories(apptraverse_ui_windows PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
  )
endif()

add_library(apptraverse_surfaces STATIC
  src/surfaces/surfaces_model.cpp
  src/surfaces/surfaces_localization.cpp
  src/surfaces/surfaces_registration.cpp
  src/surfaces/desktop_surface_presenter.cpp
  src/surfaces/surface_window_presenter.cpp
  src/surfaces/surface_button_presenter.cpp
  src/surfaces/surface_language_button_presenter.cpp
  src/surfaces/surfaces_chrome_wiring.cpp
  src/surfaces/surfaces_presentation_size_queue.cpp
)
target_link_libraries(apptraverse_surfaces PUBLIC apptraverse_application_runtime)
target_include_directories(apptraverse_surfaces PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_features(apptraverse_surfaces PUBLIC cxx_std_20)
if(MSVC)
  target_compile_options(apptraverse_surfaces PRIVATE /bigobj)
endif()

if(WIN32)
  add_library(apptraverse_surfaces_windows STATIC
    src/platform/windows/surfaces_win_presenters.cpp
    src/platform/windows/surfaces_win32_z_order.cpp
    src/platform/windows/surfaces_win_registration.cpp
  )
  target_link_libraries(apptraverse_surfaces_windows PUBLIC
    apptraverse_surfaces
    apptraverse_ui_windows
    user32
    gdi32
  )
  target_include_directories(apptraverse_surfaces_windows PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
  )
  target_compile_features(apptraverse_surfaces_windows PUBLIC cxx_std_20)
endif()

option(APPTRAVERSE_BUILD_EXAMPLES "Build upstream AppTraverse examples" OFF)

if(APPTRAVERSE_BUILD_AETHER_DEMOS)
  add_subdirectory(examples/model_ui_runtime_demo)
  add_subdirectory(examples/chat_ui_runtime_demo)
  add_subdirectory(examples/aether_presence_monitor)
endif()

if(APPTRAVERSE_BUILD_EXAMPLES)
add_subdirectory(examples/main_window_runtime_demo)
add_subdirectory(examples/dynamic_objects_demo)
add_subdirectory(examples/surfaces_demo)
add_subdirectory(examples/shared_node_demo)
add_subdirectory(examples/chat_demo)
endif()

include(CTest)
if(BUILD_TESTING)
  add_subdirectory(tests)
endif()
