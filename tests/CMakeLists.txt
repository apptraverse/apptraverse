add_executable(apptraverse_event_sourced_core_test
  event_sourced_core_test.cpp
)

target_link_libraries(apptraverse_event_sourced_core_test PRIVATE apptraverse)

add_test(
  NAME apptraverse_event_sourced_core_test
  COMMAND apptraverse_event_sourced_core_test
)

add_executable(apptraverse_directory_domain_storage_test
  directory_domain_storage_test.cpp
)

target_link_libraries(apptraverse_directory_domain_storage_test PRIVATE apptraverse)

add_test(
  NAME apptraverse_directory_domain_storage_test
  COMMAND apptraverse_directory_domain_storage_test
)

add_executable(apptraverse_model_ui_runtime_test
  model_ui_runtime_test.cpp
)

target_link_libraries(apptraverse_model_ui_runtime_test PRIVATE
  model_ui_runtime_demo_common
)

target_compile_definitions(apptraverse_model_ui_runtime_test PRIVATE
  "MODEL_UI_RUNTIME_DEMO_SOURCE_DIR=\"${CMAKE_SOURCE_DIR}/examples/model_ui_runtime_demo\""
)

add_test(
  NAME apptraverse_model_ui_runtime_test
  COMMAND apptraverse_model_ui_runtime_test
)
