if(NOT DEFINED STATE_PARENT OR NOT DEFINED WRITER_EXE OR NOT DEFINED READER_EXE)
  message(FATAL_ERROR "native_class_layers_evolution_run.cmake requires STATE_PARENT WRITER_EXE READER_EXE")
endif()
if(NOT EXISTS "${WRITER_EXE}" OR NOT EXISTS "${READER_EXE}")
  message(FATAL_ERROR "Build the native class-layer writer and reader before running this test")
endif()

string(RANDOM LENGTH 16 ALPHABET 0123456789abcdef run_id)
set(run_dir "${STATE_PARENT}/run-${run_id}")
if(EXISTS "${run_dir}")
  message(FATAL_ERROR "Refusing to reuse existing evolution test directory: ${run_dir}")
endif()
file(MAKE_DIRECTORY "${run_dir}")
set(snapshot "${run_dir}/state snapshot.bin")

execute_process(
  COMMAND "${WRITER_EXE}" "${snapshot}"
  RESULT_VARIABLE writer_result
  OUTPUT_VARIABLE writer_output
  ERROR_VARIABLE writer_error
  TIMEOUT 30
)
if(NOT writer_result STREQUAL "0" OR NOT EXISTS "${snapshot}")
  message(FATAL_ERROR "Evolution writer failed (${writer_result}) in ${run_dir}\n${writer_output}\n${writer_error}")
endif()
file(SHA256 "${snapshot}" before_sha256)

execute_process(
  COMMAND "${READER_EXE}" "${snapshot}"
  RESULT_VARIABLE reader_result
  OUTPUT_VARIABLE reader_output
  ERROR_VARIABLE reader_error
  TIMEOUT 30
)
if(NOT EXISTS "${snapshot}")
  message(FATAL_ERROR "Evolution reader removed the input snapshot: ${snapshot}")
endif()
file(SHA256 "${snapshot}" after_sha256)
if(NOT before_sha256 STREQUAL after_sha256)
  message(FATAL_ERROR "Evolution reader modified its input snapshot: ${snapshot}")
endif()
if(NOT reader_result STREQUAL "0")
  message(FATAL_ERROR "Evolution reader failed (${reader_result}) in ${run_dir}\n${reader_output}\n${reader_error}")
endif()

message(STATUS "Cross-process base-schema evolution PASS; input SHA256 unchanged; state: ${snapshot}")
