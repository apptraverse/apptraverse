cmake_minimum_required(VERSION 3.25)

project(apptraverse_minimal_app_native LANGUAGES C CXX)

if(NOT ANDROID)
  message(FATAL_ERROR "Minimal App Android native build requires ANDROID")
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

set(APPTRAVERSE_BUILD_AETHER_DEMOS OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
add_subdirectory(${APPTRAVERSE_ROOT} ${CMAKE_CURRENT_BINARY_DIR}/apptraverse)

add_library(minimal_app_android STATIC
  ../minimal_app_android_graph.cpp
  ../minimal_app_android_lifecycle.cpp
  ../minimal_app_android_cold_load_probe.cpp
  ../minimal_app_android_initial_publication_probe.cpp
)
target_include_directories(minimal_app_android PUBLIC
  ${APPTRAVERSE_ROOT}/examples/minimal_app/android
  ${APPTRAVERSE_ROOT}/examples/minimal_app/common
)
target_link_libraries(minimal_app_android PUBLIC
  minimal_app_common
  apptraverse_ui_android
)
target_compile_definitions(minimal_app_android PUBLIC APPTRAVERSE_ENABLE_DISTILLATION)

add_library(apptraverse_minimal_app SHARED
  jni_bridge.cpp
  minimal_native_runtime.cpp
)

target_include_directories(apptraverse_minimal_app PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}
  ${APPTRAVERSE_ROOT}/examples/minimal_app/android
  ${APPTRAVERSE_ROOT}/examples/minimal_app/common
  ${APPTRAVERSE_ROOT}/src/platform/android
)

target_link_libraries(apptraverse_minimal_app PRIVATE
  minimal_app_android
  minimal_app_common
  apptraverse_ui_android
  log
)

set_target_properties(apptraverse_minimal_app PROPERTIES
  OUTPUT_NAME "apptraverse_minimal_app"
  POSITION_INDEPENDENT_CODE ON
  CXX_VISIBILITY_PRESET hidden
  VISIBILITY_INLINES_HIDDEN ON
)
