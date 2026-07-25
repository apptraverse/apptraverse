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

add_executable(apptraverse_prefab_subgraph_instantiation_test
  prefab_subgraph_instantiation_test.cpp
)

target_link_libraries(
  apptraverse_prefab_subgraph_instantiation_test PRIVATE apptraverse
)

add_test(
  NAME apptraverse_prefab_subgraph_instantiation_test
  COMMAND apptraverse_prefab_subgraph_instantiation_test
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

add_executable(apptraverse_event_sender_duplicate_delivery_test
  event_sender_duplicate_delivery_test.cpp
)

target_link_libraries(
  apptraverse_event_sender_duplicate_delivery_test PRIVATE apptraverse
)

add_test(
  NAME apptraverse_event_sender_duplicate_delivery_test
  COMMAND apptraverse_event_sender_duplicate_delivery_test
)
