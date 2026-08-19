# Native library for the Android single-client chat example.
# This file is the CMake entry point used by Gradle externalNativeBuild only.
# Desktop CMake never adds the android/ directory.

cmake_minimum_required(VERSION 3.25)

project(apptraverse_single_client_chat_native LANGUAGES C CXX)

# ANDROID is defined by the NDK toolchain file, which CMake loads with the
# first project() call.
if(NOT ANDROID)
  message(FATAL_ERROR
    "This project is built by Gradle for Android only, ANDROID is not set")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# Everything linked into the shared library must be position independent.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

get_filename_component(APPTRAVERSE_ROOT
  "${CMAKE_CURRENT_LIST_DIR}/../../../.." REALPATH)

# Share one CPM download cache between ABIs and build types.
# Keep it short on Windows to avoid MAX_PATH failures while cloning deps.
if(NOT DEFINED CPM_SOURCE_CACHE AND NOT DEFINED ENV{CPM_SOURCE_CACHE})
  if(WIN32 OR CMAKE_HOST_WIN32)
    set(CPM_SOURCE_CACHE "C:/cpm-cache" CACHE PATH "CPM.cmake download cache")
  else()
    set(CPM_SOURCE_CACHE "${APPTRAVERSE_ROOT}/.cpm_cache"
      CACHE PATH "CPM.cmake download cache")
  endif()
endif()

# Prefer an explicit USER_CONFIG; otherwise use the shared chat product header.
# Absolute path so aether's #include USER_CONFIG works regardless of its cwd.
# Replace a stale cache entry if it still points at the removed Android-local header.
get_filename_component(_apptraverse_aether_user_config
  "${APPTRAVERSE_ROOT}/examples/single_client_chat/common/aether_user_config.h"
  ABSOLUTE)
if(NOT USER_CONFIG OR USER_CONFIG STREQUAL "" OR NOT EXISTS "${USER_CONFIG}")
  set(USER_CONFIG "${_apptraverse_aether_user_config}"
    CACHE PATH "Path to user provided configuration header file" FORCE)
endif()

# The App Traverse root project owns the aether pin and the apptraverse library.
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
add_subdirectory(${APPTRAVERSE_ROOT} ${CMAKE_CURRENT_BINARY_DIR}/apptraverse)

add_library(apptraverse_single_client_chat SHARED
  jni_bridge.cpp
  native_runtime.cpp
  ui_bridge.cpp
  ${APPTRAVERSE_ROOT}/examples/single_client_chat/common/aether_p2p_framing.cpp
  ${APPTRAVERSE_ROOT}/examples/single_client_chat/common/aether_p2p_transport.cpp
)

target_include_directories(apptraverse_single_client_chat PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}
  ${APPTRAVERSE_ROOT}/examples/single_client_chat/common
)

target_link_libraries(apptraverse_single_client_chat PRIVATE
  apptraverse
  single_client_chat_common
  android
  log
)

# Only JNI_OnLoad is exported, the natives are bound with RegisterNatives.
set_target_properties(apptraverse_single_client_chat PROPERTIES
  OUTPUT_NAME "apptraverse_single_client_chat"
  POSITION_INDEPENDENT_CODE ON
  CXX_VISIBILITY_PRESET hidden
  VISIBILITY_INLINES_HIDDEN ON
)
