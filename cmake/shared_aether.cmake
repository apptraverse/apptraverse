# Keep the optional SDK out of ordinary model/UI builds. Shared object and
# serialization dependencies have already been added by the root project.
set(APPTRAVERSE_AETHER_CLIENT_GIT_TAG
  "91aea53310335778b2bb4803cffff43d2b19b59b")
set(APPTRAVERSE_AETHER_CLIENT_SOURCE "" CACHE PATH "Optional clean SDK checkout at the pinned upstream revision")
if(EMSCRIPTEN)
  set(_apptraverse_shared_aether_default_config "${CMAKE_CURRENT_LIST_DIR}/shared_aether_web_config.h")
else()
  set(_apptraverse_shared_aether_default_config "${CMAKE_CURRENT_LIST_DIR}/shared_aether_config.h")
endif()
set(APPTRAVERSE_AETHER_USER_CONFIG "${_apptraverse_shared_aether_default_config}"
    CACHE FILEPATH "Aether transport configuration for this build")
option(APPTRAVERSE_AETHER_FAULT_PROXY "Enable process-scoped socket fault hooks for explicit acceptance tests" OFF)
function(_apptraverse_verify_sdk source allow_unpatched)
  execute_process(COMMAND git -C "${source}" rev-parse HEAD
    OUTPUT_VARIABLE sdk_sha OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
  execute_process(COMMAND git -C "${source}" diff --name-only HEAD
    OUTPUT_VARIABLE sdk_changes OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
  if(NOT sdk_sha STREQUAL APPTRAVERSE_AETHER_CLIENT_GIT_TAG OR sdk_changes)
    message(FATAL_ERROR "Use a clean pinned upstream SDK checkout; got ${sdk_sha}")
  endif()
endfunction()

function(apptraverse_add_shared_aether)
  set(USER_CONFIG "${APPTRAVERSE_AETHER_USER_CONFIG}")
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
  if(APPTRAVERSE_AETHER_CLIENT_SOURCE)
    _apptraverse_verify_sdk("${APPTRAVERSE_AETHER_CLIENT_SOURCE}" TRUE)
    set(CPM_aether-client-cpp_SOURCE "${APPTRAVERSE_AETHER_CLIENT_SOURCE}")
  endif()
  CPMAddPackage(
    NAME aether-client-cpp
    GITHUB_REPOSITORY aethernetio/aether-client-cpp
    GIT_TAG ${APPTRAVERSE_AETHER_CLIENT_GIT_TAG}
    EXCLUDE_FROM_ALL YES)
  if(NOT TARGET aether)
    message(FATAL_ERROR "The pinned Aether SDK did not define its target")
  endif()
  _apptraverse_verify_sdk("${aether-client-cpp_SOURCE_DIR}" FALSE)
  target_link_libraries(aether PUBLIC apptraverse_compile_policy)
  if(APPTRAVERSE_AETHER_FAULT_PROXY)
    if(NOT WIN32)
      message(FATAL_ERROR "The explicit socket fault proxy currently supports Windows only")
    endif()
    target_compile_definitions(aether PUBLIC AE_BENCH_TCP_PROXY=1)
  endif()
  add_library(apptraverse_shared_aether STATIC EXCLUDE_FROM_ALL
    src/aether_link.cpp)
  target_link_libraries(apptraverse_shared_aether PUBLIC
    apptraverse_shared_node aether)
  set_property(TARGET apptraverse_shared_aether PROPERTY APPTRAVERSE_SDK_SOURCE_DIR
    "${aether-client-cpp_SOURCE_DIR}")
endfunction()
