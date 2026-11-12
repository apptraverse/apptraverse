# Keep the optional SDK out of ordinary model/UI builds. Shared object and
# serialization dependencies have already been added by the root project.
set(APPTRAVERSE_AETHER_CLIENT_GIT_TAG
  "fc61d36c8e30842ca1088020e69621efaba513df")
set(APPTRAVERSE_AETHER_CLIENT_SOURCE "" CACHE PATH
  "Optional isolated checkout of the pinned SDK; the exact upstream compile fix is applied here")

function(_apptraverse_verify_sdk source allow_unpatched)
  execute_process(COMMAND git -C "${source}" rev-parse HEAD
    OUTPUT_VARIABLE sdk_sha OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE sdk_git_result)
  if(NOT sdk_git_result EQUAL 0 OR
     NOT sdk_sha STREQUAL APPTRAVERSE_AETHER_CLIENT_GIT_TAG)
    message(FATAL_ERROR "Aether source must match ${APPTRAVERSE_AETHER_CLIENT_GIT_TAG}; got ${sdk_sha}")
  endif()
  execute_process(COMMAND git -C "${source}" diff --name-only HEAD
    OUTPUT_VARIABLE sdk_changes OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE sdk_status_result)
  if(NOT sdk_status_result EQUAL 0 OR
     (sdk_changes AND NOT sdk_changes STREQUAL "aether/api_protocol/make_api_call_sender.h"))
    message(FATAL_ERROR "Use an isolated SDK checkout; other worktrees' edits are not part of this pin")
  endif()
  if(sdk_changes OR NOT allow_unpatched)
    execute_process(COMMAND git -C "${source}" hash-object
      --path=aether/api_protocol/make_api_call_sender.h aether/api_protocol/make_api_call_sender.h
      OUTPUT_VARIABLE sdk_blob OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT sdk_blob STREQUAL "94e5ab1a70e13b84b2e3a53f2f643a2fe7ce9ba4")
      message(FATAL_ERROR "SDK changes differ from the exact upstream MSVC ApiContext fix")
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
  set(sdk_patch "${CMAKE_CURRENT_SOURCE_DIR}/cmake/patches/aether-client-msvc-api-context.patch")
  if(APPTRAVERSE_AETHER_CLIENT_SOURCE)
    _apptraverse_verify_sdk("${APPTRAVERSE_AETHER_CLIENT_SOURCE}" TRUE)
    execute_process(COMMAND git -C "${APPTRAVERSE_AETHER_CLIENT_SOURCE}"
      apply --reverse --check "${sdk_patch}" RESULT_VARIABLE already_patched
      OUTPUT_QUIET ERROR_QUIET)
    if(NOT already_patched EQUAL 0)
      execute_process(COMMAND git -C "${APPTRAVERSE_AETHER_CLIENT_SOURCE}"
        apply "${sdk_patch}" RESULT_VARIABLE patch_result)
      if(NOT patch_result EQUAL 0)
        message(FATAL_ERROR "Could not apply the upstream MSVC ApiContext fix")
      endif()
    endif()
    set(CPM_aether-client-cpp_SOURCE "${APPTRAVERSE_AETHER_CLIENT_SOURCE}")
  endif()
  CPMAddPackage(
    NAME aether-client-cpp
    GITHUB_REPOSITORY aethernetio/aether-client-cpp
    GIT_TAG ${APPTRAVERSE_AETHER_CLIENT_GIT_TAG}
    PATCHES "${sdk_patch}"
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
endfunction()
