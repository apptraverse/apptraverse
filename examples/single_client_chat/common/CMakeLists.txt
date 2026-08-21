# Platform-neutral chat component (reusable) and demo registration glue.

add_library(apptraverse_chat_component STATIC
  model/chat_component_registration.cpp
  model/chat_peer_set.cpp
  chat_presence.cpp
  chat_sync_controller.cpp
  chat_component.cpp
  room_control.cpp
  room_membership_controller.cpp
)

target_include_directories(apptraverse_chat_component PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/model
)

target_link_libraries(apptraverse_chat_component PUBLIC apptraverse)

target_compile_features(apptraverse_chat_component PUBLIC cxx_std_20)

add_library(apptraverse::chat_component ALIAS apptraverse_chat_component)

add_library(single_client_chat_common STATIC
  model/registration.cpp
)

target_include_directories(single_client_chat_common PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/model
)

target_link_libraries(single_client_chat_common PUBLIC apptraverse::chat_component)

target_compile_features(single_client_chat_common PUBLIC cxx_std_20)