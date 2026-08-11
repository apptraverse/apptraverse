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
