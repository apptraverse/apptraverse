add_executable(apptraverse_event_sourced_core_test
  event_sourced_core_test.cpp
)

target_link_libraries(apptraverse_event_sourced_core_test PRIVATE apptraverse)

add_test(
  NAME apptraverse_event_sourced_core_test
  COMMAND apptraverse_event_sourced_core_test
)

add_executable(apptraverse_model_presenter_graph_test
  model_presenter_graph_test.cpp
)

target_link_libraries(apptraverse_model_presenter_graph_test PRIVATE apptraverse)

add_test(
  NAME apptraverse_model_presenter_graph_test
  COMMAND apptraverse_model_presenter_graph_test
)

add_executable(apptraverse_single_client_chat_test
  single_client_chat_test.cpp
)

target_link_libraries(apptraverse_single_client_chat_test PRIVATE apptraverse)

add_test(
  NAME apptraverse_single_client_chat_test
  COMMAND apptraverse_single_client_chat_test
)
