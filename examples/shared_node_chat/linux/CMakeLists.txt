add_executable(shared_node_chat main.cpp)
target_link_libraries(shared_node_chat PRIVATE apptraverse_shared_chat_ui apptraverse_ui_linux)

# Explicit real-cloud native acceptance; ordinary CTest never creates identities.
add_executable(apptraverse_shared_chat_linux_native_test EXCLUDE_FROM_ALL ../../../tests/shared_chat_linux_native_test.cpp)
target_link_libraries(apptraverse_shared_chat_linux_native_test PRIVATE apptraverse_shared_chat_ui apptraverse_ui_linux)
