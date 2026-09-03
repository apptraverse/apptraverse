# Pin for aether-client-cpp (network/client) and aether-objects (Obj/Domain).
set(APPTRAVERSE_AETHER_GIT_TAG "0b0e3b54b9ffa730c41597c8b18f6a75255bded3")

# Optional local checkout of aether-client-cpp. When set (or when a sibling
# ../aether-client-cpp exists), CPM uses that tree instead of fetching GitHub.
set(APPTRAVERSE_AETHER_REPO ""
    CACHE PATH "Local aether-client-cpp checkout")

# Pins for owned deps (matched to aether-client-cpp @ APPTRAVERSE_AETHER_GIT_TAG).
set(APPTRAVERSE_AETHER_OBJECTS_GIT_TAG "68df7973014fdd366875b3af725a69750a847e8b")
set(APPTRAVERSE_AETHER_MISCPP_GIT_TAG "f8b2e1c60d12fa04fdb63ca46722e111b912d8b4")
set(APPTRAVERSE_AETHER_NUMERIC_GIT_TAG "3ab9e7310a2f8e6240931261c07c3a0c39771ec2")
set(APPTRAVERSE_AETHER_TELE_GIT_TAG "d46529cdc5159d5b9b091023130d202e83e94fb1")
set(APPTRAVERSE_GCEM_GIT_TAG "f182c6f3d6e0742eb9eef4fff506a3928d4c5107")

# Resolve the aether-client-cpp tree: APPTRAVERSE_AETHER_REPO, CPM override,
# or sibling ../aether-client-cpp next to this repository.
macro(apptraverse_prepare_aether_override)
  if(APPTRAVERSE_AETHER_REPO AND NOT APPTRAVERSE_AETHER_REPO STREQUAL "")
    set(_apptraverse_aether_repo "${APPTRAVERSE_AETHER_REPO}")
  else()
    get_filename_component(_apptraverse_aether_pin
      "${CMAKE_CURRENT_SOURCE_DIR}/.artifacts/aether-pin-941744cd" ABSOLUTE)
    get_filename_component(_apptraverse_aether_sibling
      "${CMAKE_CURRENT_SOURCE_DIR}/../aether-client-cpp" ABSOLUTE)
    if(EXISTS "${_apptraverse_aether_sibling}/aether/aether.h")
      set(_apptraverse_aether_repo "${_apptraverse_aether_sibling}")
    elseif(EXISTS "${_apptraverse_aether_pin}/aether/aether.h")
      set(_apptraverse_aether_repo "${_apptraverse_aether_pin}")
    elseif(CPM_aether-client-cpp_SOURCE AND NOT CPM_aether-client-cpp_SOURCE STREQUAL "")
      set(_apptraverse_aether_repo "${CPM_aether-client-cpp_SOURCE}")
    else()
      set(_apptraverse_aether_repo "")
    endif()
  endif()

  if(_apptraverse_aether_repo AND NOT _apptraverse_aether_repo STREQUAL "")
    set(CPM_aether-client-cpp_SOURCE "${_apptraverse_aether_repo}"
        CACHE PATH "Local aether-client-cpp checkout" FORCE)
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
    NAME ae-numeric
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
    NAME aether-objects
    GITHUB_REPOSITORY aethernetio/aether-objects
    GIT_TAG ${APPTRAVERSE_AETHER_OBJECTS_GIT_TAG}
    EXCLUDE_FROM_ALL NO
    OPTIONS
      "AE_INSTALL OFF"
      "AE_BUILD_TESTS OFF"
  )
  if(MSVC AND TARGET aether-objects)
    target_compile_options(aether-objects PUBLIC /Zc:preprocessor)
  endif()
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
  endif()

  if(NOT _apptraverse_aether_sha STREQUAL APPTRAVERSE_AETHER_GIT_TAG)
    message(FATAL_ERROR
      "Aether SHA mismatch: got ${_apptraverse_aether_sha}, expected ${APPTRAVERSE_AETHER_GIT_TAG} at ${_apptraverse_aether_source}")
  endif()

  _apptraverse_verify_pinned_package("gcem" "${APPTRAVERSE_GCEM_GIT_TAG}")
  _apptraverse_verify_pinned_package("ae-numeric" "${APPTRAVERSE_AETHER_NUMERIC_GIT_TAG}")
  _apptraverse_verify_pinned_package("aether-miscpp" "${APPTRAVERSE_AETHER_MISCPP_GIT_TAG}")
  _apptraverse_verify_pinned_package("aether-tele" "${APPTRAVERSE_AETHER_TELE_GIT_TAG}")
  _apptraverse_verify_pinned_package("aether-objects" "${APPTRAVERSE_AETHER_OBJECTS_GIT_TAG}")
endfunction()
