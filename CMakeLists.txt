cmake_minimum_required(VERSION 3.20)

project(AppTraverse
  VERSION 0.1.0
  LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Aether gates Winsock helpers on WIN32. MinGW defines _WIN32 but not WIN32 unless
# a Windows header was already included, which breaks SocketInitializer ODR.
if(MINGW)
  add_compile_definitions(WIN32)
endif()

include(cmake/aether_version.cmake)
include(cmake/CPM.cmake)

# Quiet Aether telemetry unless the caller passes USER_CONFIG.
if(NOT USER_CONFIG OR USER_CONFIG STREQUAL "")
  get_filename_component(_apptraverse_aether_user_config
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/aether_user_config.h"
    ABSOLUTE)
  set(USER_CONFIG "${_apptraverse_aether_user_config}"
      CACHE PATH "Path to user provided configuration header file" FORCE)
endif()

apptraverse_prepare_aether_override()
apptraverse_add_pinned_aether_owned_deps()

CPMAddPackage(
  NAME aether-client-cpp
  GITHUB_REPOSITORY aethernetio/aether-client-cpp
  GIT_TAG ${APPTRAVERSE_AETHER_GIT_TAG}
  EXCLUDE_FROM_ALL NO
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

# Candidate Aether/miscpp headers trip MSVC C4702 (if constexpr) under Æther's /WX.
if(MSVC)
  target_compile_options(aether PRIVATE /wd4702)
endif()

add_library(apptraverse STATIC
  src/apptraverse.cpp
  src/directory_domain_storage.cpp
  src/ui_materialized.cpp
  src/ui_mirror.cpp
  src/model_runtime.cpp
)
target_include_directories(apptraverse PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(apptraverse PUBLIC aether)
target_compile_features(apptraverse PUBLIC cxx_std_20)

add_subdirectory(examples/model_ui_runtime_demo)

include(CTest)
if(BUILD_TESTING)
  add_subdirectory(tests)
endif()
