if(NOT DISTILLER)
  message(FATAL_ERROR "DISTILLER is not set")
endif()
if(NOT RUNTIME_V1)
  message(FATAL_ERROR "RUNTIME_V1 is not set")
endif()
if(NOT ROLLBACK_V0)
  message(FATAL_ERROR "ROLLBACK_V0 is not set")
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
    "node state version distiller v0 failed with code ${distiller_result}")
endif()

execute_process(
  COMMAND "${RUNTIME_V1}" "${WRITABLE_ROOT}" "${BASE_ROOT}"
  RESULT_VARIABLE runtime_v1_result
)
if(NOT runtime_v1_result EQUAL 0)
  message(FATAL_ERROR
    "node state version runtime v1 failed with code ${runtime_v1_result}")
endif()

execute_process(
  COMMAND "${ROLLBACK_V0}" "${WRITABLE_ROOT}" "${BASE_ROOT}"
  RESULT_VARIABLE rollback_v0_result
)
if(NOT rollback_v0_result EQUAL 0)
  message(FATAL_ERROR
    "node state version rollback v0 failed with code ${rollback_v0_result}")
endif()

file(REMOVE_RECURSE "${STATE_ROOT}")
