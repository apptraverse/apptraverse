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
  add_compile_options(-pthread)
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
  PATCHES "${APPTRAVERSE_AETHER_OBJECTS_LOCAL_PERSISTENT_LOAD_PATCH}"
          "${APPTRAVERSE_AETHER_OBJECTS_DOMAIN_STORAGE_ACCESS_PATCH}"
          "${APPTRAVERSE_AETHER_OBJECTS_OBJPTR_RESET_FLAGS_PATCH}"
          "${APPTRAVERSE_AETHER_OBJECTS_FORCE_MATERIALIZE_PATCH}"
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

# This development branch builds only the window catalog application.
# SharedNode/network/chat and the historical demos are not part of its graph.
set(APPTRAVERSE_ENABLE_SHARED_NODE OFF)
set(APPTRAVERSE_BUILD_AETHER_DEMOS OFF)
option(APPTRAVERSE_BUILD_EXAMPLES "Build the window catalog demo" ON)

add_library(apptraverse STATIC
  src/apptraverse.cpp
  src/noninteractive_crt.cpp
  src/directory_domain_storage.cpp
  src/overlay_domain_storage.cpp
  src/partitioned_model_domain_storage.cpp
  src/model_persistence.cpp
  src/model_object_proxy.cpp
  src/object_serialization.cpp
  src/ui_mirror.cpp
  src/model_runtime.cpp
  src/application_model_runtime.cpp
  src/application_ui_session.cpp
  src/embedded_distilled_domain.cpp
  src/graph_mirror.cpp
  src/ui/ui_localization_string.cpp
  src/ui/ui_locale_settings.cpp
  src/ui/ui_window.cpp
  src/ui/ui_desktop_window.cpp
  src/ui/ui_push_button.cpp
  src/ui/ui_push_button_presenter.cpp
  src/ui/ui_list_container.cpp
  src/ui/ui_presenters.cpp
  src/ui/ui_registration.cpp
)
target_include_directories(apptraverse PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(apptraverse PUBLIC
  aether::objects aether::miscpp apptraverse_compile_policy)
target_compile_features(apptraverse PUBLIC cxx_std_20)
target_compile_definitions(apptraverse PUBLIC
  APPTRAVERSE_ENABLE_SHARED_NODE=0 AE_DISTILLATION=1 AE_FILTRATION=1)

# Optional controls do not enter a catalog-only build or its registration.
add_library(apptraverse_ui_extended STATIC EXCLUDE_FROM_ALL
  src/ui/ui_edit_box.cpp
  src/ui/ui_bitmap_image.cpp
  src/ui/ui_presenters_edit_box.cpp
  src/ui/ui_registration_extended.cpp
)
target_link_libraries(apptraverse_ui_extended PUBLIC apptraverse)

if(WIN32)
  target_compile_definitions(apptraverse PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
  add_library(apptraverse_ui_windows STATIC
    src/platform/windows/win_ui_utf8.cpp
    src/platform/windows/win_ui_frame.cpp
    src/platform/windows/win_ui_native_push_button.cpp
    src/platform/windows/win_ui_push_button_presenter.cpp
    src/platform/windows/win_ui_presenters.cpp
    src/platform/windows/win_ui_registration.cpp
    src/platform/windows/win32_fatal.cpp
    src/platform/windows/win_application_message_host.cpp
    src/platform/windows/win_application_loading_window.cpp
    src/platform/windows/win_application_host.cpp
  )
  target_link_libraries(apptraverse_ui_windows PUBLIC
    apptraverse user32 gdi32 comctl32)
  add_library(apptraverse_ui_windows_extended STATIC EXCLUDE_FROM_ALL
    src/platform/windows/win_ui_presenters_extended.cpp
    src/platform/windows/win_ui_registration_extended.cpp
  )
  target_link_libraries(apptraverse_ui_windows_extended PUBLIC
    apptraverse_ui_windows apptraverse_ui_extended)
  if(APPTRAVERSE_BUILD_EXAMPLES)
    add_subdirectory(examples/surfaces_demo)
    add_subdirectory(examples/minimal_app)
  endif()
endif()

include(CTest)
if(BUILD_TESTING)
  add_subdirectory(tests)
endif()
