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
  GIT_TAG main
  EXCLUDE_FROM_ALL NO
  OPTIONS
    "AE_BUILD_TOOLS OFF"
    "AE_BUILD_EXAMPLES OFF"
    "AE_BUILD_TESTS OFF"
    "AE_INSTALL OFF"
)
