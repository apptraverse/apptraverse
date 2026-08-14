# Single source of truth for the aether-client-cpp pin used by desktop and Android.
set(APPTRAVERSE_AETHER_GIT_TAG "7294f92a0cf749c5d56eedc28673d8089d1f5cb2")

# Capture any explicit -DCPM_aether-client-cpp_SOURCE=... before CPMAddPackage.
macro(apptraverse_prepare_aether_override)
  set(_apptraverse_aether_override "${CPM_aether-client-cpp_SOURCE}")
endmacro()

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

  execute_process(
    COMMAND git rev-parse HEAD
    WORKING_DIRECTORY "${_apptraverse_aether_source}"
    OUTPUT_VARIABLE _apptraverse_aether_sha
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE _apptraverse_aether_git_rc
    ERROR_VARIABLE _apptraverse_aether_git_err
  )
  if(NOT _apptraverse_aether_git_rc EQUAL 0)
    message(FATAL_ERROR
      "git rev-parse HEAD failed in ${_apptraverse_aether_source}: ${_apptraverse_aether_git_err}")
  endif()

  if(_apptraverse_aether_override AND NOT _apptraverse_aether_override STREQUAL "")
    message(STATUS
      "APPTRAVERSE_AETHER_OVERRIDE=1 local_sha=${_apptraverse_aether_sha}")
  else()
    if(NOT _apptraverse_aether_sha STREQUAL APPTRAVERSE_AETHER_GIT_TAG)
      message(FATAL_ERROR
        "Aether SHA mismatch: got ${_apptraverse_aether_sha}, expected ${APPTRAVERSE_AETHER_GIT_TAG} at ${_apptraverse_aether_source}")
    endif()
  endif()
endfunction()
