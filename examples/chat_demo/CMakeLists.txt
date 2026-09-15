# Common chat model, commands, and launch option parser (no GUI, no transport)
add_library(chat_demo_model STATIC
  common/chat_model.cpp
  common/chat_commands.cpp
  common/chat_launch_options.cpp
)

target_link_libraries(chat_demo_model PUBLIC
  apptraverse
)

target_include_directories(chat_demo_model PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/common
)
