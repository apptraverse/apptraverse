# Generate chat_build_info_generated.h into the build tree.
# Invoked as: cmake -DREPO_ROOT=... -DOUTPUT=... -DCONFIGURATION=... -P this_file
# Rewrites the output only when content changes (no timestamp-only churn).

if(NOT REPO_ROOT OR NOT OUTPUT)
  message(FATAL_ERROR "REPO_ROOT and OUTPUT are required")
endif()
if(NOT CONFIGURATION)
  set(CONFIGURATION "unknown")
endif()

find_program(GIT_EXECUTABLE git REQUIRED)
execute_process(
  COMMAND "${GIT_EXECUTABLE}" rev-parse HEAD
  WORKING_DIRECTORY "${REPO_ROOT}"
  OUTPUT_VARIABLE _sha
  OUTPUT_STRIP_TRAILING_WHITESPACE
  RESULT_VARIABLE _sha_rc)
if(NOT _sha_rc EQUAL 0)
  message(FATAL_ERROR "git rev-parse HEAD failed in ${REPO_ROOT}")
endif()

execute_process(
  COMMAND "${GIT_EXECUTABLE}" status --porcelain
  WORKING_DIRECTORY "${REPO_ROOT}"
  OUTPUT_VARIABLE _porcelain
  OUTPUT_STRIP_TRAILING_WHITESPACE)

set(_dirty 0)
set(_dirty_flag "clean")
if(NOT _porcelain STREQUAL "")
  set(_dirty 1)
  set(_dirty_flag "dirty")
endif()

# Compile-input fingerprint: tracked files that affect chat binaries, excluding
# docs and local helper bats. Unrelated untracked helpers stay out of the hash.
execute_process(
  COMMAND "${GIT_EXECUTABLE}" ls-files -z --
    "cmake/"
    "examples/chat_demo/"
    "include/"
    "src/"
    "tests/chat_session"
    "tests/chat_demo"
    "tests/shared_sync"
    "tests/aether_"
  WORKING_DIRECTORY "${REPO_ROOT}"
  OUTPUT_VARIABLE _files_z
  RESULT_VARIABLE _ls_rc)
# Fallback: hash the source SHA + dirty + key pins when ls-files pathspec is awkward on Windows.
execute_process(
  COMMAND "${GIT_EXECUTABLE}" rev-parse HEAD:cmake/aether_version.cmake
  WORKING_DIRECTORY "${REPO_ROOT}"
  OUTPUT_VARIABLE _cmake_blob
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET)
execute_process(
  COMMAND "${GIT_EXECUTABLE}" rev-parse HEAD:examples/chat_demo
  WORKING_DIRECTORY "${REPO_ROOT}"
  OUTPUT_VARIABLE _chat_tree
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET)
if(_cmake_blob STREQUAL "")
  set(_cmake_blob "unknown")
endif()
if(_chat_tree STREQUAL "")
  set(_chat_tree "unknown")
endif()
set(_fingerprint "${_sha}|${_dirty_flag}|${_cmake_blob}|${_chat_tree}|${CONFIGURATION}")
string(SHA256 _fp_hash "${_fingerprint}")

set(_objects_pin "1d30264737c9bcca8a181161116b66c7dbeeb5fb")
set(_client_pin "0b0e3b54b9ffa730c41597c8b18f6a75255bded3")
set(_miscpp_pin "f8b2e1c60d12fa04fdb63ca46722e111b912d8b4")
file(STRINGS "${REPO_ROOT}/cmake/aether_version.cmake" _pin_lines)
foreach(_line IN LISTS _pin_lines)
  if(_line MATCHES "APPTRAVERSE_AETHER_OBJECTS_GIT_TAG[^\"]*\"([0-9a-f]+)\"")
    set(_objects_pin "${CMAKE_MATCH_1}")
  elseif(_line MATCHES "set\\(APPTRAVERSE_AETHER_GIT_TAG[^\"]*\"([0-9a-f]+)\"")
    set(_client_pin "${CMAKE_MATCH_1}")
  elseif(_line MATCHES "APPTRAVERSE_AETHER_MISCPP_GIT_TAG[^\"]*\"([0-9a-f]+)\"")
    set(_miscpp_pin "${CMAKE_MATCH_1}")
  endif()
endforeach()

set(_patch_id "aether-objects-domain-graph-serialization-scope")
set(_cxx "${CXX_COMPILER_ID}")
if(NOT _cxx)
  set(_cxx "unknown")
endif()

set(_content "#pragma once
// Generated — do not edit. Written only when identity inputs change.
#define APPTRAVERSE_CHAT_SOURCE_SHA \"${_sha}\"
#define APPTRAVERSE_CHAT_SOURCE_DIRTY ${_dirty}
#define APPTRAVERSE_CHAT_SOURCE_DIRTY_FLAG \"${_dirty_flag}\"
#define APPTRAVERSE_CHAT_COMPILE_FINGERPRINT \"${_fp_hash}\"
#define APPTRAVERSE_CHAT_BUILD_CONFIGURATION \"${CONFIGURATION}\"
#define APPTRAVERSE_CHAT_CXX_COMPILER \"${_cxx}\"
#define APPTRAVERSE_CHAT_AETHER_CLIENT_SHA \"${_client_pin}\"
#define APPTRAVERSE_CHAT_AETHER_OBJECTS_SHA \"${_objects_pin}\"
#define APPTRAVERSE_CHAT_AETHER_MISCPP_SHA \"${_miscpp_pin}\"
#define APPTRAVERSE_CHAT_OBJECTS_SCOPE_PATCH \"${_patch_id}\"
")

get_filename_component(_outdir "${OUTPUT}" DIRECTORY)
file(MAKE_DIRECTORY "${_outdir}")
set(_tmp "${OUTPUT}.tmp")
file(WRITE "${_tmp}" "${_content}")
if(EXISTS "${OUTPUT}")
  file(READ "${OUTPUT}" _old)
  if(_old STREQUAL _content)
    file(REMOVE "${_tmp}")
    return()
  endif()
endif()
file(RENAME "${_tmp}" "${OUTPUT}")
message(STATUS "APPTRAVERSE_CHAT_BUILD_INFO written source=${_sha} dirty=${_dirty_flag} fp=${_fp_hash}")
