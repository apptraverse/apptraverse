# Native library of the Android chat demo. Gradle externalNativeBuild is the
# only entry point; the desktop CMake tree never adds this directory.

cmake_minimum_required(VERSION 3.25)

project(apptraverse_chat_native LANGUAGES C CXX)

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

if(NOT DEFINED CPM_SOURCE_CACHE AND NOT DEFINED ENV{CPM_SOURCE_CACHE})
  if(CMAKE_HOST_WIN32)
    set(CPM_SOURCE_CACHE "C:/cpm-cache" CACHE PATH "CPM.cmake download cache")
  else()
    set(CPM_SOURCE_CACHE "${APPTRAVERSE_ROOT}/.cpm_cache"
      CACHE PATH "CPM.cmake download cache")
  endif()
endif()

set(APPTRAVERSE_BUILD_AETHER_DEMOS ON CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
add_subdirectory(${APPTRAVERSE_ROOT} ${CMAKE_CURRENT_BINARY_DIR}/apptraverse)

add_library(apptraverse_chat SHARED
  chat_native_runtime.cpp
  chat_ui_bridge.cpp
  jni_bridge.cpp
)

target_include_directories(apptraverse_chat PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}
  ${APPTRAVERSE_ROOT}/examples/chat_demo/runtime
  ${APPTRAVERSE_ROOT}/examples/chat_demo/aether
  ${APPTRAVERSE_ROOT}/examples/chat_demo/common
)

target_link_libraries(apptraverse_chat PRIVATE
  chat_demo_runtime
  log
)

set_target_properties(apptraverse_chat PROPERTIES
  OUTPUT_NAME "apptraverse_chat"
  POSITION_INDEPENDENT_CODE ON
  CXX_VISIBILITY_PRESET hidden
  VISIBILITY_INLINES_HIDDEN ON
)
