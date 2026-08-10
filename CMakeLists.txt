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
  GIT_TAG 0a3ea72483f9156ed59186caf4da9fb127b4654d
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

add_library(apptraverse STATIC
  src/apptraverse.cpp
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

if(WIN32)
  add_subdirectory(examples/single_client_chat)
endif()
