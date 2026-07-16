if(NOT DISTILLER)
  message(FATAL_ERROR "DISTILLER is not set")
endif()
if(NOT RUNTIME)
  message(FATAL_ERROR "RUNTIME is not set")
endif()
if(NOT STATE_ROOT)
  message(FATAL_ERROR "STATE_ROOT is not set")
endif()

file(REMOVE_RECURSE "${STATE_ROOT}")
file(MAKE_DIRECTORY "${STATE_ROOT}")

set(BASE_ROOT "${STATE_ROOT}/base")
set(WRITABLE_ROOT "${STATE_ROOT}/00000002")

execute_process(
  COMMAND "${DISTILLER}" "${BASE_ROOT}"
  RESULT_VARIABLE distiller_result
)
if(NOT distiller_result EQUAL 0)
  message(FATAL_ERROR
    "type-owned factory distiller failed with code ${distiller_result}")
endif()

execute_process(
  COMMAND "${RUNTIME}" "${WRITABLE_ROOT}" "${BASE_ROOT}"
  RESULT_VARIABLE runtime_result
)
if(NOT runtime_result EQUAL 0)
  message(FATAL_ERROR
    "type-owned factory runtime failed with code ${runtime_result}")
endif()

file(REMOVE_RECURSE "${STATE_ROOT}")
