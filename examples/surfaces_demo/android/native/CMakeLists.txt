cmake_minimum_required(VERSION 3.25)

project(apptraverse_window_catalog_native LANGUAGES C CXX)

if(APPTRAVERSE_CMAKE_INITIAL_CACHE)
  include("${APPTRAVERSE_CMAKE_INITIAL_CACHE}")
endif()
option(APPTRAVERSE_WINDOW_CATALOG_DEBUG_FRESH "Start a fresh development USER" OFF)

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

# Distilled supply is produced by a build-host exporter (examples/surfaces_demo/
# android/host_export); the NDK toolchain never runs that executable. The nested
# host build tracks builder sources and rewrites the header when the graph changes
# (same pattern as examples/minimal_app/android/native).
set(_wc_supply_header
  "${CMAKE_CURRENT_BINARY_DIR}/generated/embedded_distilled_window_catalog_android_supply.h")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
# Short path: nested MSVC link paths must stay under MAX_PATH on Windows.
# Keep the incremental host build stable across Gradle native configurations.
# Export once to its own directory, then copy into each NDK configuration.
set(_wc_host_generator_key default)
if(CMAKE_HOST_WIN32)
  get_filename_component(_wc_cmake_bin "${CMAKE_COMMAND}" DIRECTORY)
  set(_wc_ninja "${_wc_cmake_bin}/ninja.exe")
  if(EXISTS "${_wc_ninja}")
    set(_wc_host_generator_key ninja)
  endif()
endif()
# Standalone NDK CMake and AGP may select different host generators. Their
# caches cannot share a directory even when ABI and application policy match.
set(_wc_host_export_dir "${APPTRAVERSE_ROOT}/b/wc-ahe/${ANDROID_ABI}-${APPTRAVERSE_WINDOW_CATALOG_DEBUG_FRESH}-${_wc_host_generator_key}")
set(_wc_host_supply_header
  "${_wc_host_export_dir}/generated/embedded_distilled_window_catalog_android_supply.h")
file(MAKE_DIRECTORY "${_wc_host_export_dir}/generated")
set(_wc_host_export_args
  -DCMAKE_BUILD_TYPE=Debug
  -DBUILD_TESTING=OFF
  -DAPPTRAVERSE_BUILD_LINUX_GUI=OFF
  -DAPPTRAVERSE_BUILD_EXAMPLES=ON
  "-DAPPTRAVERSE_ANDROID_WINDOW_CATALOG_SUPPLY_HEADER=${_wc_host_supply_header}")
# Visual Studio generators do not always expose standalone custom targets to
# MSBuild the way Ninja does; match minimal_app's intent with a Ninja host build.
if(CMAKE_HOST_WIN32)
  if(EXISTS "${_wc_ninja}")
    list(APPEND _wc_host_export_args "-G" "Ninja"
      "-DCMAKE_BUILD_TYPE=Debug"
      "-DCMAKE_C_FLAGS_DEBUG=/Od /RTC1 /Z7"
      "-DCMAKE_CXX_FLAGS_DEBUG=/Od /RTC1 /Z7"
      "-DCMAKE_MAKE_PROGRAM=${_wc_ninja}")
  endif()
endif()
if(APPTRAVERSE_CMAKE_INITIAL_CACHE)
  list(APPEND _wc_host_export_args "-C" "${APPTRAVERSE_CMAKE_INITIAL_CACHE}")
endif()
if(CPM_SOURCE_CACHE)
  list(APPEND _wc_host_export_args "-DCPM_SOURCE_CACHE=${CPM_SOURCE_CACHE}")
endif()
if(DEFINED CPM_aether-miscpp_SOURCE)
  list(APPEND _wc_host_export_args
    "-DCPM_aether-miscpp_SOURCE=${CPM_aether-miscpp_SOURCE}")
endif()
add_custom_command(
  OUTPUT "${_wc_host_export_dir}/window_catalog_supply_configured.stamp"
  COMMAND "${CMAKE_COMMAND}" -S "${APPTRAVERSE_ROOT}"
          -B "${_wc_host_export_dir}" ${_wc_host_export_args}
  COMMAND "${CMAKE_COMMAND}" -E touch
          "${_wc_host_export_dir}/window_catalog_supply_configured.stamp"
  DEPENDS "${CMAKE_CURRENT_LIST_FILE}" ${APPTRAVERSE_CMAKE_INITIAL_CACHE}
  VERBATIM
  COMMENT "Configure build-host WindowCatalog Android supply exporter")
if(CMAKE_HOST_WIN32 AND EXISTS "${_wc_ninja}")
  set(_wc_supply_build_cmd
    "${CMAKE_COMMAND}" --build "${_wc_host_export_dir}" --parallel 4
    --target apptraverse_android_window_catalog_supply)
else()
  set(_wc_supply_build_cmd
    "${CMAKE_COMMAND}" --build "${_wc_host_export_dir}"
    --config Debug --parallel 4 --target apptraverse_android_window_catalog_supply)
endif()
add_custom_target(window_catalog_android_supply
  COMMAND ${_wc_supply_build_cmd}
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
          "${_wc_host_supply_header}" "${_wc_supply_header}"
  DEPENDS "${_wc_host_export_dir}/window_catalog_supply_configured.stamp"
  BYPRODUCTS "${_wc_supply_header}"
  VERBATIM
  COMMENT "Build-host export of the Android WindowCatalog supply")
else()
  set(APPTRAVERSE_ANDROID_WINDOW_CATALOG_DEBUG_SUPPLY_HEADER "" CACHE FILEPATH
      "Android WindowCatalog supply prepared by Debug")
  if(NOT EXISTS "${APPTRAVERSE_ANDROID_WINDOW_CATALOG_DEBUG_SUPPLY_HEADER}")
    message(FATAL_ERROR "Provide the existing Debug Android WindowCatalog supply header for a shipping consumer")
  endif()
  add_custom_command(OUTPUT "${_wc_supply_header}"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${APPTRAVERSE_ANDROID_WINDOW_CATALOG_DEBUG_SUPPLY_HEADER}"
            "${_wc_supply_header}"
    DEPENDS "${APPTRAVERSE_ANDROID_WINDOW_CATALOG_DEBUG_SUPPLY_HEADER}" VERBATIM)
  add_custom_target(window_catalog_android_supply DEPENDS "${_wc_supply_header}")
endif()

add_library(window_catalog_android STATIC
  ../window_catalog_android_lifecycle.cpp
  ../window_catalog_android_embedded_profile.cpp
  ../window_catalog_android_persistence.cpp
  ../window_catalog_graph_android.cpp
)
add_dependencies(window_catalog_android window_catalog_android_supply)
target_include_directories(window_catalog_android PUBLIC
  ${APPTRAVERSE_ROOT}/examples/surfaces_demo/android
  ${APPTRAVERSE_ROOT}/examples/surfaces_demo/common
  ${APPTRAVERSE_ROOT}/examples/surfaces_demo/android/native
)
target_include_directories(window_catalog_android PRIVATE
  ${CMAKE_CURRENT_BINARY_DIR}/generated)
target_link_libraries(window_catalog_android PUBLIC
  surfaces_demo_window_catalog_common
  apptraverse_ui_android
)
if(APPTRAVERSE_WINDOW_CATALOG_DEBUG_FRESH)
  target_compile_definitions(window_catalog_android PRIVATE
    APPTRAVERSE_WINDOW_CATALOG_DEBUG_FRESH)
endif()

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
  ${CMAKE_CURRENT_BINARY_DIR}/generated
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

# An actual target-ABI consumer for cross-platform USER interchange acceptance.
add_executable(apptraverse_window_catalog_android_portable_user_fixture
  ${APPTRAVERSE_ROOT}/tests/window_catalog_portable_user_fixture.cpp)
add_dependencies(apptraverse_window_catalog_android_portable_user_fixture
  window_catalog_android_supply)
target_include_directories(apptraverse_window_catalog_android_portable_user_fixture PRIVATE
  ${CMAKE_CURRENT_BINARY_DIR}/generated
  ${APPTRAVERSE_ROOT}/examples/surfaces_demo/android
  ${APPTRAVERSE_ROOT}/examples/surfaces_demo/common)
target_compile_definitions(apptraverse_window_catalog_android_portable_user_fixture PRIVATE
  APPTRAVERSE_PORTABLE_ANDROID APPTRAVERSE_PORTABLE_PLATFORM="android")
target_link_libraries(apptraverse_window_catalog_android_portable_user_fixture PRIVATE
  window_catalog_android log)
