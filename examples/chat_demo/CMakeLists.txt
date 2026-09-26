# Common chat model, commands, and launch option parser (no GUI, no transport)

set(APPTRAVERSE_CHAT_BUILD_INFO_GENERATED
  "${CMAKE_BINARY_DIR}/generated/chat_build_info_generated.h")
if(NOT DEFINED APPTRAVERSE_CMAKE_DIR)
  get_filename_component(APPTRAVERSE_CMAKE_DIR
    "${CMAKE_CURRENT_LIST_DIR}/../../cmake" ABSOLUTE)
endif()
if(NOT DEFINED APPTRAVERSE_REPO_ROOT)
  get_filename_component(APPTRAVERSE_REPO_ROOT
    "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
endif()

add_custom_command(
  OUTPUT "${APPTRAVERSE_CHAT_BUILD_INFO_GENERATED}"
  COMMAND "${CMAKE_COMMAND}"
    "-DREPO_ROOT=${APPTRAVERSE_REPO_ROOT}"
    "-DOUTPUT=${APPTRAVERSE_CHAT_BUILD_INFO_GENERATED}"
    "-DCONFIGURATION=${CMAKE_BUILD_TYPE}"
    "-DCXX_COMPILER_ID=${CMAKE_CXX_COMPILER_ID}"
    -P "${APPTRAVERSE_CMAKE_DIR}/generate_chat_build_info.cmake"
  DEPENDS
    "${APPTRAVERSE_CMAKE_DIR}/generate_chat_build_info.cmake"
    "${APPTRAVERSE_CMAKE_DIR}/aether_version.cmake"
  COMMENT "Generating chat build identity header"
  VERBATIM
)

add_library(chat_demo_build_info STATIC
  common/chat_build_info.cpp
  "${APPTRAVERSE_CHAT_BUILD_INFO_GENERATED}"
)
target_include_directories(chat_demo_build_info PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/common
  ${CMAKE_BINARY_DIR}/generated
)
if(TARGET apptraverse_compile_policy)
  target_link_libraries(chat_demo_build_info PUBLIC apptraverse_compile_policy)
endif()
# Touch the generated header before compiling this TU when identity inputs change.
set_source_files_properties(common/chat_build_info.cpp PROPERTIES
  OBJECT_DEPENDS "${APPTRAVERSE_CHAT_BUILD_INFO_GENERATED}")

add_library(chat_demo_model STATIC
  common/chat_model.cpp
  common/chat_commands.cpp
  common/chat_launch_options.cpp
  common/profile_lock.cpp
  common/chat_bootstrap.cpp
  common/chat_demo_runtime_state.cpp
)

target_link_libraries(chat_demo_model PUBLIC
  apptraverse
  chat_demo_build_info
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

endif()
