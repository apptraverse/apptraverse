# Keep the optional SDK out of ordinary model/UI builds. Shared object and
# serialization dependencies have already been added by the root project.
set(APPTRAVERSE_AETHER_CLIENT_GIT_TAG
  "5ddf5eaaac5bd154ec384ff8c74f247a0ab80fb7")
set(APPTRAVERSE_AETHER_INTEGRATED_GIT_TAG
  "423ab1e1364a05cd4349764bde0d2341a18c82de")
include(${CMAKE_CURRENT_LIST_DIR}/shared_aether_manifest.cmake)
set(APPTRAVERSE_AETHER_CLIENT_SOURCE "" CACHE PATH
  "Optional isolated SDK checkout; the frozen native API port and Debug fix are applied here")

function(_apptraverse_sdk_blob source path output)
  execute_process(COMMAND git -C "${source}" hash-object --path=${path} "${path}"
    OUTPUT_VARIABLE blob OUTPUT_STRIP_TRAILING_WHITESPACE RESULT_VARIABLE result)
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "Cannot verify SDK file ${path}")
  endif()
  set(${output} "${blob}" PARENT_SCOPE)
endfunction()

function(_apptraverse_verify_sdk source allow_unpatched)
  execute_process(COMMAND git -C "${source}" rev-parse HEAD
    OUTPUT_VARIABLE sdk_sha OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE sdk_git_result)
  if(NOT sdk_git_result EQUAL 0 OR
     (NOT sdk_sha STREQUAL APPTRAVERSE_AETHER_CLIENT_GIT_TAG AND
      NOT sdk_sha STREQUAL APPTRAVERSE_AETHER_INTEGRATED_GIT_TAG))
    message(FATAL_ERROR "Use the pinned public SDK base or frozen integration; got ${sdk_sha}")
  endif()
  execute_process(COMMAND git -C "${source}" diff --name-only HEAD
    OUTPUT_VARIABLE sdk_changes OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE sdk_status_result)
  if(NOT sdk_status_result EQUAL 0)
    message(FATAL_ERROR "Cannot inspect the isolated SDK checkout")
  endif()
  if(allow_unpatched AND NOT sdk_changes AND
     sdk_sha STREQUAL APPTRAVERSE_AETHER_CLIENT_GIT_TAG)
    return()
  endif()
  set(allowed "CMakeLists.txt" "aether/registration/registration.cpp")
  foreach(row IN LISTS APPTRAVERSE_AETHER_PORT_FILES)
    string(REPLACE "|" ";" fields "${row}")
    list(GET fields 0 path)
    list(GET fields 1 expected)
    list(APPEND allowed "${path}")
    _apptraverse_sdk_blob("${source}" "${path}" actual)
    if(NOT actual STREQUAL expected)
      message(FATAL_ERROR "SDK native port differs at ${path}")
    endif()
  endforeach()
  string(REPLACE "\n" ";" changed_files "${sdk_changes}")
  foreach(path IN LISTS changed_files)
    if(NOT path IN_LIST allowed)
      message(FATAL_ERROR "Use an isolated SDK checkout; unexpected change: ${path}")
    endif()
  endforeach()
  _apptraverse_sdk_blob("${source}" "CMakeLists.txt" sdk_cmake)
  if(NOT sdk_cmake STREQUAL APPTRAVERSE_AETHER_BASE_CMAKE_BLOB AND
     NOT sdk_cmake STREQUAL APPTRAVERSE_AETHER_INTEGRATED_CMAKE_BLOB)
    message(FATAL_ERROR "Unexpected SDK root CMake changes")
  endif()
  _apptraverse_sdk_blob("${source}" "aether/registration/registration.cpp" registration)
  if(NOT registration STREQUAL "a5834d1347c704535de397517d3bee0201b70800" AND
     NOT (allow_unpatched AND registration STREQUAL APPTRAVERSE_AETHER_BASE_REGISTRATION_BLOB))
    message(FATAL_ERROR "SDK registration differs from the exact Debug capture fix")
  endif()
endfunction()

function(_apptraverse_apply_sdk_patch source patch)
  execute_process(COMMAND git -C "${source}" apply --reverse --check "${patch}"
    RESULT_VARIABLE already_patched OUTPUT_QUIET ERROR_QUIET)
  if(NOT already_patched EQUAL 0)
    execute_process(COMMAND git -C "${source}" apply "${patch}" RESULT_VARIABLE result)
    if(NOT result EQUAL 0)
      message(FATAL_ERROR "Could not apply SDK patch ${patch}")
    endif()
  endif()
endfunction()

function(apptraverse_add_shared_aether)
  set(USER_CONFIG "${CMAKE_CURRENT_SOURCE_DIR}/cmake/shared_aether_config.h")
  set(AE_BUILD_TESTS OFF CACHE BOOL "SDK tests" FORCE)
  set(AE_BUILD_EXAMPLES OFF CACHE BOOL "SDK examples" FORCE)
  set(AE_BUILD_TOOLS OFF CACHE BOOL "SDK tools" FORCE)
  set(AE_INSTALL OFF CACHE BOOL "SDK installation" FORCE)
  set(AE_DISTILLATION OFF CACHE BOOL "SDK fresh identity policy" FORCE)
  set(AE_FILTRATION ON CACHE BOOL "SDK persisted identity loading" FORCE)
  set(AE_BUILD_PRESENCE_BENCH OFF CACHE BOOL "SDK diagnostic hooks" FORCE)
  set(STDEXEC_BUILD_TESTS OFF CACHE BOOL "stdexec tests" FORCE)
  set(STDEXEC_BUILD_EXAMPLES OFF CACHE BOOL "stdexec examples" FORCE)
  set(STDEXEC_INSTALL OFF CACHE BOOL "stdexec installation" FORCE)
  set(sdk_patch "${CMAKE_CURRENT_SOURCE_DIR}/cmake/patches/aether-client-debug-registration-capture.patch")
  set(sdk_port "${CMAKE_CURRENT_SOURCE_DIR}/cmake/patches/aether-client-shared-native-presence.patch")
  if(APPTRAVERSE_AETHER_CLIENT_SOURCE)
    _apptraverse_verify_sdk("${APPTRAVERSE_AETHER_CLIENT_SOURCE}" TRUE)
    _apptraverse_apply_sdk_patch("${APPTRAVERSE_AETHER_CLIENT_SOURCE}" "${sdk_port}")
    _apptraverse_apply_sdk_patch("${APPTRAVERSE_AETHER_CLIENT_SOURCE}" "${sdk_patch}")
    set(CPM_aether-client-cpp_SOURCE "${APPTRAVERSE_AETHER_CLIENT_SOURCE}")
  endif()
  CPMAddPackage(
    NAME aether-client-cpp
    GITHUB_REPOSITORY aethernetio/aether-client-cpp
    GIT_TAG ${APPTRAVERSE_AETHER_CLIENT_GIT_TAG}
    PATCHES "${sdk_port}" "${sdk_patch}"
    EXCLUDE_FROM_ALL YES)
  if(NOT TARGET aether)
    message(FATAL_ERROR "The pinned Aether SDK did not define its target")
  endif()
  _apptraverse_verify_sdk("${aether-client-cpp_SOURCE_DIR}" FALSE)
  target_link_libraries(aether PUBLIC apptraverse_compile_policy)
  add_library(apptraverse_shared_aether STATIC EXCLUDE_FROM_ALL
    src/aether_link.cpp)
  target_link_libraries(apptraverse_shared_aether PUBLIC
    apptraverse_shared_node aether)
  set_property(TARGET apptraverse_shared_aether PROPERTY APPTRAVERSE_SDK_SOURCE_DIR
    "${aether-client-cpp_SOURCE_DIR}")
endfunction()
