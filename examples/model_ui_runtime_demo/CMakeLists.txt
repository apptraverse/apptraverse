# Platform-neutral model/UI-runtime demo core. No Æther client, no network.

add_library(model_ui_runtime_demo_common STATIC
  common/demo_model.cpp
  common/demo_registration.cpp
  common/demo_bootstrap.cpp
  common/model_executor.cpp
  common/ui_publication.cpp
  common/ui_runtime_registry.cpp
)

target_include_directories(model_ui_runtime_demo_common PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/common
)

target_link_libraries(model_ui_runtime_demo_common PUBLIC apptraverse)
target_compile_features(model_ui_runtime_demo_common PUBLIC cxx_std_20)

if(WIN32)
  add_subdirectory(windows)
endif()
