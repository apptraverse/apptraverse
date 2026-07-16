cmake_minimum_required(VERSION 3.20)

project(AppTraverse
  VERSION 0.1.0
  LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(cmake/CPM.cmake)

CPMAddPackage(
  NAME aether-client-cpp
  GITHUB_REPOSITORY aethernetio/aether-client-cpp
  GIT_TAG 48bde3d44a2919457335661ec7b1ab4f05b29167
  EXCLUDE_FROM_ALL NO
  OPTIONS
    "AE_BUILD_TOOLS OFF"
    "AE_BUILD_EXAMPLES OFF"
    "AE_BUILD_TESTS OFF"
    "AE_INSTALL OFF"
)

add_library(apptraverse STATIC
  src/directory_domain_storage.cpp
)
target_include_directories(apptraverse PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(apptraverse PUBLIC aether)
target_compile_features(apptraverse PUBLIC cxx_std_20)

include(CTest)
if(BUILD_TESTING)
  add_subdirectory(tests)
endif()
