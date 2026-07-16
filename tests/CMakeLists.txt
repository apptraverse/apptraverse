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
