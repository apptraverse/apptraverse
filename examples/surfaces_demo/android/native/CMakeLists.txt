cmake_minimum_required(VERSION 3.25)

project(apptraverse_window_catalog_native LANGUAGES C CXX)

if(NOT ANDROID)
  message(FATAL_ERROR
    "WindowCatalog Android native build requires ANDROID")
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

set(_wc_supply_header
  "${CMAKE_CURRENT_LIST_DIR}/../generated/embedded_distilled_window_catalog_android_supply.h")
if(NOT EXISTS "${_wc_supply_header}")
  message(FATAL_ERROR
    "Missing ${_wc_supply_header}. Run apptraverse_export_window_catalog_distilled_graph_android on the build host.")
endif()

add_library(window_catalog_android STATIC
  ../window_catalog_android_lifecycle.cpp
  ../window_catalog_android_embedded_profile.cpp
  ../window_catalog_android_persistence.cpp
  ../window_catalog_graph_android.cpp
)
target_include_directories(window_catalog_android PUBLIC
  ${APPTRAVERSE_ROOT}/examples/surfaces_demo/android
  ${APPTRAVERSE_ROOT}/examples/surfaces_demo/common
  ${APPTRAVERSE_ROOT}/examples/surfaces_demo/android/native
)
target_include_directories(window_catalog_android PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}/../generated)
target_link_libraries(window_catalog_android PUBLIC
  surfaces_demo_window_catalog_common
  apptraverse_ui_android
)
target_compile_definitions(window_catalog_android PUBLIC
  APPTRAVERSE_ENABLE_DISTILLATION)

add_library(apptraverse_window_catalog_jni SHARED
  jni_bridge.cpp
  window_catalog_native_runtime.cpp
  window_catalog_ui_bridge.cpp
)

target_include_directories(apptraverse_window_catalog_jni PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}
  ${APPTRAVERSE_ROOT}/examples/surfaces_demo/android
  ${APPTRAVERSE_ROOT}/examples/surfaces_demo/common
  ${APPTRAVERSE_ROOT}/src/platform/android
  ${CMAKE_CURRENT_LIST_DIR}/../generated
)

target_link_libraries(apptraverse_window_catalog_jni PRIVATE
  window_catalog_android
  surfaces_demo_window_catalog_common
  apptraverse_ui_android
  log
)

set_target_properties(apptraverse_window_catalog_jni PROPERTIES
  OUTPUT_NAME "apptraverse_window_catalog"
  POSITION_INDEPENDENT_CODE ON
  CXX_VISIBILITY_PRESET hidden
  VISIBILITY_INLINES_HIDDEN ON
)
