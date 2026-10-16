if(NOT DEFINED SNAPSHOT OR NOT DEFINED SAVE_EXE OR NOT DEFINED LOAD_EXE)
  message(FATAL_ERROR "presenter_platform_persist_run.cmake requires SNAPSHOT SAVE_EXE LOAD_EXE")
endif()

execute_process(COMMAND "${SAVE_EXE}" "${SNAPSHOT}" RESULT_VARIABLE save_result)
if(NOT save_result EQUAL 0)
  message(FATAL_ERROR "save backend failed: ${save_result}")
endif()

execute_process(COMMAND "${LOAD_EXE}" "${SNAPSHOT}" RESULT_VARIABLE load_result)
if(NOT load_result EQUAL 0)
  message(FATAL_ERROR "load backend failed: ${load_result}")
endif()
