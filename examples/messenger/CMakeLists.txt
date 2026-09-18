add_library(messenger_model STATIC
  common/messenger_model.cpp
  common/messenger_aether_uid.cpp
)

target_include_directories(messenger_model PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/common
)

target_link_libraries(messenger_model PUBLIC apptraverse)
target_compile_features(messenger_model PUBLIC cxx_std_20)

if(TARGET aether)
  target_include_directories(messenger_model PUBLIC
    ${CMAKE_SOURCE_DIR}/examples/chat_demo/aether
    ${CMAKE_SOURCE_DIR}/examples/chat_demo/common
  )
  target_link_libraries(messenger_model PUBLIC aether)

  add_library(messenger_aether STATIC
    ${CMAKE_SOURCE_DIR}/examples/chat_demo/aether/aether_stream_frame.cpp
    ${CMAKE_SOURCE_DIR}/examples/chat_demo/aether/chat_aether_runtime.cpp
  )
  target_link_libraries(messenger_aether PUBLIC
    aether
    apptraverse
  )
  target_include_directories(messenger_aether PUBLIC
    ${CMAKE_SOURCE_DIR}/examples/chat_demo/aether
    ${CMAKE_SOURCE_DIR}/examples/chat_demo/common
  )
  target_compile_features(messenger_aether PUBLIC cxx_std_20)
endif()

add_library(messenger_distill STATIC
  common/messenger_distill.cpp
)
target_link_libraries(messenger_distill PUBLIC
  messenger_model
)
target_compile_features(messenger_distill PUBLIC cxx_std_20)

add_library(messenger_desktop STATIC
  desktop/desktop_surface_presenter.cpp
)
target_include_directories(messenger_desktop PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/desktop
  ${CMAKE_CURRENT_SOURCE_DIR}/common
)
target_link_libraries(messenger_desktop PUBLIC
  messenger_model
)
target_compile_features(messenger_desktop PUBLIC cxx_std_20)

add_executable(messenger_model_test
  common/messenger_model_test.cpp
)
target_link_libraries(messenger_model_test PRIVATE
  messenger_model
  messenger_distill
)
target_include_directories(messenger_model_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/common
)
target_compile_definitions(messenger_model_test PRIVATE
  APPTRAVERSE_ENABLE_DISTILLATION
)
target_compile_features(messenger_model_test PRIVATE cxx_std_20)
if(MSVC)
  target_compile_options(messenger_model_test PRIVATE /utf-8)
endif()

# UID validation unit checks (no network).
add_executable(messenger_aether_uid_test
  common/messenger_aether_uid_test.cpp
)
target_link_libraries(messenger_aether_uid_test PRIVATE messenger_model)
target_compile_features(messenger_aether_uid_test PRIVATE cxx_std_20)

if(WIN32 AND NOT EMSCRIPTEN)
  add_subdirectory(windows)
endif()
