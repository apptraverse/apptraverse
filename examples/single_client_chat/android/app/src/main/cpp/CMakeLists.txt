cmake_minimum_required(VERSION 3.22.1)

project(apptraverse_single_client_chat LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT APPTRAVERSE_ROOT)
  message(FATAL_ERROR "APPTRAVERSE_ROOT must be set (absolute path to AppTraverse)")
endif()

include("${APPTRAVERSE_ROOT}/cmake/aether_version.cmake")

# USER_CONFIG must be set before aether is added so the cache is visible to it.
# Respect an explicit user/Gradle value; otherwise use the AppTraverse-owned header.
if(NOT USER_CONFIG OR USER_CONFIG STREQUAL "")
  set(_apptraverse_aether_user_config
      "${CMAKE_CURRENT_LIST_DIR}/aether_user_config.h")
  set(USER_CONFIG "${_apptraverse_aether_user_config}"
      CACHE PATH "Path to user provided configuration header file")
endif()

apptraverse_prepare_aether_override()

include("${APPTRAVERSE_ROOT}/cmake/CPM.cmake")

CPMAddPackage(
  NAME aether-client-cpp
  GITHUB_REPOSITORY aethernetio/aether-client-cpp
  GIT_TAG ${APPTRAVERSE_AETHER_GIT_TAG}
  OPTIONS
    "AE_BUILD_TOOLS OFF"
    "AE_BUILD_EXAMPLES OFF"
    "AE_BUILD_TESTS OFF"
    "AE_INSTALL OFF"
    "AE_DISTILLATION ON"
    "AE_FILTRATION ON"
    "AE_BUILD_ANDROID_SMOKE OFF"
)

apptraverse_verify_aether_pin()

add_library(apptraverse STATIC
  "${APPTRAVERSE_ROOT}/src/apptraverse.cpp"
  "${APPTRAVERSE_ROOT}/src/directory_domain_storage.cpp"
)
target_include_directories(apptraverse PUBLIC
  "${APPTRAVERSE_ROOT}/include"
)
target_link_libraries(apptraverse PUBLIC aether)
target_compile_features(apptraverse PUBLIC cxx_std_20)

# cpp -> app/src/main/cpp -> ../../../../ = android/
set(_android_dir "${CMAKE_CURRENT_LIST_DIR}/../../../..")
set(_native_dir "${_android_dir}/native")
set(_common_dir "${_android_dir}/../common")

add_library(apptraverse_single_client_chat SHARED
  "${_native_dir}/jni_bridge.cpp"
  "${_native_dir}/native_runtime.cpp"
  "${_native_dir}/ui_bridge.cpp"
)
set_target_properties(apptraverse_single_client_chat PROPERTIES
  OUTPUT_NAME apptraverse_single_client_chat
)
target_include_directories(apptraverse_single_client_chat PRIVATE
  "${_native_dir}"
  "${_common_dir}"
)
target_link_libraries(apptraverse_single_client_chat PRIVATE
  log
  android
  aether
  apptraverse
)
