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
set(WRITABLE_A "${STATE_ROOT}/replica_a")
set(WRITABLE_B "${STATE_ROOT}/replica_b")

execute_process(
  COMMAND "${DISTILLER}" "${BASE_ROOT}"
  RESULT_VARIABLE distiller_result
)
if(NOT distiller_result EQUAL 0)
  message(FATAL_ERROR
    "journal sync distiller failed with code ${distiller_result}")
endif()

execute_process(
  COMMAND "${RUNTIME}" "${WRITABLE_A}" "${WRITABLE_B}" "${BASE_ROOT}"
  RESULT_VARIABLE runtime_result
)
if(NOT runtime_result EQUAL 0)
  message(FATAL_ERROR
    "journal sync runtime failed with code ${runtime_result}")
endif()

file(REMOVE_RECURSE "${STATE_ROOT}")
