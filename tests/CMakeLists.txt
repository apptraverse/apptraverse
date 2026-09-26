add_executable(apptraverse_event_sourced_core_test
  event_sourced_core_test.cpp
)

target_link_libraries(apptraverse_event_sourced_core_test PRIVATE apptraverse)

add_test(
  NAME apptraverse_event_sourced_core_test
  COMMAND apptraverse_event_sourced_core_test
)

add_executable(apptraverse_shared_node_foundation_test
  shared_node_foundation_test.cpp
)

target_link_libraries(apptraverse_shared_node_foundation_test PRIVATE
  apptraverse
  shared_node_demo_model
)

target_include_directories(apptraverse_shared_node_foundation_test PRIVATE
  ${CMAKE_SOURCE_DIR}/examples/shared_node_demo
)

add_test(
  NAME apptraverse_shared_node_foundation_test
  COMMAND apptraverse_shared_node_foundation_test
)

add_executable(apptraverse_shared_node_initial_sync_test
  shared_node_initial_sync_test.cpp
)

target_link_libraries(apptraverse_shared_node_initial_sync_test PRIVATE
  apptraverse
  shared_node_demo_model
)

target_include_directories(apptraverse_shared_node_initial_sync_test PRIVATE
  ${CMAKE_SOURCE_DIR}/examples/shared_node_demo
)

add_test(
  NAME apptraverse_shared_node_initial_sync_test
  COMMAND apptraverse_shared_node_initial_sync_test
)

add_executable(apptraverse_shared_node_incremental_event_test
  shared_node_incremental_event_test.cpp
)

target_link_libraries(apptraverse_shared_node_incremental_event_test PRIVATE
  apptraverse
  shared_node_demo_model
)

target_include_directories(apptraverse_shared_node_incremental_event_test PRIVATE
  ${CMAKE_SOURCE_DIR}/examples/shared_node_demo
)

add_test(
  NAME apptraverse_shared_node_incremental_event_test
  COMMAND apptraverse_shared_node_incremental_event_test
)

add_executable(apptraverse_permanent_pair_sync_test
  permanent_pair_sync_test.cpp
)

target_link_libraries(apptraverse_permanent_pair_sync_test PRIVATE
  apptraverse
)

add_test(
  NAME apptraverse_permanent_pair_sync_test
  COMMAND apptraverse_permanent_pair_sync_test
)

add_executable(apptraverse_shared_child_node_stand_test
  shared_child_node_stand_test.cpp
)

target_link_libraries(apptraverse_shared_child_node_stand_test PRIVATE
  apptraverse
)

add_test(
  NAME apptraverse_shared_child_node_stand_test
  COMMAND apptraverse_shared_child_node_stand_test
)
add_executable(apptraverse_native_class_layers_test
  native_class_layers_test.cpp
)

target_link_libraries(apptraverse_native_class_layers_test PRIVATE
  apptraverse
  shared_node_demo_model
)

target_include_directories(apptraverse_native_class_layers_test PRIVATE
  ${CMAKE_SOURCE_DIR}/examples/shared_node_demo
)

add_test(
  NAME apptraverse_native_class_layers_test
  COMMAND apptraverse_native_class_layers_test
)

add_executable(native_class_layers_writer_v1
  native_class_layers_writer_v1.cpp
)

target_link_libraries(native_class_layers_writer_v1 PRIVATE
  apptraverse
)

add_executable(native_class_layers_reader_v2
  native_class_layers_reader_v2.cpp
)

target_link_libraries(native_class_layers_reader_v2 PRIVATE
  apptraverse
)

add_test(
  NAME apptraverse_native_class_layers_writer_v1
  COMMAND native_class_layers_writer_v1
)

add_test(
  NAME apptraverse_native_class_layers_reader_v2
  COMMAND native_class_layers_reader_v2
)

add_executable(apptraverse_native_object_graph_serialization_test
  native_object_graph_serialization_test.cpp
)

target_link_libraries(apptraverse_native_object_graph_serialization_test PRIVATE
  apptraverse
)

add_test(
  NAME apptraverse_native_object_graph_serialization_test
  COMMAND apptraverse_native_object_graph_serialization_test
)

add_executable(apptraverse_closed_event_graph_test
  closed_event_graph_test.cpp
)

target_link_libraries(apptraverse_closed_event_graph_test PRIVATE
  apptraverse
)

add_test(
  NAME apptraverse_closed_event_graph_test
  COMMAND apptraverse_closed_event_graph_test
)

add_executable(apptraverse_journal_retention_test
  journal_retention_test.cpp
)

target_link_libraries(apptraverse_journal_retention_test PRIVATE apptraverse)

add_test(
  NAME apptraverse_journal_retention_test
  COMMAND apptraverse_journal_retention_test
)

add_executable(apptraverse_model_runtime_stop_test
  model_runtime_stop_test.cpp
)

target_link_libraries(apptraverse_model_runtime_stop_test PRIVATE apptraverse)

add_test(
  NAME apptraverse_model_runtime_stop_test
  COMMAND apptraverse_model_runtime_stop_test
)

add_executable(apptraverse_directory_domain_storage_test
  directory_domain_storage_test.cpp
)

target_link_libraries(apptraverse_directory_domain_storage_test PRIVATE apptraverse)

add_test(
  NAME apptraverse_directory_domain_storage_test
  COMMAND apptraverse_directory_domain_storage_test
)

add_executable(apptraverse_overlay_domain_storage_test
  overlay_domain_storage_test.cpp
)

target_link_libraries(apptraverse_overlay_domain_storage_test PRIVATE apptraverse)

add_test(
  NAME apptraverse_overlay_domain_storage_test
  COMMAND apptraverse_overlay_domain_storage_test
)
add_executable(apptraverse_publication_channel_test
  publication_channel_test.cpp
)

target_include_directories(apptraverse_publication_channel_test PRIVATE
  ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(apptraverse_publication_channel_test PRIVATE
  apptraverse_compile_policy
)

add_test(
  NAME apptraverse_publication_channel_test
  COMMAND apptraverse_publication_channel_test
)

add_executable(apptraverse_chat_demo_model_test
  chat_demo_model_test.cpp
)
target_link_libraries(apptraverse_chat_demo_model_test PRIVATE
  apptraverse
  chat_demo_model
)
target_include_directories(apptraverse_chat_demo_model_test PRIVATE
  ${CMAKE_SOURCE_DIR}/examples/chat_demo/common
)
add_test(
  NAME apptraverse_chat_demo_model_test
  COMMAND apptraverse_chat_demo_model_test
)

add_executable(apptraverse_chat_demo_launch_options_test
  chat_demo_launch_options_test.cpp
)
target_link_libraries(apptraverse_chat_demo_launch_options_test PRIVATE
  apptraverse
  chat_demo_model
)
target_include_directories(apptraverse_chat_demo_launch_options_test PRIVATE
  ${CMAKE_SOURCE_DIR}/examples/chat_demo/common
)
add_test(
  NAME apptraverse_chat_demo_launch_options_test
  COMMAND apptraverse_chat_demo_launch_options_test
)

add_executable(apptraverse_chat_demo_sync_test
  chat_demo_sync_test.cpp
)
target_link_libraries(apptraverse_chat_demo_sync_test PRIVATE
  apptraverse
  chat_demo_model
)
target_include_directories(apptraverse_chat_demo_sync_test PRIVATE
  ${CMAKE_SOURCE_DIR}/examples/chat_demo/common
)
add_test(
  NAME apptraverse_chat_demo_sync_test
  COMMAND apptraverse_chat_demo_sync_test
)

if(TARGET chat_demo_aether)
  add_executable(apptraverse_chat_aether_write_completion_test
    chat_aether_write_completion_test.cpp
  )
  target_link_libraries(apptraverse_chat_aether_write_completion_test PRIVATE
    chat_demo_aether
  )
  target_include_directories(apptraverse_chat_aether_write_completion_test PRIVATE
    ${CMAKE_SOURCE_DIR}/examples/chat_demo/aether
  )
  add_test(
    NAME apptraverse_chat_aether_write_completion_test
    COMMAND apptraverse_chat_aether_write_completion_test
  )

  add_executable(apptraverse_aether_p2p_safe_stream_duplex_test
    aether_p2p_safe_stream_duplex_test.cpp
  )
  target_link_libraries(apptraverse_aether_p2p_safe_stream_duplex_test PRIVATE
    aether
  )
  target_include_directories(apptraverse_aether_p2p_safe_stream_duplex_test PRIVATE
    ${aether-client-cpp_SOURCE_DIR}
  )
  add_test(
    NAME apptraverse_aether_p2p_safe_stream_duplex_test
    COMMAND apptraverse_aether_p2p_safe_stream_duplex_test
  )

  add_executable(apptraverse_aether_stream_frame_test
    aether_stream_frame_test.cpp
  )
  target_link_libraries(apptraverse_aether_stream_frame_test PRIVATE
    chat_demo_aether
  )
  target_include_directories(apptraverse_aether_stream_frame_test PRIVATE
    ${CMAKE_SOURCE_DIR}/examples/chat_demo/aether
  )
  add_test(
    NAME apptraverse_aether_stream_frame_test
    COMMAND apptraverse_aether_stream_frame_test
  )

  add_executable(apptraverse_aether_byte_transport_dispatch_test
    aether_byte_transport_dispatch_test.cpp
  )
  target_link_libraries(apptraverse_aether_byte_transport_dispatch_test PRIVATE
    chat_demo_aether
  )
  target_include_directories(apptraverse_aether_byte_transport_dispatch_test PRIVATE
    ${CMAKE_SOURCE_DIR}/examples/chat_demo/aether
  )
  add_test(
    NAME apptraverse_aether_byte_transport_dispatch_test
    COMMAND apptraverse_aether_byte_transport_dispatch_test
  )

  add_executable(apptraverse_chat_session_startup_test
    chat_session_startup_test.cpp
  )
  target_link_libraries(apptraverse_chat_session_startup_test PRIVATE
    chat_demo_runtime
  )
  target_include_directories(apptraverse_chat_session_startup_test PRIVATE
    ${CMAKE_SOURCE_DIR}/examples/chat_demo/runtime
    ${CMAKE_SOURCE_DIR}/examples/chat_demo/aether
    ${CMAKE_SOURCE_DIR}/examples/chat_demo/common
    ${CMAKE_CURRENT_SOURCE_DIR}
  )
  add_test(
    NAME apptraverse_chat_session_startup_test
    COMMAND apptraverse_chat_session_startup_test
  )

  add_executable(apptraverse_chat_session_bootstrap_test
    chat_session_bootstrap_test.cpp
  )
  target_link_libraries(apptraverse_chat_session_bootstrap_test PRIVATE
    chat_demo_runtime
  )
  target_include_directories(apptraverse_chat_session_bootstrap_test PRIVATE
    ${CMAKE_SOURCE_DIR}/examples/chat_demo/runtime
    ${CMAKE_SOURCE_DIR}/examples/chat_demo/aether
    ${CMAKE_SOURCE_DIR}/examples/chat_demo/common
    ${CMAKE_CURRENT_SOURCE_DIR}
  )
  add_test(
    NAME apptraverse_chat_session_bootstrap_test
    COMMAND apptraverse_chat_session_bootstrap_test
  )

  add_executable(apptraverse_chat_session_publication_test
    chat_session_publication_test.cpp
  )
  target_link_libraries(apptraverse_chat_session_publication_test PRIVATE
    chat_demo_runtime
  )
  target_include_directories(apptraverse_chat_session_publication_test PRIVATE
    ${CMAKE_SOURCE_DIR}/examples/chat_demo/runtime
    ${CMAKE_SOURCE_DIR}/examples/chat_demo/aether
    ${CMAKE_SOURCE_DIR}/examples/chat_demo/common
    ${CMAKE_CURRENT_SOURCE_DIR}
  )
  add_test(
    NAME apptraverse_chat_session_publication_test
    COMMAND apptraverse_chat_session_publication_test
  )

  add_executable(apptraverse_chat_session_status_test
    chat_session_status_test.cpp
  )
  target_link_libraries(apptraverse_chat_session_status_test PRIVATE
    chat_demo_runtime
  )
  target_include_directories(apptraverse_chat_session_status_test PRIVATE
    ${CMAKE_SOURCE_DIR}/examples/chat_demo/runtime
    ${CMAKE_SOURCE_DIR}/examples/chat_demo/aether
    ${CMAKE_SOURCE_DIR}/examples/chat_demo/common
    ${CMAKE_SOURCE_DIR}/tests
  )
  add_test(
    NAME apptraverse_chat_session_status_test
    COMMAND apptraverse_chat_session_status_test
  )

  add_executable(apptraverse_chat_session_lifecycle_test
    chat_session_lifecycle_test.cpp
  )
  target_link_libraries(apptraverse_chat_session_lifecycle_test PRIVATE
    chat_demo_runtime
  )
  target_include_directories(apptraverse_chat_session_lifecycle_test PRIVATE
    ${CMAKE_SOURCE_DIR}/examples/chat_demo/runtime
    ${CMAKE_SOURCE_DIR}/examples/chat_demo/aether
    ${CMAKE_SOURCE_DIR}/examples/chat_demo/common
    ${CMAKE_CURRENT_SOURCE_DIR}
  )
  add_test(
    NAME apptraverse_chat_session_lifecycle_test
    COMMAND apptraverse_chat_session_lifecycle_test
  )

  add_executable(apptraverse_chat_session_command_limits_test
    chat_session_command_limits_test.cpp
  )
  target_link_libraries(apptraverse_chat_session_command_limits_test PRIVATE
    chat_demo_runtime
  )
  target_include_directories(apptraverse_chat_session_command_limits_test PRIVATE
    ${CMAKE_SOURCE_DIR}/examples/chat_demo/runtime
    ${CMAKE_SOURCE_DIR}/examples/chat_demo/aether
    ${CMAKE_SOURCE_DIR}/examples/chat_demo/common
    ${CMAKE_CURRENT_SOURCE_DIR}
  )
  add_test(
    NAME apptraverse_chat_session_command_limits_test
    COMMAND apptraverse_chat_session_command_limits_test
  )

  add_executable(apptraverse_chat_session_fault_test
    chat_session_fault_test.cpp
  )
  target_link_libraries(apptraverse_chat_session_fault_test PRIVATE
    chat_demo_runtime
  )
  target_include_directories(apptraverse_chat_session_fault_test PRIVATE
    ${CMAKE_SOURCE_DIR}/examples/chat_demo/runtime
    ${CMAKE_SOURCE_DIR}/examples/chat_demo/aether
    ${CMAKE_SOURCE_DIR}/examples/chat_demo/common
    ${CMAKE_CURRENT_SOURCE_DIR}
  )
  add_test(
    NAME apptraverse_chat_session_fault_test
    COMMAND apptraverse_chat_session_fault_test
  )

  add_executable(apptraverse_shared_sync_protocol_test
    shared_sync_protocol_test.cpp
  )
  target_link_libraries(apptraverse_shared_sync_protocol_test PRIVATE
    apptraverse
    chat_demo_model
    shared_node_demo_model
  )
  target_include_directories(apptraverse_shared_sync_protocol_test PRIVATE
    ${CMAKE_SOURCE_DIR}/examples/chat_demo/common
    ${CMAKE_SOURCE_DIR}/examples/shared_node_demo
  )
  add_test(
    NAME apptraverse_shared_sync_protocol_test
    COMMAND apptraverse_shared_sync_protocol_test
  )

  add_executable(apptraverse_chat_session_integration_test
    chat_session_integration_test.cpp
  )
  target_link_libraries(apptraverse_chat_session_integration_test PRIVATE
    chat_demo_runtime
    shared_node_demo_model
  )
  target_include_directories(apptraverse_chat_session_integration_test PRIVATE
    ${CMAKE_SOURCE_DIR}/examples/chat_demo/runtime
    ${CMAKE_SOURCE_DIR}/examples/chat_demo/aether
    ${CMAKE_SOURCE_DIR}/examples/chat_demo/common
    ${CMAKE_SOURCE_DIR}/examples/shared_node_demo
    ${CMAKE_CURRENT_SOURCE_DIR}
  )
  if(MSVC)
    target_compile_options(apptraverse_chat_session_integration_test PRIVATE /utf-8)
  endif()
  add_test(
    NAME apptraverse_chat_session_integration_test
    COMMAND apptraverse_chat_session_integration_test
  )

  add_executable(apptraverse_chat_session_delivery_test
    chat_session_delivery_test.cpp
  )
  target_link_libraries(apptraverse_chat_session_delivery_test PRIVATE
    chat_demo_runtime
  )
  target_include_directories(apptraverse_chat_session_delivery_test PRIVATE
    ${CMAKE_SOURCE_DIR}/examples/chat_demo/runtime
    ${CMAKE_SOURCE_DIR}/examples/chat_demo/aether
    ${CMAKE_SOURCE_DIR}/examples/chat_demo/common
    ${CMAKE_CURRENT_SOURCE_DIR}
  )
  add_test(
    NAME apptraverse_chat_session_delivery_test
    COMMAND apptraverse_chat_session_delivery_test
  )
  if(UNIX AND NOT EMSCRIPTEN)
    add_executable(apptraverse_chat_aether_p2p_test
      chat_aether_p2p_test.cpp
    )
    target_link_libraries(apptraverse_chat_aether_p2p_test PRIVATE
      apptraverse
      chat_demo_model
      chat_demo_aether
      aether
    )
    target_include_directories(apptraverse_chat_aether_p2p_test PRIVATE
      ${CMAKE_SOURCE_DIR}/examples/chat_demo/common
      ${CMAKE_SOURCE_DIR}/examples/chat_demo/aether
    )
    add_dependencies(apptraverse_chat_aether_p2p_test
      apptraverse_chat_aether_probe
    )
    add_test(
      NAME apptraverse_chat_aether_p2p_test
      COMMAND apptraverse_chat_aether_p2p_test
    )
  endif()
endif()

add_executable(apptraverse_presenter_load_order_test
  presenter_load_order_test.cpp
)

target_link_libraries(apptraverse_presenter_load_order_test PRIVATE apptraverse)

add_test(
  NAME apptraverse_presenter_load_order_test
  COMMAND apptraverse_presenter_load_order_test
)

add_custom_target(apptraverse_shared_node_headless_check
  COMMAND $<TARGET_FILE:apptraverse_event_sourced_core_test>
  COMMAND $<TARGET_FILE:apptraverse_journal_retention_test>
  COMMAND $<TARGET_FILE:apptraverse_shared_node_foundation_test>
  COMMAND $<TARGET_FILE:apptraverse_shared_node_initial_sync_test>
  COMMAND $<TARGET_FILE:apptraverse_shared_node_incremental_event_test>
  COMMAND $<TARGET_FILE:apptraverse_permanent_pair_sync_test>
  COMMAND $<TARGET_FILE:apptraverse_shared_child_node_stand_test>
  COMMAND $<TARGET_FILE:apptraverse_closed_event_graph_test>
  DEPENDS
    apptraverse_event_sourced_core_test
    apptraverse_journal_retention_test
    apptraverse_shared_node_foundation_test
    apptraverse_shared_node_initial_sync_test
    apptraverse_shared_node_incremental_event_test
    apptraverse_permanent_pair_sync_test
    apptraverse_shared_child_node_stand_test
    apptraverse_closed_event_graph_test
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
  COMMENT "Headless SharedNode sync and transport checks"
)
