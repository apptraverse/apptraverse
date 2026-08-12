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

# aether includes this header through the USER_CONFIG define and resolves the
# path against its own source root. STRING keeps the value relative, a PATH
# cache entry would be rewritten into an absolute build directory path.
# Force Hydrogen for real registration / cloud DNS / P2P (do not keep a stale
# android_smoke CACHE value from earlier local builds).
set(USER_CONFIG "config/user_config_hydrogen.h"
  CACHE STRING "Path to the aether user configuration header" FORCE)

# The AppTraverse root project owns the aether pin and the apptraverse library.
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
add_subdirectory(${APPTRAVERSE_ROOT} ${CMAKE_CURRENT_BINARY_DIR}/apptraverse)

add_library(apptraverse_single_client_chat SHARED
  jni_bridge.cpp
  native_runtime.cpp
  ui_bridge.cpp
  ${APPTRAVERSE_ROOT}/examples/single_client_chat/common/aether_p2p_framing.cpp
  ${APPTRAVERSE_ROOT}/examples/single_client_chat/common/aether_p2p_stream_select.cpp
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
