cmake_minimum_required(VERSION 3.20)

# Keep Debug's shared CRT for development. Every release configuration, including
# dependencies created below, uses the static C/C++ runtime (CMP0091 is NEW).
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:DebugDLL>")

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
  add_compile_options(-pthread)
  add_link_options(-pthread)
endif()

include(cmake/CPM.cmake)
include(cmake/aether_version.cmake)
include(cmake/apptraverse_compile_policy.cmake)
include(cmake/apple_min_size.cmake)

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
# Resolve the application-owned numeric revision before telemetry/SDK request it.
CPMAddPackage(
  NAME ae-numeric
  GITHUB_REPOSITORY aethernetio/aethernet-numeric
  GIT_TAG ${APPTRAVERSE_AETHER_NUMERIC_GIT_TAG}
  OPTIONS "AE_NUMERIC_INSTALL OFF" "AE_BUILD_TESTS OFF"
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
set(AE_OBJECTS_RAW_TELE ON CACHE BOOL
    "Route object-system telemetry through its independent raw space")
CPMAddPackage(
  NAME aether-objects
  GITHUB_REPOSITORY aethernetio/aether-objects
  GIT_TAG ${APPTRAVERSE_AETHER_OBJECTS_GIT_TAG}
  EXCLUDE_FROM_ALL NO
  PATCHES ${APPTRAVERSE_AETHER_OBJECTS_PATCHES}
  OPTIONS
    "AE_INSTALL OFF"
    "AE_BUILD_TESTS OFF"
)

# The same exception/STL policy is required on the object/serialization
# dependency TUs as on their consumers. No parent CMake cache is rewritten.
foreach(_dependency aether::objects aether::miscpp)
  get_target_property(_real_target ${_dependency} ALIASED_TARGET)
  if(NOT _real_target)
    set(_real_target ${_dependency})
  endif()
  get_target_property(_type ${_real_target} TYPE)
  if(_type STREQUAL "INTERFACE_LIBRARY")
    target_link_libraries(${_real_target} INTERFACE apptraverse_compile_policy)
  else()
    target_link_libraries(${_real_target} PUBLIC apptraverse_compile_policy)
  endif()
endforeach()

option(APPTRAVERSE_BUILD_EXAMPLES
  "Build the window catalog and minimal application" ON)
option(APPTRAVERSE_BUILD_LEGACY_EXAMPLES
  "Build historical demos (main window, dynamic objects) and their tests" OFF)
option(APPTRAVERSE_BUILD_LINUX_GUI "Build Linux GTK hosts and native GUI tests" ON)
add_library(apptraverse STATIC
  src/apptraverse.cpp
  src/object_registration.cpp
  src/noninteractive_crt.cpp
  src/single_file_user_storage.cpp
  src/directory_domain_storage.cpp
  src/overlay_domain_storage.cpp
  src/embedded_distilled_domain.cpp
  src/distilled_domain_export.cpp
  src/distilled_supply_package.cpp
  src/partitioned_model_domain_storage.cpp
  src/model_object_proxy.cpp
  src/object_serialization.cpp
  src/graph_walk_registry.cpp
  src/object_clone.cpp
  src/node_history.cpp
  src/ui_mirror.cpp
  src/model_runtime.cpp
  src/application_model_runtime.cpp
  src/application_ui_session.cpp
  src/graph_mirror.cpp
  src/closed_event_graph.cpp
  src/ui/ui_localization_string.cpp
  src/ui/ui_locale_settings.cpp
  src/ui/ui_window.cpp
  src/ui/ui_desktop_window.cpp
  src/ui/ui_push_button.cpp
  src/ui/ui_push_button_presenter.cpp
  src/ui/ui_list_container.cpp
  src/ui/ui_presenters.cpp
  src/ui/ui_window_presenter.cpp
  src/ui/ui_registration.cpp
  src/ui/ui_list_row_presenter_registration.cpp
)
target_include_directories(apptraverse PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(apptraverse PUBLIC
  aether::objects aether::miscpp apptraverse_compile_policy)
target_compile_features(apptraverse PUBLIC cxx_std_20)

# SharedNode is an explicitly linked headless extension. Existing application
# registrations and native hosts do not acquire replication/runtime objects.
add_library(apptraverse_shared_node STATIC EXCLUDE_FROM_ALL
  src/shared_node.cpp
  src/shared_graph.cpp
  src/shared_sync.cpp
  src/memory_link.cpp
  src/p2p_message_fragments.cpp
)
target_link_libraries(apptraverse_shared_node PUBLIC apptraverse)

option(APPTRAVERSE_BUILD_AETHER_LINK
  "Build the opt-in SharedNode Aether transport and headless cloud worker" OFF)
if(APPTRAVERSE_BUILD_AETHER_LINK)
  include(cmake/shared_aether.cmake)
  apptraverse_add_shared_aether()
  add_library(apptraverse_shared_chat_session STATIC EXCLUDE_FROM_ALL src/chat_session.cpp)
  target_link_libraries(apptraverse_shared_chat_session PUBLIC apptraverse_shared_aether)
  set(APPTRAVERSE_CHAT_SUPPLY_HEADER "" CACHE FILEPATH "Chat supply prepared by the Debug exporter")
  if(APPTRAVERSE_CHAT_SUPPLY_HEADER)
    if(NOT EXISTS "${APPTRAVERSE_CHAT_SUPPLY_HEADER}")
      message(FATAL_ERROR "Chat supply header does not exist: ${APPTRAVERSE_CHAT_SUPPLY_HEADER}")
    endif()
    target_compile_definitions(apptraverse_shared_chat_session PRIVATE
      APPTRAVERSE_CHAT_DISTILLATION=0
      "APPTRAVERSE_CHAT_EMBEDDED_HEADER=\"${APPTRAVERSE_CHAT_SUPPLY_HEADER}\"")
  else()
    target_compile_definitions(apptraverse_shared_chat_session PRIVATE
      "APPTRAVERSE_CHAT_DISTILLATION=$<IF:$<CONFIG:Debug>,1,0>")
    add_executable(apptraverse_chat_supply_exporter EXCLUDE_FROM_ALL
      examples/shared_node_chat/export_chat_supply.cpp)
    target_link_libraries(apptraverse_chat_supply_exporter PRIVATE apptraverse_shared_chat_session)
  endif()
endif()

if(ANDROID)
  # noninteractive_crt.cpp reports fatal errors through the Android log API.
  target_link_libraries(apptraverse PUBLIC log)
endif()

if(WIN32)
  target_compile_definitions(apptraverse PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
  add_library(apptraverse_ui_windows STATIC
    src/platform/windows/win_ui_utf8.cpp
    src/platform/windows/win_ui_frame.cpp
    src/platform/windows/win_ui_native_push_button.cpp
    src/platform/windows/win_ui_push_button_presenter.cpp
    src/platform/windows/win_ui_presenters.cpp
    src/platform/windows/win_ui_registration.cpp
    src/platform/windows/win_window_registration.cpp
    src/platform/windows/win32_fatal.cpp
    src/platform/windows/win_application_message_host.cpp
    src/platform/windows/win_application_loading_window.cpp
    src/platform/windows/win_application_host.cpp
  )
  target_link_libraries(apptraverse_ui_windows PUBLIC
    apptraverse user32 gdi32 comctl32)
endif()

# Android presenter classes without their JNI Views: the build-host exporter
# distills the Android MinimalApp supply from these.
add_library(apptraverse_ui_android_presenters STATIC
  src/platform/android/android_ui_presenters.cpp
  src/platform/android/android_ui_registration.cpp
  src/platform/android/android_ui_window_presenter_host.cpp
)
target_include_directories(apptraverse_ui_android_presenters PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(apptraverse_ui_android_presenters PUBLIC apptraverse)
target_compile_features(apptraverse_ui_android_presenters PUBLIC cxx_std_20)

add_library(apptraverse_ui_ios_presenters STATIC
  src/platform/ios/ios_ui_presenters.cpp
)
target_include_directories(apptraverse_ui_ios_presenters PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(apptraverse_ui_ios_presenters PUBLIC apptraverse)
target_compile_features(apptraverse_ui_ios_presenters PUBLIC cxx_std_20)

if(APPLE AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
  enable_language(OBJC OBJCXX)
  set(CMAKE_OBJCXX_STANDARD 20)
  set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
  if(NOT CMAKE_Swift_COMPILER)
    execute_process(
      COMMAND xcrun --find swiftc
      OUTPUT_VARIABLE _apptraverse_swiftc_path
      OUTPUT_STRIP_TRAILING_WHITESPACE)
    set(CMAKE_Swift_COMPILER "${_apptraverse_swiftc_path}")
  endif()
  set(CMAKE_Swift_FLAGS "-sdk ${CMAKE_OSX_SYSROOT}")
  enable_language(Swift)

  add_library(apptraverse_ui_macos_presenters STATIC
    src/platform/macos/macos_ui_presenters.cpp
  )
  target_include_directories(apptraverse_ui_macos_presenters PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include)
  target_link_libraries(apptraverse_ui_macos_presenters PUBLIC apptraverse)
  target_compile_features(apptraverse_ui_macos_presenters PUBLIC cxx_std_20)

  add_library(apptraverse_ui_macos_bridge STATIC
    src/platform/macos/macos_application_bridge.mm)
  target_include_directories(apptraverse_ui_macos_bridge PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include)
  target_link_libraries(apptraverse_ui_macos_bridge PUBLIC
    apptraverse_ui_macos_presenters
    "-framework Foundation")
  set_source_files_properties(
    src/platform/macos/macos_application_bridge.mm
    PROPERTIES COMPILE_FLAGS "-fobjc-arc")
  target_compile_options(apptraverse_ui_macos_bridge PRIVATE
    $<$<COMPILE_LANGUAGE:OBJCXX>:-fno-rtti>
    $<$<COMPILE_LANGUAGE:OBJCXX>:-fno-objc-exceptions>)

  add_library(apptraverse_ui_macos_swift STATIC
    src/platform/macos/MacApplicationHost.swift)
  set_target_properties(apptraverse_ui_macos_swift PROPERTIES
    Swift_MODULE_NAME AppTraverseMacUI)
  target_compile_options(apptraverse_ui_macos_swift PRIVATE
    "SHELL:-I ${CMAKE_CURRENT_SOURCE_DIR}/include/apptraverse/platform/macos"
    "$<$<CONFIG:Debug>:-enable-testing>")
  target_include_directories(apptraverse_ui_macos_swift INTERFACE
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include/apptraverse/platform/macos)

  add_library(apptraverse_ui_macos INTERFACE)
  target_link_libraries(apptraverse_ui_macos INTERFACE
    apptraverse_ui_macos_bridge
    apptraverse_ui_macos_swift
    "-framework AppKit"
    "-framework Foundation"
    "-framework SwiftUI")
  target_link_options(apptraverse_ui_macos INTERFACE
    -L${CMAKE_OSX_SYSROOT}/usr/lib/swift
    -L/usr/lib/swift
    -Wl,-rpath,/usr/lib/swift)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
  enable_language(OBJC OBJCXX)
  set(CMAKE_OBJCXX_STANDARD 20)
  set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
  if(NOT CMAKE_Swift_COMPILER)
    execute_process(
      COMMAND xcrun --find swiftc
      OUTPUT_VARIABLE _apptraverse_swiftc_path
      OUTPUT_STRIP_TRAILING_WHITESPACE)
    set(CMAKE_Swift_COMPILER "${_apptraverse_swiftc_path}")
  endif()
  set(CMAKE_Swift_FLAGS "-sdk ${CMAKE_OSX_SYSROOT}")
  # A simulator target cannot be linked into a physical-device application.
  string(TOLOWER "${CMAKE_OSX_SYSROOT}" _apptraverse_ios_sysroot)
  if(_apptraverse_ios_sysroot MATCHES "iphonesimulator")
    set(_apptraverse_ios_swift_platform iphonesimulator)
    set(_apptraverse_ios_target_suffix -simulator)
  else()
    set(_apptraverse_ios_swift_platform iphoneos)
    set(_apptraverse_ios_target_suffix "")
  endif()
  set(CMAKE_Swift_COMPILER_TARGET
    ${CMAKE_OSX_ARCHITECTURES}-apple-ios${CMAKE_OSX_DEPLOYMENT_TARGET}${_apptraverse_ios_target_suffix})
  enable_language(Swift)

  add_library(apptraverse_ui_ios_bridge STATIC
    src/platform/ios/ios_application_bridge.mm)
  target_include_directories(apptraverse_ui_ios_bridge PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include)
  target_link_libraries(apptraverse_ui_ios_bridge PUBLIC
    apptraverse_ui_ios_presenters
    "-framework Foundation")
  set_source_files_properties(
    src/platform/ios/ios_application_bridge.mm
    PROPERTIES COMPILE_FLAGS "-fobjc-arc")
  target_compile_options(apptraverse_ui_ios_bridge PRIVATE
    $<$<COMPILE_LANGUAGE:OBJCXX>:-fno-rtti>
    $<$<COMPILE_LANGUAGE:OBJCXX>:-fno-objc-exceptions>)

  add_library(apptraverse_ui_ios_swift STATIC
    src/platform/ios/IosApplicationHost.swift)
  set_target_properties(apptraverse_ui_ios_swift PROPERTIES
    Swift_MODULE_NAME AppTraverseIosUI)
  target_compile_options(apptraverse_ui_ios_swift PRIVATE
    "SHELL:-I ${CMAKE_CURRENT_SOURCE_DIR}/include/apptraverse/platform/ios"
    "$<$<OR:$<CONFIG:Debug>,$<BOOL:${APPTRAVERSE_BUILD_IOS_ACCEPTANCE}>>:-enable-testing>")
  target_include_directories(apptraverse_ui_ios_swift INTERFACE
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include/apptraverse/platform/ios)

  add_library(apptraverse_ui_ios INTERFACE)
  target_link_libraries(apptraverse_ui_ios INTERFACE
    apptraverse_ui_ios_bridge
    apptraverse_ui_ios_swift
    "-framework UIKit"
    "-framework Foundation"
    "-framework SwiftUI")
  get_filename_component(_ios_swift_bin_dir ${CMAKE_Swift_COMPILER} DIRECTORY)
  set(_ios_swift_runtime_dir ${_ios_swift_bin_dir}/../lib/swift/${_apptraverse_ios_swift_platform})
  target_link_options(apptraverse_ui_ios INTERFACE
    -L${CMAKE_OSX_SYSROOT}/usr/lib/swift
    -L${_ios_swift_runtime_dir}
    -Wl,-rpath,/usr/lib/swift)
endif()

if(ANDROID)
  add_library(apptraverse_ui_android STATIC
    src/platform/android/android_jni_util.cpp
    src/platform/android/android_ui_bridge.cpp
    src/platform/android/android_application_host.cpp
    src/platform/android/android_ui_layout_scale.cpp
    src/platform/android/android_ui_window_presenter.cpp
    src/platform/android/android_ui_push_button_presenter.cpp
    src/platform/android/android_ui_list_presenter.cpp
  )
  target_include_directories(apptraverse_ui_android PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src/platform/android)
  target_link_libraries(apptraverse_ui_android PUBLIC
    apptraverse_ui_android_presenters apptraverse log)
  target_compile_features(apptraverse_ui_android PUBLIC cxx_std_20)
endif()

if(UNIX AND NOT APPLE AND NOT ANDROID AND NOT EMSCRIPTEN AND
    APPTRAVERSE_BUILD_LINUX_GUI)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(GTK3 REQUIRED IMPORTED_TARGET gtk+-3.0)
  pkg_check_modules(X11 IMPORTED_TARGET x11)
  add_library(apptraverse_ui_linux STATIC
    src/platform/linux/linux_fatal.cpp
    src/platform/linux/linux_application_host.cpp
    src/platform/linux/linux_desktop_window_stack.cpp
    src/platform/linux/linux_ui_client_mount.cpp
    src/platform/linux/linux_ui_window_presenter.cpp
    src/platform/linux/linux_ui_list_presenter.cpp
    src/platform/linux/linux_ui_push_button_presenter.cpp
    src/platform/linux/linux_ui_registration.cpp
  )
  target_include_directories(apptraverse_ui_linux PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include)
  target_link_libraries(apptraverse_ui_linux PUBLIC apptraverse PkgConfig::GTK3)
  if(TARGET PkgConfig::X11)
    target_link_libraries(apptraverse_ui_linux PUBLIC PkgConfig::X11)
  endif()
  target_compile_features(apptraverse_ui_linux PUBLIC cxx_std_20)
endif()

if(EMSCRIPTEN)
  add_library(apptraverse_ui_web STATIC
    src/platform/web/web_ui_dom_em.cpp
    src/platform/web/web_ui_window_presenter.cpp
    src/platform/web/web_ui_push_button_presenter.cpp
    src/platform/web/web_ui_list_presenter.cpp
    src/platform/web/web_application_host.cpp
    src/platform/web/web_idbfs.cpp
    src/platform/web/web_ui_registration.cpp
  )
  target_include_directories(apptraverse_ui_web PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include)
  target_link_libraries(apptraverse_ui_web PUBLIC apptraverse)
  target_compile_definitions(apptraverse_ui_web PUBLIC
    EMSCRIPTEN_HAS_UNBOUND_TYPE_NAMES=0)
  target_compile_options(apptraverse_ui_web PRIVATE
    -pthread -fno-rtti -fno-exceptions)
endif()

set(_apptraverse_surfaces_common_sources
  src/surfaces/surfaces_model.cpp
  src/surfaces/surfaces_localization.cpp
  src/surfaces/surfaces_registration.cpp
  src/surfaces/desktop_surface_presenter.cpp
  src/surfaces/surface_window_presenter.cpp
  src/surfaces/surface_button_presenter.cpp
  src/surfaces/surface_language_button_presenter.cpp
  src/surfaces/surfaces_chrome_wiring.cpp
  src/surfaces/surfaces_presentation_size_queue.cpp
)

function(apptraverse_add_surfaces_library target revision)
  set(_sources ${_apptraverse_surfaces_common_sources})
  if(revision EQUAL 1)
    list(APPEND _sources src/surfaces/surfaces_v1_hooks_impl.cpp)
  endif()
  add_library(${target} STATIC ${_sources})
  target_link_libraries(${target} PUBLIC apptraverse)
  target_include_directories(${target} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
  )
  target_compile_features(${target} PUBLIC cxx_std_20)
  target_compile_definitions(${target} PUBLIC
    APPTRAVERSE_SURFACE_DEMO_REVISION=${revision})
  if(MSVC)
    target_compile_options(${target} PRIVATE /bigobj)
  endif()
endfunction()

apptraverse_add_surfaces_library(apptraverse_surfaces 0)
apptraverse_add_surfaces_library(apptraverse_surfaces_v1 1)

if(WIN32)
  function(apptraverse_add_surfaces_windows_library target surfaces_lib)
    add_library(${target} STATIC
      src/platform/windows/surfaces_win_presenters.cpp
      src/platform/windows/surfaces_win32_z_order.cpp
      src/platform/windows/surfaces_win_registration.cpp
    )
    target_link_libraries(${target} PUBLIC
      ${surfaces_lib}
      apptraverse_ui_windows
      user32
      gdi32
    )
    target_include_directories(${target} PUBLIC
      ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_compile_features(${target} PUBLIC cxx_std_20)
  endfunction()

  apptraverse_add_surfaces_windows_library(apptraverse_surfaces_windows
    apptraverse_surfaces)
  apptraverse_add_surfaces_windows_library(apptraverse_surfaces_windows_v1
    apptraverse_surfaces_v1)
endif()

if(APPTRAVERSE_BUILD_LEGACY_EXAMPLES)
  add_subdirectory(examples/main_window_runtime_demo)
  add_subdirectory(examples/dynamic_objects_demo)
endif()

if(APPTRAVERSE_BUILD_EXAMPLES OR BUILD_TESTING)
  add_subdirectory(examples/surfaces_demo)
endif()

if(APPTRAVERSE_BUILD_EXAMPLES OR BUILD_TESTING)
  add_subdirectory(examples/minimal_app)
endif()

include(CTest)
option(APPTRAVERSE_BUILD_SHARED_NODE_CHAT
  "Build the native SharedNode chat over Aether" OFF)
if(APPTRAVERSE_BUILD_SHARED_NODE_CHAT)
  if((NOT WIN32 AND NOT APPLE) OR NOT APPTRAVERSE_BUILD_AETHER_LINK)
    message(FATAL_ERROR "SharedNode chat requires Windows/macOS/iOS and APPTRAVERSE_BUILD_AETHER_LINK")
  endif()
  add_subdirectory(examples/shared_node_chat)
endif()
if(BUILD_TESTING)
  add_subdirectory(tests)
endif()
