# Single source of truth for the aether-client-cpp pin used by desktop and Android.
set(APPTRAVERSE_AETHER_GIT_TAG "941744cdccb364134da5cc61f4edc613465e843a")

# Exact Aether-owned dependency revisions recorded from two independent
# configures of the candidate (CPM GIT_TAG main/master, then reused).
# Add these before Æther so its floating GIT_TAG main/master calls reuse
# the pinned sources instead of moving branch heads.
set(APPTRAVERSE_AETHER_MISCPP_GIT_TAG "0e467d9dc53e9f82c8e23fbdd238fceb97e5d504")
set(APPTRAVERSE_AETHER_NUMERIC_GIT_TAG "9e9758a4b57f446caaf387fe268b06b19ab24dcb")
set(APPTRAVERSE_AETHER_TELE_GIT_TAG "79c42274dc2ffce91347a108eec7e0bb392cc83c")
set(APPTRAVERSE_STDEXEC_GIT_TAG "e8c349f3f3425b9341306bc56615fc5279a15cf4")
set(APPTRAVERSE_GCEM_GIT_TAG "f182c6f3d6e0742eb9eef4fff506a3928d4c5107")

# Capture any explicit -DCPM_aether-client-cpp_SOURCE=... before CPMAddPackage.
macro(apptraverse_prepare_aether_override)
  set(_apptraverse_aether_override "${CPM_aether-client-cpp_SOURCE}")
endmacro()

# Add Aether-owned packages first so Æther's later CPMAddPackage(GIT_TAG main)
# reuses these pinned revisions.
function(apptraverse_add_pinned_aether_owned_deps)
  CPMAddPackage(
    NAME gcem
    GITHUB_REPOSITORY aethernetio/gcem
    GIT_TAG ${APPTRAVERSE_GCEM_GIT_TAG}
    EXCLUDE_FROM_ALL NO
  )
  CPMAddPackage(
    NAME numeric
    GITHUB_REPOSITORY aethernetio/aethernet-numeric
    GIT_TAG ${APPTRAVERSE_AETHER_NUMERIC_GIT_TAG}
    EXCLUDE_FROM_ALL NO
    OPTIONS
      "AE_NUMERIC_INSTALL OFF"
      "AE_BUILD_TESTS OFF"
  )
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
    NAME stdexec
    GITHUB_REPOSITORY aethernetio/stdexec
    GIT_TAG ${APPTRAVERSE_STDEXEC_GIT_TAG}
    EXCLUDE_FROM_ALL NO
    OPTIONS
      "STDEXEC_BUILD_EXAMPLES OFF"
      "STDEXEC_INSTALL OFF"
  )
endfunction()

function(apptraverse_add_pinned_aether_miscpp)
  apptraverse_add_pinned_aether_owned_deps()
endfunction()

function(_apptraverse_cpm_source_dir package_name out_var)
  set(_src_var "${package_name}_SOURCE_DIR")
  if(NOT "${${_src_var}}" STREQUAL "")
    set(${out_var} "${${_src_var}}" PARENT_SCOPE)
    return()
  endif()
  set(_src_var "CPM_PACKAGE_${package_name}_SOURCE_DIR")
  if(NOT "${${_src_var}}" STREQUAL "")
    set(${out_var} "${${_src_var}}" PARENT_SCOPE)
    return()
  endif()
  set(${out_var} "" PARENT_SCOPE)
endfunction()

function(_apptraverse_git_head source_dir out_var)
  execute_process(
    COMMAND git rev-parse HEAD
    WORKING_DIRECTORY "${source_dir}"
    OUTPUT_VARIABLE _sha
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE _git_rc
    ERROR_VARIABLE _git_err
  )
  if(NOT _git_rc EQUAL 0)
    message(FATAL_ERROR
      "git rev-parse HEAD failed in ${source_dir}: ${_git_err}")
  endif()
  set(${out_var} "${_sha}" PARENT_SCOPE)
endfunction()

function(_apptraverse_verify_pinned_package package_name expected_sha)
  _apptraverse_cpm_source_dir("${package_name}" _src)
  if(_src STREQUAL "")
    message(FATAL_ERROR
      "Could not resolve ${package_name} source directory after CPMAddPackage")
  endif()
  _apptraverse_git_head("${_src}" _sha)
  message(STATUS "APPTRAVERSE_${package_name}_SOURCE=${_src}")
  message(STATUS "APPTRAVERSE_${package_name}_EXPECTED_SHA=${expected_sha}")
  message(STATUS "APPTRAVERSE_${package_name}_SHA=${_sha}")
  if(NOT _sha STREQUAL expected_sha)
    message(FATAL_ERROR
      "${package_name} SHA mismatch: got ${_sha}, expected ${expected_sha} at ${_src}")
  endif()
endfunction()

# After CPMAddPackage: print resolved source and verify SHA unless user overrode.
function(apptraverse_verify_aether_pin)
  if(DEFINED aether_SOURCE_DIR AND NOT aether_SOURCE_DIR STREQUAL "")
    set(_apptraverse_aether_source "${aether_SOURCE_DIR}")
  elseif(DEFINED CPM_PACKAGE_aether-client-cpp_SOURCE_DIR
         AND NOT CPM_PACKAGE_aether-client-cpp_SOURCE_DIR STREQUAL "")
    set(_apptraverse_aether_source "${CPM_PACKAGE_aether-client-cpp_SOURCE_DIR}")
  else()
    message(FATAL_ERROR
      "Could not resolve aether source directory after CPMAddPackage")
  endif()

  message(STATUS "APPTRAVERSE_AETHER_SOURCE=${_apptraverse_aether_source}")
  message(STATUS "APPTRAVERSE_AETHER_EXPECTED_SHA=${APPTRAVERSE_AETHER_GIT_TAG}")

  _apptraverse_git_head("${_apptraverse_aether_source}" _apptraverse_aether_sha)

  if(_apptraverse_aether_override AND NOT _apptraverse_aether_override STREQUAL "")
    message(STATUS
      "APPTRAVERSE_AETHER_OVERRIDE=1 local_sha=${_apptraverse_aether_sha}")
  else()
    if(NOT _apptraverse_aether_sha STREQUAL APPTRAVERSE_AETHER_GIT_TAG)
      message(FATAL_ERROR
        "Aether SHA mismatch: got ${_apptraverse_aether_sha}, expected ${APPTRAVERSE_AETHER_GIT_TAG} at ${_apptraverse_aether_source}")
    endif()
  endif()

  _apptraverse_verify_pinned_package("gcem" "${APPTRAVERSE_GCEM_GIT_TAG}")
  _apptraverse_verify_pinned_package("numeric" "${APPTRAVERSE_AETHER_NUMERIC_GIT_TAG}")
  _apptraverse_verify_pinned_package("aether-miscpp" "${APPTRAVERSE_AETHER_MISCPP_GIT_TAG}")
  _apptraverse_verify_pinned_package("aether-tele" "${APPTRAVERSE_AETHER_TELE_GIT_TAG}")
  _apptraverse_verify_pinned_package("stdexec" "${APPTRAVERSE_STDEXEC_GIT_TAG}")
endfunction()
