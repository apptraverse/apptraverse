# Standalone Apple CMake entry point for the single-client chat example.
# Desktop/root CMake never adds this directory. Invoke this file as the
# top-level project for macOS x86_64 or iOS Simulator x86_64.

cmake_minimum_required(VERSION 3.25)

project(apptraverse_apple_chat LANGUAGES C CXX OBJCXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_OSX_ARCHITECTURES "x86_64" CACHE STRING "Apple product architecture" FORCE)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
  set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0" CACHE STRING "" FORCE)
  enable_language(Swift)
  set(CMAKE_Swift_LANGUAGE_VERSION 5)
else()
  set(CMAKE_OSX_DEPLOYMENT_TARGET "16.0" CACHE STRING "" FORCE)
endif()

get_filename_component(APPTRAVERSE_ROOT
  "${CMAKE_CURRENT_LIST_DIR}/../../.." REALPATH)

if(NOT DEFINED CPM_SOURCE_CACHE AND NOT DEFINED ENV{CPM_SOURCE_CACHE})
  set(CPM_SOURCE_CACHE "${APPTRAVERSE_ROOT}/.cpm_cache"
    CACHE PATH "CPM.cmake download cache")
endif()

if(NOT DEFINED USER_CONFIG OR USER_CONFIG STREQUAL "")
  get_filename_component(_apptraverse_aether_user_config
    "${CMAKE_CURRENT_LIST_DIR}/native/aether_user_config.h" ABSOLUTE)
  set(USER_CONFIG "${_apptraverse_aether_user_config}"
    CACHE STRING "Path to the aether user configuration header")
endif()

set(BUILD_TESTING OFF CACHE BOOL "" FORCE)

# Apple Clang 15 libc++ omits <stop_token>. Put the local C++20 shim first so
# Aether's #include <stop_token> resolves without patching the pinned dependency.
include_directories(BEFORE
  "${CMAKE_CURRENT_LIST_DIR}/native/compat"
)

add_subdirectory(${APPTRAVERSE_ROOT} ${CMAKE_CURRENT_BINARY_DIR}/apptraverse)

add_library(AppTraverseAppleChat SHARED
  native/apple_chat_runtime.cpp
  native/apple_chat_bridge.mm
  ${APPTRAVERSE_ROOT}/examples/single_client_chat/common/aether_p2p_framing.cpp
  ${APPTRAVERSE_ROOT}/examples/single_client_chat/common/aether_p2p_transport.cpp
)

set_source_files_properties(native/apple_chat_bridge.mm PROPERTIES
  COMPILE_FLAGS "-fobjc-arc"
)

target_include_directories(AppTraverseAppleChat PUBLIC
  ${CMAKE_CURRENT_LIST_DIR}/native
)
target_include_directories(AppTraverseAppleChat PRIVATE
  ${APPTRAVERSE_ROOT}/examples/single_client_chat/common
)

target_link_libraries(AppTraverseAppleChat PRIVATE
  apptraverse
  single_client_chat_common
  "-framework Foundation"
)

set_target_properties(AppTraverseAppleChat PROPERTIES
  OUTPUT_NAME AppTraverseAppleChat
  CXX_STANDARD 20
  MACOSX_RPATH ON
  BUILD_WITH_INSTALL_RPATH ON
  INSTALL_NAME_DIR "@rpath"
  PUBLIC_HEADER native/apple_chat_bridge.h
)

if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
  set_target_properties(AppTraverseAppleChat PROPERTIES
    FRAMEWORK TRUE
    MACOSX_FRAMEWORK_IDENTIFIER com.apptraverse.AppleChat
    XCODE_ATTRIBUTE_DEFINES_MODULE YES
  )
endif()

if(NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
  add_executable(AppTraverseChatMacDemo
    Sources/AppTraverseChatAppleUI/ChatBackend.swift
    Sources/AppTraverseChatAppleUI/AppleChatBackend.swift
    Sources/AppTraverseChatAppleUI/ChatViewModel.swift
    Sources/AppTraverseChatAppleUI/ChatView.swift
    Sources/AppTraverseChatMacDemo/AppTraverseChatMacDemo.swift
  )

  set_target_properties(AppTraverseChatMacDemo PROPERTIES
    Swift_MODULE_NAME AppTraverseChatMacDemo
    MACOSX_BUNDLE FALSE
    BUILD_RPATH "$<TARGET_FILE_DIR:AppTraverseAppleChat>"
    INSTALL_RPATH "@executable_path"
  )

  target_compile_options(AppTraverseChatMacDemo PRIVATE
    "$<$<COMPILE_LANGUAGE:Swift>:SHELL:-import-objc-header ${CMAKE_CURRENT_LIST_DIR}/native/apple_chat_bridge.h>"
    "$<$<COMPILE_LANGUAGE:Swift>:-target>"
    "$<$<COMPILE_LANGUAGE:Swift>:x86_64-apple-macosx13.0>"
    "$<$<COMPILE_LANGUAGE:Swift>:-DAPPTRAVERSE_APPLE_NATIVE>"
  )

  target_link_libraries(AppTraverseChatMacDemo PRIVATE
    AppTraverseAppleChat
    "-framework Foundation"
    "-framework AppKit"
    "-framework SwiftUI"
    "-framework Combine"
  )
endif()
