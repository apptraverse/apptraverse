if(NOT DEFINED EXE OR NOT DEFINED STATE_ROOT)
  message(FATAL_ERROR
    "object_origin_disk_roundtrip_run.cmake requires EXE and STATE_ROOT")
endif()

file(REMOVE_RECURSE "${STATE_ROOT}/backing" "${STATE_ROOT}/user")
file(MAKE_DIRECTORY "${STATE_ROOT}/backing")
file(MAKE_DIRECTORY "${STATE_ROOT}/user")

execute_process(COMMAND "${EXE}" --write "${STATE_ROOT}"
                RESULT_VARIABLE write_result)
if(NOT write_result EQUAL 0)
  message(FATAL_ERROR "object origin disk --write failed: ${write_result}")
endif()

execute_process(COMMAND "${EXE}" --read "${STATE_ROOT}"
                RESULT_VARIABLE read_result)
if(NOT read_result EQUAL 0)
  message(FATAL_ERROR "object origin disk --read failed: ${read_result}")
endif()
