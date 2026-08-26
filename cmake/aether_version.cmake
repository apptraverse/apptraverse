# Pin for the Aether object/domain sources in aethernetio/aether-client-cpp.
# AppTraverse does not vendor the object system; it builds a subset from that repo.
set(APPTRAVERSE_AETHER_GIT_TAG "941744cdccb364134da5cc61f4edc613465e843a")

# Optional local checkout of aether-client-cpp. When set (or when a sibling
# ../aether-client-cpp exists), CPM uses that tree instead of fetching GitHub.
set(APPTRAVERSE_AETHER_REPO ""
    CACHE PATH "Local aether-client-cpp checkout for the object system")

# Pins for aether-tele / numeric / miscpp so their CPM GIT_TAG main/master
# calls reuse these revisions.
set(APPTRAVERSE_AETHER_MISCPP_GIT_TAG "0e467d9dc53e9f82c8e23fbdd238fceb97e5d504")
set(APPTRAVERSE_AETHER_NUMERIC_GIT_TAG "9e9758a4b57f446caaf387fe268b06b19ab24dcb")
set(APPTRAVERSE_AETHER_TELE_GIT_TAG "79c42274dc2ffce91347a108eec7e0bb392cc83c")
set(APPTRAVERSE_GCEM_GIT_TAG "f182c6f3d6e0742eb9eef4fff506a3928d4c5107")

# Resolve the aether-client-cpp tree: APPTRAVERSE_AETHER_REPO, CPM override,
# or sibling ../aether-client-cpp next to this repository.
macro(apptraverse_prepare_aether_override)
  if(APPTRAVERSE_AETHER_REPO AND NOT APPTRAVERSE_AETHER_REPO STREQUAL "")
    set(_apptraverse_aether_repo "${APPTRAVERSE_AETHER_REPO}")
  elseif(CPM_aether-client-cpp_SOURCE AND NOT CPM_aether-client-cpp_SOURCE STREQUAL "")
    set(_apptraverse_aether_repo "${CPM_aether-client-cpp_SOURCE}")
  else()
    get_filename_component(_apptraverse_aether_repo
      "${CMAKE_CURRENT_SOURCE_DIR}/../aether-client-cpp" ABSOLUTE)
    if(NOT EXISTS "${_apptraverse_aether_repo}/aether/obj/obj.h")
      set(_apptraverse_aether_repo "")
    endif()
  endif()

  if(_apptraverse_aether_repo AND NOT _apptraverse_aether_repo STREQUAL "")
    set(CPM_aether-client-cpp_SOURCE "${_apptraverse_aether_repo}"
        CACHE PATH "Local aether-client-cpp checkout (object system)" FORCE)
  endif()

  set(_apptraverse_aether_override "${CPM_aether-client-cpp_SOURCE}")
endmacro()

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

function(apptraverse_verify_aether_pin)
  if(DEFINED aether-client-cpp_SOURCE_DIR AND NOT aether-client-cpp_SOURCE_DIR STREQUAL "")
    set(_apptraverse_aether_source "${aether-client-cpp_SOURCE_DIR}")
  elseif(DEFINED CPM_PACKAGE_aether-client-cpp_SOURCE_DIR
         AND NOT CPM_PACKAGE_aether-client-cpp_SOURCE_DIR STREQUAL "")
    set(_apptraverse_aether_source "${CPM_PACKAGE_aether-client-cpp_SOURCE_DIR}")
  else()
    message(FATAL_ERROR
      "Could not resolve aether-client-cpp source directory after DOWNLOAD_ONLY fetch")
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
endfunction()
