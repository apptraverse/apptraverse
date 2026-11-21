# Opt-in minimum-profile evidence for the actual app link, including LTO builds.
include_guard(GLOBAL)

function(apptraverse_catalog_binary_contract target)
  if(NOT APPTRAVERSE_MINIMUM_64 OR MSVC OR APPLE OR
     NOT CMAKE_BUILD_TYPE STREQUAL "MinSizeRel")
    return()
  endif()
  cmake_parse_arguments(CONTRACT "NETWORK_ONLY" "" "" ${ARGN})
  find_package(Python3 3.9 REQUIRED COMPONENTS Interpreter)
  set(_inspector "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../scripts/inspect_catalog_binary.py")
  set(_map "${CMAKE_CURRENT_BINARY_DIR}/${target}-$<CONFIG>.map")
  target_link_options(${target} PRIVATE "-Wl,-Map,${_map}")
  set(_artifact "$<TARGET_FILE:${target}>")
  if(EMSCRIPTEN)
    set(_artifact "$<TARGET_FILE_DIR:${target}>/$<TARGET_FILE_BASE_NAME:${target}>.wasm")
  endif()
  set(_mode)
  if(CONTRACT_NETWORK_ONLY)
    set(_mode --network-only)
  endif()
  set(_command "${Python3_EXECUTABLE}" "${_inspector}" "${_artifact}"
      --map "${_map}" --json "${_map}.json" ${_mode})
  set_property(TARGET ${target} PROPERTY APPTRAVERSE_CATALOG_BINARY_CONTRACT_COMMAND
    "${_command}")
  add_custom_target(${target}_binary_contract
    COMMAND ${_command}
    DEPENDS ${target}
    VERBATIM)
endfunction()

# Register from tests/ after the root enables CTest.
function(apptraverse_catalog_binary_contract_test target)
  if(NOT TARGET ${target}_binary_contract)
    return()
  endif()
  get_property(_command TARGET ${target} PROPERTY APPTRAVERSE_CATALOG_BINARY_CONTRACT_COMMAND)
  add_test(NAME ${target}_binary_contract COMMAND ${_command} CONFIGURATIONS MinSizeRel)
  set_tests_properties(${target}_binary_contract PROPERTIES
    LABELS "window_catalog;binary_contract")
endfunction()
