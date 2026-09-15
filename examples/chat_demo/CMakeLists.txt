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

# Native Aether runtime and IByteTransport adapter
if(TARGET aether)
  add_library(chat_demo_aether STATIC
    aether/aether_stream_frame.cpp
    aether/chat_aether_runtime.cpp
    aether/aether_byte_transport.cpp
    aether/aether_link.cpp
  )

  target_link_libraries(chat_demo_aether PUBLIC
    chat_demo_model
    aether
  )

  target_include_directories(chat_demo_aether PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/aether
    ${CMAKE_CURRENT_SOURCE_DIR}/common
  )

  add_library(chat_demo_runtime STATIC
    runtime/chat_session.cpp
  )

  target_link_libraries(chat_demo_runtime PUBLIC
    chat_demo_aether
  )

  target_include_directories(chat_demo_runtime PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/runtime
    ${CMAKE_CURRENT_SOURCE_DIR}/aether
    ${CMAKE_CURRENT_SOURCE_DIR}/common
  )

  add_executable(apptraverse_chat_aether_probe
    aether/chat_aether_probe.cpp
  )

  target_link_libraries(apptraverse_chat_aether_probe PRIVATE
    chat_demo_aether
  )

  if(WIN32)
    add_subdirectory(windows)
  endif()
endif()
