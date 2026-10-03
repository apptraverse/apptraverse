cmake_minimum_required(VERSION 3.20)

project(AppTraverse
  VERSION 0.1.0
  LANGUAGES CXX C
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Emscripten pthreads require atomics/bulk-memory on every TU that ends up in
# a -pthread link. Apply once for the whole tree (including aether-objects).
if(EMSCRIPTEN)
  add_compile_options(-pthread -fexceptions)
  add_link_options(-pthread)
endif()

include(cmake/CPM.cmake)
include(cmake/aether_version.cmake)
include(cmake/apptraverse_compile_policy.cmake)

# Quiet Aether telemetry unless the caller passes USER_CONFIG.
if(NOT USER_CONFIG OR USER_CONFIG STREQUAL "")
  get_filename_component(_apptraverse_aether_user_config
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/aether_user_config.h"
    ABSOLUTE)
  set(USER_CONFIG "${_apptraverse_aether_user_config}"
      CACHE PATH "Path to user provided configuration header file" FORCE)
endif()

# Fetch dependncies
# For using CPM.cmake projects with external package managers, such as conan or vcpkg,
# setting the variable CPM_USE_LOCAL_PACKAGES will make CPM.cmake try to add a package through
# find_package first, and add it from source if it doesn't succeed.
# It is possible to override the consumer's dependency with the version by supplying
# the CMake option CPM_<dependency name>_SOURCE set to the absolute path of the local library.
# All dependencies must be EXCLUDE_FROM_ALL FALSE to make them install together with the apptraverse.
# To avoid extra downloads it is recommend to set the CPM_SOURCE_CACHE environmental variable
# Also it required to used PATCHES
#
if (NOT CPM_SOURCE_CACHE)
  set(CPM_SOURCE_CACHE "${CMAKE_CURRENT_BINARY_DIR}/cpm.cache")
endif()

CPMAddPackage(
  NAME aether-miscpp
  GITHUB_REPOSITORY aethernetio/aether-miscpp
  GIT_TAG ${APPTRAVERSE_AETHER_MISCPP_GIT_TAG}
  EXCLUDE_FROM_ALL NO
  OPTIONS
    "AE_INSTALL OFF"
    "AE_BUILD_TESTS OFF"
)
CPMAddPackage(
  NAME aether-tele
  GITHUB_REPOSITORY aethernetio/aether-tele
  GIT_TAG ${APPTRAVERSE_AETHER_TELE_GIT_TAG}
  EXCLUDE_FROM_ALL NO
  OPTIONS
    "AE_TELE_INSTALL OFF"
    "AE_TELE_BUILD_TESTS OFF"
)
CPMAddPackage(
  NAME aether-objects
  GITHUB_REPOSITORY aethernetio/aether-objects
  GIT_TAG ${APPTRAVERSE_AETHER_OBJECTS_GIT_TAG}
  EXCLUDE_FROM_ALL NO
  PATCHES "${APPTRAVERSE_AETHER_OBJECTS_SCOPE_PATCH}"
  OPTIONS
    "AE_INSTALL OFF"
    "AE_BUILD_TESTS OFF"
)

CPMAddPackage(
  NAME aether-client-cpp
  GITHUB_REPOSITORY aethernetio/aether-client-cpp
  GIT_TAG ${APPTRAVERSE_AETHER_GIT_TAG}
  EXCLUDE_FROM_ALL NO
  OPTIONS
    "AE_INSTALL OFF"
    "AE_BUILD_TESTS ${APPTRAVERSE_BUILD_AETHER_DEMOS}"
)

add_library(apptraverse STATIC
  src/apptraverse.cpp
  src/noninteractive_crt.cpp
  src/runtime_lifecycle.cpp
  src/directory_domain_storage.cpp
  src/overlay_domain_storage.cpp
  src/object_serialization.cpp
  src/ui_mirror.cpp
  src/model_runtime.cpp
  src/graph_mirror.cpp
  src/link.cpp
  src/shared_node.cpp
  src/shared_network_graph.cpp
  src/memory_transport.cpp
  src/sync_frame.cpp
  src/shared_sync_runtime.cpp
)
target_include_directories(apptraverse PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)
# Object graph only. Full Aether client is linked by chat/presence targets.
# PUBLIC compile policy so every AppTraverse-owned consumer gets /GR- or -fno-rtti.
target_link_libraries(apptraverse PUBLIC
  aether::objects
  aether::miscpp
  apptraverse_compile_policy
)

target_compile_definitions(apptraverse PUBLIC
  AE_DISTILLATION=1
  AE_FILTRATION=1
)
if(WIN32)
  target_compile_definitions(apptraverse PUBLIC
    NOMINMAX
    WIN32_LEAN_AND_MEAN
  )
endif()
target_compile_features(apptraverse PUBLIC cxx_std_20)

if(APPTRAVERSE_BUILD_AETHER_DEMOS)
  add_subdirectory(examples/model_ui_runtime_demo)
  add_subdirectory(examples/chat_ui_runtime_demo)
  add_subdirectory(examples/aether_presence_monitor)
endif()
add_subdirectory(examples/main_window_runtime_demo)
add_subdirectory(examples/dynamic_objects_demo)
add_subdirectory(examples/surfaces_demo)
add_subdirectory(examples/shared_node_demo)
add_subdirectory(examples/chat_demo)

include(CTest)
if(BUILD_TESTING)
  add_subdirectory(tests)
endif()
