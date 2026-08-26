cmake_minimum_required(VERSION 3.20)

project(AppTraverse
  VERSION 0.1.0
  LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(cmake/aether_version.cmake)
include(cmake/aether_object.cmake)
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

# Object system lives in aethernetio/aether-client-cpp (aether/obj, aether/ptr,
# aether/domain_storage). Fetch pinned sources unless a local checkout is set.
# Do not add_subdirectory the full client: that builds sockets, crypto, and P2P.
CPMAddPackage(
  NAME aether-client-cpp
  GITHUB_REPOSITORY aethernetio/aether-client-cpp
  GIT_TAG ${APPTRAVERSE_AETHER_GIT_TAG}
  DOWNLOAD_ONLY YES
)

apptraverse_verify_aether_pin()
apptraverse_add_object_aether()

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
