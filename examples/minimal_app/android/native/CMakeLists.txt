cmake_minimum_required(VERSION 3.25)

project(apptraverse_minimal_app_native LANGUAGES C CXX)
if(APPTRAVERSE_CMAKE_INITIAL_CACHE)
  include("${APPTRAVERSE_CMAKE_INITIAL_CACHE}")
endif()
option(APPTRAVERSE_MINIMAL_DEBUG_FRESH "Start an isolated fresh Debug USER" ON)

if(NOT ANDROID)
  message(FATAL_ERROR "Minimal App Android native build requires ANDROID")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

get_filename_component(APPTRAVERSE_ROOT
  "${CMAKE_CURRENT_LIST_DIR}/../../../.." REALPATH)
include("${APPTRAVERSE_ROOT}/cmake/apptraverse_minimum_size.cmake")

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

# The supply is distilled by a build-host exporter (examples/minimal_app/
# android/host_export) in a nested host build of this tree; the NDK toolchain
# never runs a target executable. The host build tracks the builder, schema
# and presenter sources and rewrites the header only when the exporter changes.
set(_minimal_supply_dir "${CMAKE_CURRENT_BINARY_DIR}/generated_distilled")
set(_minimal_supply_header
  "${_minimal_supply_dir}/embedded_distilled_minimal_app_supply.h")
# Outside the Gradle .cxx tree so host (MSBuild) object paths stay short.
set(_minimal_host_export_dir
  "${APPTRAVERSE_ROOT}/b/mahe/${CMAKE_BUILD_TYPE}-${ANDROID_ABI}")
set(_minimal_host_export_args
  -DCMAKE_BUILD_TYPE=Debug
  -DBUILD_TESTING=OFF
  -DAPPTRAVERSE_BUILD_LINUX_GUI=OFF
  "-DAPPTRAVERSE_ANDROID_MINIMAL_SUPPLY_HEADER=${_minimal_supply_header}")
if(APPTRAVERSE_CMAKE_INITIAL_CACHE)
  list(APPEND _minimal_host_export_args "-C" "${APPTRAVERSE_CMAKE_INITIAL_CACHE}")
endif()
if(CPM_SOURCE_CACHE)
  list(APPEND _minimal_host_export_args "-DCPM_SOURCE_CACHE=${CPM_SOURCE_CACHE}")
endif()
if(DEFINED CPM_aether-miscpp_SOURCE)
  list(APPEND _minimal_host_export_args
    "-DCPM_aether-miscpp_SOURCE=${CPM_aether-miscpp_SOURCE}")
endif()
add_custom_command(
  OUTPUT "${_minimal_host_export_dir}/CMakeCache.txt"
  COMMAND "${CMAKE_COMMAND}" -S "${APPTRAVERSE_ROOT}"
          -B "${_minimal_host_export_dir}" ${_minimal_host_export_args}
  DEPENDS "${CMAKE_CURRENT_LIST_FILE}" ${APPTRAVERSE_CMAKE_INITIAL_CACHE}
  VERBATIM
  COMMENT "Configure build-host MinimalApp supply exporter")
add_custom_target(minimal_app_android_supply
  COMMAND "${CMAKE_COMMAND}" --build "${_minimal_host_export_dir}"
          --config Debug --parallel 1 --target apptraverse_android_minimal_supply
  DEPENDS "${_minimal_host_export_dir}/CMakeCache.txt"
  BYPRODUCTS "${_minimal_supply_header}"
  VERBATIM
  COMMENT "Build-host export of the Android MinimalApp supply")

option(APPTRAVERSE_MINIMAL_TEST_HOOKS "Compile explicit Android native regression hooks" OFF)
add_library(minimal_app_android STATIC
  ../minimal_app_android_lifecycle.cpp
)
if(APPTRAVERSE_MINIMAL_TEST_HOOKS)
  target_sources(minimal_app_android PRIVATE
    ../minimal_app_android_embedded_profile.cpp
    ../minimal_app_android_cold_load_probe.cpp
    ../minimal_app_android_initial_publication_probe.cpp
    ../minimal_app_android_cold_reload_apply_probe.cpp
    ../minimal_app_android_checkpoint_test_gate.cpp
    ../minimal_app_android_model_thread_start_probe.cpp
    ../minimal_app_android_pending_save_race_probe.cpp
    ../minimal_app_android_early_background_save_probe.cpp
  )
  target_compile_definitions(minimal_app_android PUBLIC APPTRAVERSE_MINIMAL_TEST_HOOKS)
endif()
add_dependencies(minimal_app_android minimal_app_android_supply)
target_include_directories(minimal_app_android PUBLIC
  ${APPTRAVERSE_ROOT}/examples/minimal_app/android
  ${APPTRAVERSE_ROOT}/examples/minimal_app/common
)
target_include_directories(minimal_app_android PRIVATE ${_minimal_supply_dir})
target_link_libraries(minimal_app_android PUBLIC
  minimal_app_common
  apptraverse_ui_android
)
target_compile_definitions(minimal_app_android PUBLIC APPTRAVERSE_ENABLE_DISTILLATION)

add_library(apptraverse_minimal_app SHARED
  jni_bridge.cpp
  minimal_native_runtime.cpp
)
if(APPTRAVERSE_MINIMAL_DEBUG_FRESH)
  target_compile_definitions(apptraverse_minimal_app PRIVATE
    $<$<CONFIG:Debug>:APPTRAVERSE_DEBUG_FRESH_START>)
endif()

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

# Explicit test fixture; only apptraverse.testHooks enables this target. It
# builds the graph in-test; the app library never links the builder.
if(APPTRAVERSE_MINIMAL_TEST_HOOKS)
add_library(apptraverse_minimal_app_test_fixture SHARED
  ../app/src/androidTest/cpp/presenter_lifecycle_fixture.cpp
  ../minimal_app_android_graph.cpp
)
target_link_libraries(apptraverse_minimal_app_test_fixture PRIVATE
  minimal_app_android
  minimal_app_common
  apptraverse_ui_android
  log
)
set_target_properties(apptraverse_minimal_app_test_fixture PROPERTIES
  POSITION_INDEPENDENT_CODE ON
  CXX_VISIBILITY_PRESET hidden
  VISIBILITY_INLINES_HIDDEN ON
)

endif()
