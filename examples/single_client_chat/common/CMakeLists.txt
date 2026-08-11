# Platform-neutral single-client-chat model and helpers.
# Not part of the public AppTraverse library API.

add_library(single_client_chat_common STATIC
  model/registration.cpp
  model/chat_peer_set.cpp
  chat_sync_controller.cpp
)

target_include_directories(single_client_chat_common PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/model
)

target_link_libraries(single_client_chat_common PUBLIC apptraverse)

target_compile_features(single_client_chat_common PUBLIC cxx_std_20)
