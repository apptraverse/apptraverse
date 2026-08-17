add_executable(apptraverse_event_sourced_core_test
  event_sourced_core_test.cpp
)

target_link_libraries(apptraverse_event_sourced_core_test PRIVATE apptraverse)

add_test(
  NAME apptraverse_event_sourced_core_test
  COMMAND apptraverse_event_sourced_core_test
)

add_executable(apptraverse_object_link_test
  object_link_test.cpp
)

target_link_libraries(apptraverse_object_link_test PRIVATE apptraverse)

add_test(
  NAME apptraverse_object_link_test
  COMMAND apptraverse_object_link_test
)

add_executable(apptraverse_shared_graph_discovery_test
  shared_graph_discovery_test.cpp
)

target_link_libraries(apptraverse_shared_graph_discovery_test PRIVATE
  apptraverse
  single_client_chat_common
)

add_test(
  NAME apptraverse_shared_graph_discovery_test
  COMMAND apptraverse_shared_graph_discovery_test
)

add_executable(apptraverse_chat_peer_set_test
  chat_peer_set_test.cpp
)

target_link_libraries(apptraverse_chat_peer_set_test PRIVATE
  apptraverse
  single_client_chat_common
)

add_test(
  NAME apptraverse_chat_peer_set_test
  COMMAND apptraverse_chat_peer_set_test
)

add_executable(apptraverse_chat_sync_controller_test
  chat_sync_controller_test.cpp
)

target_link_libraries(apptraverse_chat_sync_controller_test PRIVATE
  apptraverse
  single_client_chat_common
)

add_test(
  NAME apptraverse_chat_sync_controller_test
  COMMAND apptraverse_chat_sync_controller_test
)
add_executable(apptraverse_chat_component_test
  chat_component_test.cpp
)

target_link_libraries(apptraverse_chat_component_test PRIVATE
  apptraverse::chat_component
)

add_test(
  NAME apptraverse_chat_component_test
  COMMAND apptraverse_chat_component_test
)

add_executable(apptraverse_generic_storage_transfer_test
  generic_storage_transfer_test.cpp
)

target_link_libraries(apptraverse_generic_storage_transfer_test PRIVATE apptraverse)

add_test(
  NAME apptraverse_generic_storage_transfer_test
  COMMAND apptraverse_generic_storage_transfer_test
)

add_executable(apptraverse_persisted_session_state_test
  persisted_session_state_test.cpp
)

target_link_libraries(apptraverse_persisted_session_state_test PRIVATE apptraverse)

add_test(
  NAME apptraverse_persisted_session_state_test
  COMMAND apptraverse_persisted_session_state_test
)

add_executable(apptraverse_restart_safe_sync_test
  restart_safe_sync_test.cpp
)

target_link_libraries(apptraverse_restart_safe_sync_test PRIVATE
  apptraverse
  single_client_chat_common
)

add_test(
  NAME apptraverse_restart_safe_sync_test
  COMMAND apptraverse_restart_safe_sync_test
)

add_executable(apptraverse_remote_event_merge_test
  remote_event_merge_test.cpp
)

target_link_libraries(apptraverse_remote_event_merge_test PRIVATE apptraverse)

add_test(
  NAME apptraverse_remote_event_merge_test
  COMMAND apptraverse_remote_event_merge_test
)

add_executable(apptraverse_acknowledged_memory_session_test
  acknowledged_memory_session_test.cpp
)

target_link_libraries(apptraverse_acknowledged_memory_session_test PRIVATE
  apptraverse
  single_client_chat_common
)

add_test(
  NAME apptraverse_acknowledged_memory_session_test
  COMMAND apptraverse_acknowledged_memory_session_test
)

add_executable(apptraverse_blockable_remote_event_test
  blockable_remote_event_test.cpp
)

target_link_libraries(apptraverse_blockable_remote_event_test PRIVATE
  apptraverse
  single_client_chat_common
)

add_test(
  NAME apptraverse_blockable_remote_event_test
  COMMAND apptraverse_blockable_remote_event_test
)

add_executable(apptraverse_sync_packet_test
  sync_packet_test.cpp
)

target_link_libraries(apptraverse_sync_packet_test PRIVATE
  apptraverse
  single_client_chat_common
)

add_test(
  NAME apptraverse_sync_packet_test
  COMMAND apptraverse_sync_packet_test
)

add_executable(apptraverse_single_client_chat_unreliable_sync_test
  single_client_chat_unreliable_sync_test.cpp
)

target_link_libraries(apptraverse_single_client_chat_unreliable_sync_test PRIVATE
  apptraverse
  single_client_chat_common
)

add_test(
  NAME apptraverse_single_client_chat_unreliable_sync_test
  COMMAND apptraverse_single_client_chat_unreliable_sync_test
)

add_executable(apptraverse_model_presenter_graph_test
  model_presenter_graph_test.cpp
)

target_link_libraries(apptraverse_model_presenter_graph_test PRIVATE
  apptraverse
  single_client_chat_common
)

add_test(
  NAME apptraverse_model_presenter_graph_test
  COMMAND apptraverse_model_presenter_graph_test
)

add_executable(apptraverse_directory_domain_storage_test
  directory_domain_storage_test.cpp
)

target_link_libraries(apptraverse_directory_domain_storage_test PRIVATE apptraverse)

add_test(
  NAME apptraverse_directory_domain_storage_test
  COMMAND apptraverse_directory_domain_storage_test
)

add_executable(apptraverse_single_client_chat_test
  single_client_chat_test.cpp
)

target_link_libraries(apptraverse_single_client_chat_test PRIVATE
  apptraverse
  single_client_chat_common
)

add_test(
  NAME apptraverse_single_client_chat_test
  COMMAND apptraverse_single_client_chat_test
)

add_executable(apptraverse_windows_window_policy_test
  windows_window_policy_test.cpp
)

target_link_libraries(apptraverse_windows_window_policy_test PRIVATE
  apptraverse
  single_client_chat_common
)

target_include_directories(apptraverse_windows_window_policy_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../examples/single_client_chat/windows
)

add_test(
  NAME apptraverse_windows_window_policy_test
  COMMAND apptraverse_windows_window_policy_test
)

add_executable(apptraverse_aether_p2p_framing_test
  aether_p2p_framing_test.cpp
  ../examples/single_client_chat/common/aether_p2p_framing.cpp
)

target_include_directories(apptraverse_aether_p2p_framing_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../examples/single_client_chat/common
)

add_test(
  NAME apptraverse_aether_p2p_framing_test
  COMMAND apptraverse_aether_p2p_framing_test
)

add_executable(apptraverse_aether_p2p_write_gating_test
  aether_p2p_write_gating_test.cpp
)

target_include_directories(apptraverse_aether_p2p_write_gating_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../examples/single_client_chat/common
)

add_test(
  NAME apptraverse_aether_p2p_write_gating_test
  COMMAND apptraverse_aether_p2p_write_gating_test
)

