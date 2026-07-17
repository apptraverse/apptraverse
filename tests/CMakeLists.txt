add_executable(apptraverse_object_smoke_test
  object_smoke_test.cpp
)

target_link_libraries(apptraverse_object_smoke_test PRIVATE aether)

add_test(NAME apptraverse_object_smoke_test COMMAND apptraverse_object_smoke_test)

add_executable(apptraverse_typed_event_test
  typed_event_test.cpp
)

target_link_libraries(apptraverse_typed_event_test PRIVATE apptraverse)

add_test(NAME apptraverse_typed_event_test COMMAND apptraverse_typed_event_test)

add_executable(apptraverse_directory_domain_storage_test
  directory_domain_storage_test.cpp
)

target_link_libraries(apptraverse_directory_domain_storage_test PRIVATE apptraverse)

add_test(
  NAME apptraverse_directory_domain_storage_test
  COMMAND apptraverse_directory_domain_storage_test
)

add_executable(apptraverse_versioned_directory_storage_test
  versioned_directory_storage_test.cpp
)

target_link_libraries(
  apptraverse_versioned_directory_storage_test PRIVATE apptraverse
)

add_test(
  NAME apptraverse_versioned_directory_storage_test
  COMMAND apptraverse_versioned_directory_storage_test
)

add_executable(apptraverse_type_owned_factory_distiller
  type_owned_factory_distiller.cpp
)
target_link_libraries(apptraverse_type_owned_factory_distiller PRIVATE apptraverse)
target_include_directories(apptraverse_type_owned_factory_distiller PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
)
target_compile_definitions(apptraverse_type_owned_factory_distiller PRIVATE
  APPTRAVERSE_DISTILLATION_BUILD=1
)

add_executable(apptraverse_type_owned_factory_runtime
  type_owned_factory_runtime.cpp
)
target_link_libraries(apptraverse_type_owned_factory_runtime PRIVATE apptraverse)
target_include_directories(apptraverse_type_owned_factory_runtime PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
)

add_test(
  NAME apptraverse_type_owned_factory_test
  COMMAND ${CMAKE_COMMAND}
    -DDISTILLER=$<TARGET_FILE:apptraverse_type_owned_factory_distiller>
    -DRUNTIME=$<TARGET_FILE:apptraverse_type_owned_factory_runtime>
    -DSTATE_ROOT=${CMAKE_CURRENT_BINARY_DIR}/type_owned_factory_state
    -P ${CMAKE_CURRENT_SOURCE_DIR}/run_type_owned_factory_test.cmake
)

add_executable(apptraverse_node_journal_distiller
  node_journal_distiller.cpp
)
target_link_libraries(apptraverse_node_journal_distiller PRIVATE apptraverse)
target_include_directories(apptraverse_node_journal_distiller PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
)
target_compile_definitions(apptraverse_node_journal_distiller PRIVATE
  APPTRAVERSE_DISTILLATION_BUILD=1
)

add_executable(apptraverse_node_journal_runtime
  node_journal_runtime.cpp
)
target_link_libraries(apptraverse_node_journal_runtime PRIVATE apptraverse)
target_include_directories(apptraverse_node_journal_runtime PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
)

add_test(
  NAME apptraverse_node_journal_test
  COMMAND ${CMAKE_COMMAND}
    -DDISTILLER=$<TARGET_FILE:apptraverse_node_journal_distiller>
    -DRUNTIME=$<TARGET_FILE:apptraverse_node_journal_runtime>
    -DSTATE_ROOT=${CMAKE_CURRENT_BINARY_DIR}/node_journal_state
    -P ${CMAKE_CURRENT_SOURCE_DIR}/run_node_journal_test.cmake
)

add_executable(apptraverse_node_inheritance_distiller
  node_inheritance_distiller.cpp
)
target_link_libraries(apptraverse_node_inheritance_distiller PRIVATE apptraverse)
target_include_directories(apptraverse_node_inheritance_distiller PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
)
target_compile_definitions(apptraverse_node_inheritance_distiller PRIVATE
  APPTRAVERSE_DISTILLATION_BUILD=1
)

add_executable(apptraverse_node_inheritance_runtime
  node_inheritance_runtime.cpp
)
target_link_libraries(apptraverse_node_inheritance_runtime PRIVATE apptraverse)
target_include_directories(apptraverse_node_inheritance_runtime PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
)

add_test(
  NAME apptraverse_node_inheritance_test
  COMMAND ${CMAKE_COMMAND}
    -DDISTILLER=$<TARGET_FILE:apptraverse_node_inheritance_distiller>
    -DRUNTIME=$<TARGET_FILE:apptraverse_node_inheritance_runtime>
    -DSTATE_ROOT=${CMAKE_CURRENT_BINARY_DIR}/node_inheritance_state
    -P ${CMAKE_CURRENT_SOURCE_DIR}/run_node_inheritance_test.cmake
)

add_executable(apptraverse_node_state_version_distiller_v0
  node_state_version_distiller_v0.cpp
)
target_link_libraries(
  apptraverse_node_state_version_distiller_v0 PRIVATE apptraverse
)
target_include_directories(apptraverse_node_state_version_distiller_v0 PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
)
target_compile_definitions(apptraverse_node_state_version_distiller_v0 PRIVATE
  APPTRAVERSE_DISTILLATION_BUILD=1
  APPTRAVERSE_STATE_SCHEMA_V0=1
)

add_executable(apptraverse_node_state_version_runtime_v1
  node_state_version_runtime_v1.cpp
)
target_link_libraries(
  apptraverse_node_state_version_runtime_v1 PRIVATE apptraverse
)
target_include_directories(apptraverse_node_state_version_runtime_v1 PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
)

add_executable(apptraverse_node_state_version_rollback_v0
  node_state_version_rollback_v0.cpp
)
target_link_libraries(
  apptraverse_node_state_version_rollback_v0 PRIVATE apptraverse
)
target_include_directories(apptraverse_node_state_version_rollback_v0 PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
)
target_compile_definitions(apptraverse_node_state_version_rollback_v0 PRIVATE
  APPTRAVERSE_STATE_SCHEMA_V0=1
)

add_test(
  NAME apptraverse_node_state_version_test
  COMMAND ${CMAKE_COMMAND}
    -DDISTILLER=$<TARGET_FILE:apptraverse_node_state_version_distiller_v0>
    -DRUNTIME_V1=$<TARGET_FILE:apptraverse_node_state_version_runtime_v1>
    -DROLLBACK_V0=$<TARGET_FILE:apptraverse_node_state_version_rollback_v0>
    -DSTATE_ROOT=${CMAKE_CURRENT_BINARY_DIR}/node_state_version_state
    -P ${CMAKE_CURRENT_SOURCE_DIR}/run_node_state_version_test.cmake
)

add_executable(apptraverse_event_state_version_distiller_v0
  event_state_version_distiller_v0.cpp
)
target_link_libraries(
  apptraverse_event_state_version_distiller_v0 PRIVATE apptraverse
)
target_include_directories(apptraverse_event_state_version_distiller_v0 PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
)
target_compile_definitions(apptraverse_event_state_version_distiller_v0 PRIVATE
  APPTRAVERSE_DISTILLATION_BUILD=1
  APPTRAVERSE_EVENT_SCHEMA_V0=1
)

add_executable(apptraverse_event_state_version_runtime_v0
  event_state_version_runtime_v0.cpp
)
target_link_libraries(
  apptraverse_event_state_version_runtime_v0 PRIVATE apptraverse
)
target_include_directories(apptraverse_event_state_version_runtime_v0 PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
)
target_compile_definitions(apptraverse_event_state_version_runtime_v0 PRIVATE
  APPTRAVERSE_EVENT_SCHEMA_V0=1
)

add_executable(apptraverse_event_state_version_runtime_v1
  event_state_version_runtime_v1.cpp
)
target_link_libraries(
  apptraverse_event_state_version_runtime_v1 PRIVATE apptraverse
)
target_include_directories(apptraverse_event_state_version_runtime_v1 PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
)

add_executable(apptraverse_event_state_version_rollback_v0
  event_state_version_rollback_v0.cpp
)
target_link_libraries(
  apptraverse_event_state_version_rollback_v0 PRIVATE apptraverse
)
target_include_directories(apptraverse_event_state_version_rollback_v0 PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
)
target_compile_definitions(apptraverse_event_state_version_rollback_v0 PRIVATE
  APPTRAVERSE_EVENT_SCHEMA_V0=1
)

add_test(
  NAME apptraverse_event_state_version_test
  COMMAND ${CMAKE_COMMAND}
    -DDISTILLER=$<TARGET_FILE:apptraverse_event_state_version_distiller_v0>
    -DRUNTIME_V0=$<TARGET_FILE:apptraverse_event_state_version_runtime_v0>
    -DRUNTIME_V1=$<TARGET_FILE:apptraverse_event_state_version_runtime_v1>
    -DROLLBACK_V0=$<TARGET_FILE:apptraverse_event_state_version_rollback_v0>
    -DSTATE_ROOT=${CMAKE_CURRENT_BINARY_DIR}/event_state_version_state
    -P ${CMAKE_CURRENT_SOURCE_DIR}/run_event_state_version_test.cmake
)
