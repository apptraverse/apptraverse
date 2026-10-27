if(NOT DEFINED STATE_DIR OR NOT DEFINED SAVE_EXE OR NOT DEFINED LOAD_EXE)
  message(FATAL_ERROR "presenter_platform_persist_run.cmake requires STATE_DIR SAVE_EXE LOAD_EXE")
endif()

file(REMOVE_RECURSE "${STATE_DIR}")
file(MAKE_DIRECTORY "${STATE_DIR}")

execute_process(COMMAND "${SAVE_EXE}" "${STATE_DIR}" RESULT_VARIABLE save_result)
if(NOT save_result EQUAL 0)
  message(FATAL_ERROR "save backend failed: ${save_result}")
endif()

execute_process(COMMAND "${LOAD_EXE}" "${STATE_DIR}" RESULT_VARIABLE load_result)
if(NOT load_result EQUAL 0)
  message(FATAL_ERROR "load backend failed: ${load_result}")
endif()
