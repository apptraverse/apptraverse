# Simplifies fingerprinting: workdir is always required from the caller.
# (ExternalProject substitutes <SOURCE_DIR>; tests pass an absolute path.)

cmake_minimum_required(VERSION 3.14)

if(NOT DEFINED APPTRAVERSE_PATCH_FILE OR APPTRAVERSE_PATCH_FILE STREQUAL "")
  message(FATAL_ERROR "cpm_apply_one_patch: APPTRAVERSE_PATCH_FILE is required")
endif()
if(NOT EXISTS "${APPTRAVERSE_PATCH_FILE}")
  message(FATAL_ERROR
    "cpm_apply_one_patch: patch file not found: ${APPTRAVERSE_PATCH_FILE}")
endif()

if(NOT DEFINED APPTRAVERSE_PATCH_WORKDIR OR APPTRAVERSE_PATCH_WORKDIR STREQUAL "")
  message(FATAL_ERROR "cpm_apply_one_patch: APPTRAVERSE_PATCH_WORKDIR is required")
endif()
set(_workdir "${APPTRAVERSE_PATCH_WORKDIR}")
if(NOT IS_DIRECTORY "${_workdir}")
  message(FATAL_ERROR
    "cpm_apply_one_patch: workdir is not a directory: ${_workdir}")
endif()

find_program(APPTRAVERSE_PATCH_EXECUTABLE NAMES patch patch.exe)
if(NOT APPTRAVERSE_PATCH_EXECUTABLE)
  message(FATAL_ERROR "cpm_apply_one_patch: `patch` executable not found")
endif()

function(apptraverse_fingerprint workdir out_var)
  set(_paths "")
  file(GLOB_RECURSE _files LIST_DIRECTORIES false "${workdir}/*")
  foreach(_f IN LISTS _files)
    if(_f MATCHES "\\.rej$")
      continue()
    endif()
    file(MD5 "${_f}" _hash)
    file(RELATIVE_PATH _rel "${workdir}" "${_f}")
    list(APPEND _paths "${_rel}=${_hash}")
  endforeach()
  list(SORT _paths)
  string(JOIN "\n" _joined ${_paths})
  set(${out_var} "${_joined}" PARENT_SCOPE)
endfunction()

function(apptraverse_run_patch workdir patch_file reverse dry_run out_rc out_log)
  set(_args -p1)
  if(reverse)
    list(APPEND _args -R)
  endif()
  if(dry_run)
    list(APPEND _args --dry-run)
  endif()
  execute_process(
    COMMAND "${APPTRAVERSE_PATCH_EXECUTABLE}" ${_args}
    INPUT_FILE "${patch_file}"
    WORKING_DIRECTORY "${workdir}"
    RESULT_VARIABLE _rc
    OUTPUT_VARIABLE _out
    ERROR_VARIABLE _err)
  set(${out_rc} "${_rc}" PARENT_SCOPE)
  set(${out_log} "${_out}${_err}" PARENT_SCOPE)
endfunction()

apptraverse_fingerprint("${_workdir}" _before)

set(_fwd_rc 1)
set(_fwd_log "")
apptraverse_run_patch("${_workdir}" "${APPTRAVERSE_PATCH_FILE}" OFF ON
                      _fwd_rc _fwd_log)

if(_fwd_rc EQUAL 0)
  set(_apply_rc 1)
  set(_apply_log "")
  apptraverse_run_patch("${_workdir}" "${APPTRAVERSE_PATCH_FILE}" OFF OFF
                        _apply_rc _apply_log)
  if(NOT _apply_rc EQUAL 0)
    message(FATAL_ERROR
      "cpm_apply_one_patch: forward dry-run succeeded but apply failed\n"
      "patch=${APPTRAVERSE_PATCH_FILE}\nworkdir=${_workdir}\n"
      "${_apply_log}")
  endif()
  apptraverse_fingerprint("${_workdir}" _after)
  if(_after STREQUAL _before)
    message(FATAL_ERROR
      "cpm_apply_one_patch: apply reported success but tree content did not "
      "change\npatch=${APPTRAVERSE_PATCH_FILE}\nworkdir=${_workdir}")
  endif()
  return()
endif()

set(_rev_rc 1)
set(_rev_log "")
apptraverse_run_patch("${_workdir}" "${APPTRAVERSE_PATCH_FILE}" ON ON
                      _rev_rc _rev_log)
if(_rev_rc EQUAL 0)
  apptraverse_fingerprint("${_workdir}" _after)
  if(NOT _after STREQUAL _before)
    message(FATAL_ERROR
      "cpm_apply_one_patch: reverse dry-run succeeded (already applied) but "
      "the tree changed unexpectedly\npatch=${APPTRAVERSE_PATCH_FILE}")
  endif()
  return()
endif()

message(FATAL_ERROR
  "cpm_apply_one_patch: patch is not applicable (corrupt, incompatible, or "
  "partial)\npatch=${APPTRAVERSE_PATCH_FILE}\nworkdir=${_workdir}\n"
  "forward_dry_run=${_fwd_log}\nreverse_dry_run=${_rev_log}")
