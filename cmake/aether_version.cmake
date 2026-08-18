# Single source of truth for the aether-client-cpp pin used by desktop and Android.
set(APPTRAVERSE_AETHER_GIT_TAG "7294f92a0cf749c5d56eedc28673d8089d1f5cb2")

# Last aether-miscpp revision compatible with the pinned Æther source.
# Æther 7294f92a includes aether-miscpp/reflect/domain_visitor.h.
# aether-miscpp 54aaaff ("new reflect and new separated domain visitor")
# moved that header to aether-miscpp/domain_visitor/. Æther still fetches
# aether-miscpp with floating GIT_TAG main; pin the parent of that move.
set(APPTRAVERSE_AETHER_MISCPP_GIT_TAG "eabf068d369ec98e4d541ea229f1c8401e186b66")

# Capture any explicit -DCPM_aether-client-cpp_SOURCE=... before CPMAddPackage.
macro(apptraverse_prepare_aether_override)
  set(_apptraverse_aether_override "${CPM_aether-client-cpp_SOURCE}")
endmacro()

# Add aether-miscpp first so Æther's later CPMAddPackage(GIT_TAG main) reuses
# this pinned revision instead of floating main.
function(apptraverse_add_pinned_aether_miscpp)
  CPMAddPackage(
    NAME aether-miscpp
    GITHUB_REPOSITORY aethernetio/aether-miscpp
    GIT_TAG ${APPTRAVERSE_AETHER_MISCPP_GIT_TAG}
    EXCLUDE_FROM_ALL NO
    OPTIONS
      "AE_INSTALL OFF"
      "AE_BUILD_TESTS OFF"
  )
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

  _apptraverse_cpm_source_dir("aether-miscpp" _apptraverse_miscpp_source)
  if(_apptraverse_miscpp_source STREQUAL "")
    message(FATAL_ERROR
      "Could not resolve aether-miscpp source directory after CPMAddPackage")
  endif()
  _apptraverse_git_head("${_apptraverse_miscpp_source}" _apptraverse_miscpp_sha)
  message(STATUS "APPTRAVERSE_AETHER_MISCPP_SOURCE=${_apptraverse_miscpp_source}")
  message(STATUS "APPTRAVERSE_AETHER_MISCPP_EXPECTED_SHA=${APPTRAVERSE_AETHER_MISCPP_GIT_TAG}")
  message(STATUS "APPTRAVERSE_AETHER_MISCPP_SHA=${_apptraverse_miscpp_sha}")
  if(NOT _apptraverse_miscpp_sha STREQUAL APPTRAVERSE_AETHER_MISCPP_GIT_TAG)
    message(FATAL_ERROR
      "aether-miscpp SHA mismatch: got ${_apptraverse_miscpp_sha}, expected ${APPTRAVERSE_AETHER_MISCPP_GIT_TAG} at ${_apptraverse_miscpp_source}")
  endif()
endfunction()
