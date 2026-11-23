# Keep the optional SDK out of ordinary model/UI builds. Shared object and
# serialization dependencies have already been added by the root project.
set(APPTRAVERSE_AETHER_CLIENT_GIT_TAG
  "4163a08ebf8a841bbb4f1f21319143e792b1399a")
set(_apptraverse_hierarchy_default ON)
if(APPTRAVERSE_BUILD_SHARED_NODE_CHAT)
  set(_apptraverse_hierarchy_default OFF)
endif()
option(APPTRAVERSE_AETHER_STREAM_HIERARCHY
  "Enable the Æther SDK stream hierarchy and SharedNode stream attachment"
  ${_apptraverse_hierarchy_default})
set(_apptraverse_native_websocket_default OFF)
if(WIN32 AND APPTRAVERSE_BUILD_SHARED_NODE_CHAT)
  set(_apptraverse_native_websocket_default ON)
endif()
option(APPTRAVERSE_AETHER_NATIVE_WEBSOCKET
  "Build the native Æther WebSocket/WSS backend"
  ${_apptraverse_native_websocket_default})
set(APPTRAVERSE_AETHER_CLIENT_SOURCE "" CACHE PATH "Optional clean SDK checkout at the pinned upstream revision")
option(APPTRAVERSE_AETHER_USE_WORKSPACE_SOURCE "Build directly from an explicitly selected SDK working tree" OFF)
if(EMSCRIPTEN)
  set(_apptraverse_shared_aether_default_config "${CMAKE_CURRENT_LIST_DIR}/shared_aether_web_config.h")
else()
  set(_apptraverse_shared_aether_default_config "${CMAKE_CURRENT_LIST_DIR}/shared_aether_config.h")
endif()
set(APPTRAVERSE_AETHER_USER_CONFIG "${_apptraverse_shared_aether_default_config}"
    CACHE FILEPATH "Aether transport configuration for this build")
option(APPTRAVERSE_AETHER_FAULT_PROXY "Enable process-scoped socket fault hooks for explicit acceptance tests" OFF)
function(_apptraverse_verify_sdk source)
  execute_process(COMMAND git -C "${source}" rev-parse HEAD
    OUTPUT_VARIABLE sdk_sha OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
  execute_process(COMMAND git -C "${source}" -c core.fileMode=false
    -c core.autocrlf=true diff --name-only HEAD
    OUTPUT_VARIABLE sdk_changes OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
  if(NOT sdk_sha STREQUAL APPTRAVERSE_AETHER_CLIENT_GIT_TAG OR sdk_changes)
    message(FATAL_ERROR "Use a clean pinned upstream SDK checkout; got ${sdk_sha}")
  endif()
endfunction()

function(apptraverse_add_shared_aether)
  set(USER_CONFIG "${APPTRAVERSE_AETHER_USER_CONFIG}")
  set(AE_ENABLE_STREAM_HIERARCHY "${APPTRAVERSE_AETHER_STREAM_HIERARCHY}")
  set(AE_NATIVE_WEBSOCKET ${APPTRAVERSE_AETHER_NATIVE_WEBSOCKET}
      CACHE BOOL "Native Æther WebSocket/WSS backend" FORCE)
  if(NOT APPTRAVERSE_AETHER_STREAM_HIERARCHY)
    set(AE_SUPPORT_ADAPTIVE_DATAGRAM OFF)
  endif()
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
    set(sdk_checkout "${APPTRAVERSE_AETHER_CLIENT_SOURCE}")
  else()
    CPMAddPackage(
      NAME aether-client-cpp
      GITHUB_REPOSITORY aethernetio/aether-client-cpp
      GIT_TAG ${APPTRAVERSE_AETHER_CLIENT_GIT_TAG}
      DOWNLOAD_ONLY YES)
    set(sdk_checkout "${aether-client-cpp_SOURCE_DIR}")
  endif()
  if(APPTRAVERSE_AETHER_USE_WORKSPACE_SOURCE)
    if(NOT APPTRAVERSE_AETHER_CLIENT_SOURCE)
      message(FATAL_ERROR "Workspace SDK mode requires APPTRAVERSE_AETHER_CLIENT_SOURCE")
    endif()
    set(sdk_source "${sdk_checkout}")
  else()
  _apptraverse_verify_sdk("${sdk_checkout}")

  # Build from an owned SDK copy. Keep both the CPM checkout and an optional
  # caller checkout pristine, and preserve the pinned wire format.
  set(sdk_patch "${APPTRAVERSE_CMAKE_DIR}/patches/aether-client-no-exceptions-wasm32-v2.patch")
  set(peer_patch "${APPTRAVERSE_CMAKE_DIR}/patches/aether-client-peer-no-exceptions.patch")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${sdk_patch}" "${peer_patch}")
  file(SHA256 "${sdk_patch}" patch_hash)
  file(SHA256 "${peer_patch}" peer_patch_hash)
  string(SHA256 patch_hash "${patch_hash}${peer_patch_hash}")
  string(SUBSTRING "${patch_hash}" 0 12 patch_id)
  string(SUBSTRING "${APPTRAVERSE_AETHER_CLIENT_GIT_TAG}" 0 12 sdk_id)
  set(sdk_source "${CMAKE_CURRENT_BINARY_DIR}/_deps/ae-${sdk_id}-${patch_id}")
  if(NOT EXISTS "${sdk_source}/apptraverse-patched")
    file(MAKE_DIRECTORY "${sdk_source}")
    if(NOT EXISTS "${sdk_source}/.git")
      execute_process(COMMAND git clone --shared --no-checkout
        "${sdk_checkout}" "${sdk_source}" COMMAND_ERROR_IS_FATAL ANY)
    endif()
    execute_process(COMMAND git -C "${sdk_source}" checkout --force --detach
      "${APPTRAVERSE_AETHER_CLIENT_GIT_TAG}" COMMAND_ERROR_IS_FATAL ANY)
    # The patch may be checked out with CRLF when a Linux build shares a Windows
    # worktree. Match context across that line-ending difference.
    execute_process(COMMAND git -C "${sdk_source}" apply --ignore-whitespace --whitespace=nowarn
      "${sdk_patch}" "${peer_patch}" COMMAND_ERROR_IS_FATAL ANY)
    file(TOUCH "${sdk_source}/apptraverse-patched")
  endif()
  endif()
  add_subdirectory("${sdk_source}" "${CMAKE_CURRENT_BINARY_DIR}/_deps/aether-client-cpp-build" EXCLUDE_FROM_ALL)
  if(NOT TARGET aether)
    message(FATAL_ERROR "The pinned Aether SDK did not define its target")
  endif()
  if(NOT EXISTS "${sdk_source}/aether/telemetry_namespaces.h")
    message(FATAL_ERROR "Shared transport telemetry requires the coordinated SDK sources. Select APPTRAVERSE_AETHER_CLIENT_SOURCE and APPTRAVERSE_AETHER_USE_WORKSPACE_SOURCE=ON; publish coordinated dependency commits before consuming updated remote pins.")
  endif()
  if(WIN32)
    target_compile_definitions(aether PRIVATE
      WINVER=0x0601 _WIN32_WINNT=0x0601 _STL_WIN32_WINNT=0x0601
      _HAS_EXCEPTIONS=0)
    target_compile_options(aether PRIVATE
      /GR- /EHs-c- /utf-8 /Zc:preprocessor /Zc:__cplusplus /Zc:externConstexpr /bigobj
      $<$<CONFIG:MinSizeRel>:/GS->)
    if(TARGET ixwebsocket)
      target_compile_definitions(ixwebsocket PRIVATE _HAS_EXCEPTIONS=0)
      target_compile_options(ixwebsocket PRIVATE /EHs-c-
        $<$<CONFIG:MinSizeRel>:/GS->)
    endif()
    if(TARGET aether-tele)
      target_compile_definitions(aether-tele PRIVATE _HAS_EXCEPTIONS=0)
      target_compile_options(aether-tele PRIVATE /EHs-c-
        $<$<CONFIG:MinSizeRel>:/GS->)
    endif()
  elseif(NOT APPTRAVERSE_AETHER_USE_WORKSPACE_SOURCE)
    target_link_libraries(aether PRIVATE apptraverse_compile_policy)
  endif()
  if(APPTRAVERSE_AETHER_FAULT_PROXY)
    if(NOT WIN32)
      message(FATAL_ERROR "The explicit socket fault proxy currently supports Windows only")
    endif()
    target_compile_definitions(aether PUBLIC AE_BENCH_TCP_PROXY=1)
  endif()
  add_library(apptraverse_shared_aether STATIC EXCLUDE_FROM_ALL
    src/aether_link.cpp src/aether_connection.cpp)
  target_link_libraries(apptraverse_shared_aether PUBLIC
    apptraverse_shared_node aether)
  set_property(TARGET apptraverse_shared_aether PROPERTY APPTRAVERSE_SDK_SOURCE_DIR
    "${sdk_source}")
endfunction()
