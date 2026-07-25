add_executable(apptraverse_leaf_object_serialization_test
  leaf_object_serialization_test.cpp
)

target_link_libraries(apptraverse_leaf_object_serialization_test PRIVATE apptraverse)

add_test(
  NAME apptraverse_leaf_object_serialization_test
  COMMAND apptraverse_leaf_object_serialization_test
)

add_executable(apptraverse_leaf_node_base_serialization_test
  leaf_node_base_serialization_test.cpp
)

target_link_libraries(
  apptraverse_leaf_node_base_serialization_test PRIVATE apptraverse
)

add_test(
  NAME apptraverse_leaf_node_base_serialization_test
  COMMAND apptraverse_leaf_node_base_serialization_test
)

add_executable(apptraverse_leaf_node_journal_replay_test
  leaf_node_journal_replay_test.cpp
)

target_link_libraries(
  apptraverse_leaf_node_journal_replay_test PRIVATE apptraverse
)

add_test(
  NAME apptraverse_leaf_node_journal_replay_test
  COMMAND apptraverse_leaf_node_journal_replay_test
)

add_executable(apptraverse_leaf_node_reordered_journal_test
  leaf_node_reordered_journal_test.cpp
)

target_link_libraries(
  apptraverse_leaf_node_reordered_journal_test PRIVATE apptraverse
)

add_test(
  NAME apptraverse_leaf_node_reordered_journal_test
  COMMAND apptraverse_leaf_node_reordered_journal_test
)

add_executable(apptraverse_leaf_node_fixed_base_capture_test
  leaf_node_fixed_base_capture_test.cpp
)

target_link_libraries(
  apptraverse_leaf_node_fixed_base_capture_test PRIVATE apptraverse
)

add_test(
  NAME apptraverse_leaf_node_fixed_base_capture_test
  COMMAND apptraverse_leaf_node_fixed_base_capture_test
)

add_executable(apptraverse_leaf_node_local_commit_test
  leaf_node_local_commit_test.cpp
)

target_link_libraries(
  apptraverse_leaf_node_local_commit_test PRIVATE apptraverse
)

add_test(
  NAME apptraverse_leaf_node_local_commit_test
  COMMAND apptraverse_leaf_node_local_commit_test
)

add_executable(apptraverse_leaf_node_remote_event_test
  leaf_node_remote_event_test.cpp
)

target_link_libraries(
  apptraverse_leaf_node_remote_event_test PRIVATE apptraverse
)

add_test(
  NAME apptraverse_leaf_node_remote_event_test
  COMMAND apptraverse_leaf_node_remote_event_test
)

add_executable(apptraverse_event_reference_graph_serialization_test
  event_reference_graph_serialization_test.cpp
)

target_link_libraries(
  apptraverse_event_reference_graph_serialization_test PRIVATE apptraverse
)

add_test(
  NAME apptraverse_event_reference_graph_serialization_test
  COMMAND apptraverse_event_reference_graph_serialization_test
)

add_executable(apptraverse_graph_journal_scanner_test
  graph_journal_scanner_test.cpp
)

target_link_libraries(
  apptraverse_graph_journal_scanner_test PRIVATE apptraverse
)

add_test(
  NAME apptraverse_graph_journal_scanner_test
  COMMAND apptraverse_graph_journal_scanner_test
)

add_executable(apptraverse_node_for_type_erased_accept_test
  node_for_type_erased_accept_test.cpp
)

target_link_libraries(
  apptraverse_node_for_type_erased_accept_test PRIVATE apptraverse
)

add_test(
  NAME apptraverse_node_for_type_erased_accept_test
  COMMAND apptraverse_node_for_type_erased_accept_test
)

add_executable(apptraverse_journal_event_message_transfer_test
  journal_event_message_transfer_test.cpp
)

target_link_libraries(
  apptraverse_journal_event_message_transfer_test PRIVATE apptraverse
)

add_test(
  NAME apptraverse_journal_event_message_transfer_test
  COMMAND apptraverse_journal_event_message_transfer_test
)

add_executable(apptraverse_graph_synchronizer_test
  graph_synchronizer_test.cpp
)

target_link_libraries(
  apptraverse_graph_synchronizer_test PRIVATE apptraverse
)

add_test(
  NAME apptraverse_graph_synchronizer_test
  COMMAND apptraverse_graph_synchronizer_test
)

add_executable(apptraverse_multi_node_graph_synchronization_test
  multi_node_graph_synchronization_test.cpp
)

target_link_libraries(
  apptraverse_multi_node_graph_synchronization_test PRIVATE apptraverse
)

add_test(
  NAME apptraverse_multi_node_graph_synchronization_test
  COMMAND apptraverse_multi_node_graph_synchronization_test
)

add_executable(apptraverse_cyclic_node_base_capture_test
  cyclic_node_base_capture_test.cpp
)

target_link_libraries(
  apptraverse_cyclic_node_base_capture_test PRIVATE apptraverse
)

add_test(
  NAME apptraverse_cyclic_node_base_capture_test
  COMMAND apptraverse_cyclic_node_base_capture_test
)

add_executable(apptraverse_client_prefab_instantiation_test
  client_prefab_instantiation_test.cpp
)

target_link_libraries(
  apptraverse_client_prefab_instantiation_test PRIVATE apptraverse
)

add_test(
  NAME apptraverse_client_prefab_instantiation_test
  COMMAND apptraverse_client_prefab_instantiation_test
)

add_executable(apptraverse_minimal_chat_graph_test
  minimal_chat_graph_test.cpp
)

target_link_libraries(
  apptraverse_minimal_chat_graph_test PRIVATE apptraverse
)

add_test(
  NAME apptraverse_minimal_chat_graph_test
  COMMAND apptraverse_minimal_chat_graph_test
)

add_executable(apptraverse_chat_client_introduction_synchronization_test
  chat_client_introduction_synchronization_test.cpp
)

target_link_libraries(
  apptraverse_chat_client_introduction_synchronization_test PRIVATE apptraverse
)

add_test(
  NAME apptraverse_chat_client_introduction_synchronization_test
  COMMAND apptraverse_chat_client_introduction_synchronization_test
)

add_executable(apptraverse_chat_message_reference_synchronization_test
  chat_message_reference_synchronization_test.cpp
)

target_link_libraries(
  apptraverse_chat_message_reference_synchronization_test PRIVATE apptraverse
)

add_test(
  NAME apptraverse_chat_message_reference_synchronization_test
  COMMAND apptraverse_chat_message_reference_synchronization_test
)

add_executable(apptraverse_multi_recipient_delivery_test
  multi_recipient_delivery_test.cpp
)

target_link_libraries(
  apptraverse_multi_recipient_delivery_test PRIVATE apptraverse
)

add_test(
  NAME apptraverse_multi_recipient_delivery_test
  COMMAND apptraverse_multi_recipient_delivery_test
)
