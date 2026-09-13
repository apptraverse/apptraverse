# Headless SharedNode foundation fixtures only — no GUI, no transport.
add_library(shared_node_demo_model STATIC
  shared_node_demo_model.cpp
)
target_link_libraries(shared_node_demo_model PUBLIC apptraverse)
target_include_directories(shared_node_demo_model PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
)
