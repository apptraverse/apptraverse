# Native library of the Android surfaces demo. Gradle externalNativeBuild is
# the only entry point; the desktop CMake tree never adds this directory.

cmake_minimum_required(VERSION 3.25)

project(apptraverse_surfaces_native LANGUAGES C CXX)

# ANDROID comes from the NDK toolchain file, loaded with the first project().
if(NOT ANDROID)
  message(FATAL_ERROR
    "This project is built by Gradle for Android only, ANDROID is not set")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

get_filename_component(APPTRAVERSE_ROOT
  "${CMAKE_CURRENT_LIST_DIR}/../../../.." REALPATH)

# One CPM download cache shared with the other Android examples on this host.
# Short path on Windows so dependency clones stay below MAX_PATH.
if(NOT DEFINED CPM_SOURCE_CACHE AND NOT DEFINED ENV{CPM_SOURCE_CACHE})
  if(CMAKE_HOST_WIN32)
    set(CPM_SOURCE_CACHE "C:/cpm-cache" CACHE PATH "CPM.cmake download cache")
  else()
    set(CPM_SOURCE_CACHE "${APPTRAVERSE_ROOT}/.cpm_cache"
      CACHE PATH "CPM.cmake download cache")
  endif()
endif()

# The surfaces demo needs the object graph only, not the Aether network client.
set(APPTRAVERSE_BUILD_AETHER_DEMOS OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
add_subdirectory(${APPTRAVERSE_ROOT} ${CMAKE_CURRENT_BINARY_DIR}/apptraverse)

add_library(apptraverse_surfaces SHARED
  android_surface_presenter.cpp
  jni_bridge.cpp
  native_runtime.cpp
  surfaces_ui_bridge.cpp
  ${APPTRAVERSE_ROOT}/examples/surfaces_demo/common/surfaces_lifecycle.cpp
)

# Dev build: create the initial graph when the state directory is empty.
target_compile_definitions(apptraverse_surfaces PRIVATE
  APPTRAVERSE_ENABLE_DISTILLATION
)

target_include_directories(apptraverse_surfaces PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}
)

target_link_libraries(apptraverse_surfaces PRIVATE
  surfaces_demo_distill
  surfaces_demo_mobile
  log
)

# Only JNI_OnLoad is exported, the natives are bound with RegisterNatives.
set_target_properties(apptraverse_surfaces PROPERTIES
  OUTPUT_NAME "apptraverse_surfaces"
  POSITION_INDEPENDENT_CODE ON
  CXX_VISIBILITY_PRESET hidden
  VISIBILITY_INLINES_HIDDEN ON
)
