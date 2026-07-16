add_executable(object_smoke_test
  object_smoke_test.cpp
)

target_link_libraries(object_smoke_test PRIVATE aether)

add_test(NAME object_smoke_test COMMAND object_smoke_test)
