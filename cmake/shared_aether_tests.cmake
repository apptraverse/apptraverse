# Explicit checks of the frozen SDK port without enabling its whole test tree.
CPMAddPackage(NAME Unity
  GITHUB_REPOSITORY ThrowTheSwitch/Unity
  GIT_TAG 54f864cf6027a50f4cc5424616e9e01e1bb729da
  EXCLUDE_FROM_ALL YES)
target_compile_definitions(unity PUBLIC UNITY_INCLUDE_DOUBLE)
get_target_property(_sdk_root apptraverse_shared_aether APPTRAVERSE_SDK_SOURCE_DIR)
add_executable(apptraverse_aether_native_presence_test
  "${_sdk_root}/tests/test-presence-api/main.cpp")
target_link_libraries(apptraverse_aether_native_presence_test PRIVATE
  aether unity)
add_test(NAME apptraverse_aether_native_presence_test
  COMMAND apptraverse_aether_native_presence_test)
foreach(_suite request-response pending-responses sender-lifecycle registration)
  string(REPLACE "-" "_" _entry "${_suite}")
  set(_target "apptraverse_aether_sdk_${_entry}_test")
  add_executable(${_target}
    "${_sdk_root}/tests/test-api-protocol/suite-main.cpp"
    "${_sdk_root}/tests/test-api-protocol/test-${_suite}.cpp")
  target_compile_definitions(${_target} PRIVATE API_PROTOCOL_SUITE=test_api_protocol_${_entry})
  target_link_libraries(${_target} PRIVATE aether unity)
  if(MSVC)
    target_link_options(${_target} PRIVATE /STACK:16777216)
  endif()
  add_test(NAME ${_target} COMMAND ${_target})
  set_tests_properties(${_target} PROPERTIES LABELS "aether;headless;sdk" TIMEOUT 60)
endforeach()
set_tests_properties(apptraverse_aether_native_presence_test PROPERTIES
  LABELS "aether;headless;sdk" TIMEOUT 60)

add_executable(apptraverse_aether_channels_test
  "${PROJECT_SOURCE_DIR}/tests/aether_channels_main.cpp"
  "${_sdk_root}/tests/test-server-connection/test_cloud_persistence.cpp")
target_compile_definitions(apptraverse_aether_channels_test PRIVATE
  API_PROTOCOL_SUITE=run_test_cloud_persistence)
target_link_libraries(apptraverse_aether_channels_test PRIVATE aether unity apptraverse)
if(MSVC)
  target_link_options(apptraverse_aether_channels_test PRIVATE /STACK:16777216)
endif()
add_test(NAME apptraverse_aether_channels_test COMMAND apptraverse_aether_channels_test)
set_tests_properties(apptraverse_aether_channels_test PROPERTIES TIMEOUT 60)

# Explicit network control: normal CTest never registers cloud identities.
add_executable(apptraverse_aether_sdk_ab_smoke EXCLUDE_FROM_ALL
  "${_sdk_root}/examples/a_b_message_exchange/main.cpp"
  "${_sdk_root}/examples/a_b_message_exchange/a_b_message_exchange.cpp")
target_compile_definitions(apptraverse_aether_sdk_ab_smoke PRIVATE
  AE_AB_MESSAGE_EXCHANGE_STAGE_TIMEOUT_MS=15000)
target_link_libraries(apptraverse_aether_sdk_ab_smoke PRIVATE aether)

add_executable(apptraverse_aether_packet_queue_test
  "${PROJECT_SOURCE_DIR}/tests/aether_packet_queue_main.cpp"
  "${_sdk_root}/tests/test-transport/test-packet-queue-manager.cpp")
target_link_libraries(apptraverse_aether_packet_queue_test PRIVATE aether unity)
add_test(NAME apptraverse_aether_packet_queue_test COMMAND apptraverse_aether_packet_queue_test)
set_tests_properties(apptraverse_aether_packet_queue_test PROPERTIES
  LABELS "aether;headless;sdk" TIMEOUT 60)
