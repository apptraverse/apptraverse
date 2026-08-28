# Local-only chat Model/UI runtime example. No Æther peers, no network.

add_library(chat_ui_runtime_demo_common STATIC
  common/chat_model.cpp
  common/chat_registration.cpp
  common/chat_bootstrap.cpp
  common/chat_shared.cpp
  common/chat_presentation.cpp
)

target_include_directories(chat_ui_runtime_demo_common PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/common
)

target_link_libraries(chat_ui_runtime_demo_common PUBLIC apptraverse)
target_compile_features(chat_ui_runtime_demo_common PUBLIC cxx_std_20)

if(WIN32)
  add_subdirectory(windows)
endif()
