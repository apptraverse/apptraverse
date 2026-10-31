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
          "${APPTRAVERSE_AETHER_OBJECTS_REJECT_UNKNOWN_STORED_CLASS_PATCH}"
          "${APPTRAVERSE_AETHER_OBJECTS_OBJPTR_RESET_FLAGS_PATCH}"
          "${APPTRAVERSE_AETHER_OBJECTS_FORCE_MATERIALIZE_PATCH}"
          "${APPTRAVERSE_AETHER_OBJECTS_SHOULD_SAVE_VERSION_PATCH}"
          "${APPTRAVERSE_AETHER_OBJECTS_ORIGIN_FACET_PATCH}"
          "${APPTRAVERSE_AETHER_OBJECTS_OBJ_FACET_FACTORY_PATCH}"
          "${APPTRAVERSE_AETHER_OBJECTS_DOMAIN_REGISTERED_OBJECTS_PATCH}"
          "${APPTRAVERSE_AETHER_OBJECTS_CLONE_SAVE_REMAP_PATCH}"
          "${APPTRAVERSE_AETHER_OBJECTS_CLONE_CANONICAL_STATE_PATCH}"
          "${APPTRAVERSE_AETHER_OBJECTS_CLONE_REFERENCE_VISITOR_PATCH}"
          "${APPTRAVERSE_AETHER_OBJECTS_EMSCRIPTEN_DOMAIN_INCLUDE_PATCH}"
          "${APPTRAVERSE_AETHER_OBJECTS_NESTED_OBJ_ARCHIVE_DESERI_PATCH}"
          "${APPTRAVERSE_AETHER_OBJECTS_COLD_LOAD_ROOT_FLUSH_PATCH}"
          "${APPTRAVERSE_AETHER_OBJECTS_RESET_OBJECT_LOAD_STATE_PATCH}"
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
  src/noninteractive_crt.cpp
  src/directory_domain_storage.cpp
  src/overlay_domain_storage.cpp
  src/embedded_distilled_domain.cpp
  src/distilled_domain_export.cpp
  src/distilled_supply_package.cpp
  src/partitioned_model_domain_storage.cpp
  src/model_object_proxy.cpp
  src/object_serialization.cpp
  src/object_clone.cpp
  src/publication_serialization_scope.cpp
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
  src/ui/ui_registration.cpp
)
target_include_directories(apptraverse PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(apptraverse PUBLIC
  aether::objects aether::miscpp apptraverse_compile_policy)
target_compile_features(apptraverse PUBLIC cxx_std_20)
target_compile_definitions(apptraverse PUBLIC AE_DISTILLATION=1)
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
endif()

if(ANDROID)
  add_library(apptraverse_ui_android STATIC
    src/platform/android/android_jni_util.cpp
    src/platform/android/android_ui_bridge.cpp
    src/platform/android/android_application_host.cpp
    src/platform/android/android_ui_window_presenter.cpp
    src/platform/android/android_ui_push_button_presenter.cpp
    src/platform/android/android_ui_list_presenter.cpp
    src/platform/android/android_ui_registration.cpp
  )
  target_include_directories(apptraverse_ui_android PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src/platform/android)
  target_link_libraries(apptraverse_ui_android PUBLIC apptraverse log)
  target_compile_features(apptraverse_ui_android PUBLIC cxx_std_20)
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
if(BUILD_TESTING)
  add_subdirectory(tests)
endif()
