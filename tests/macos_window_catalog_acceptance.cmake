# Each run owns a new profile. Existing application/user directories are untouched.
string(TIMESTAMP _stamp "%Y%m%d-%H%M%S")
string(RANDOM LENGTH 8 ALPHABET 0123456789abcdef _nonce)
set(_root "${ARTIFACT_ROOT}/${_stamp}-${_nonce}")
file(MAKE_DIRECTORY "${_root}")

function(run_acceptance executable profile evidence)
  execute_process(
    COMMAND "${executable}" --state-dir "${profile}" --artifacts "${evidence}" ${ARGN}
    RESULT_VARIABLE _status OUTPUT_VARIABLE _stdout ERROR_VARIABLE _stderr
    TIMEOUT 55)
  file(APPEND "${_root}/acceptance.log" "${_stdout}\n${_stderr}\n")
  if(NOT _status EQUAL 0)
    message(FATAL_ERROR "macOS acceptance failed (${_status})\n${_stdout}\n${_stderr}\nArtifacts: ${_root}")
  endif()
endfunction()

run_acceptance("${DEBUG_EXE}" "${_root}/debug-profile" "${_root}/debug")
run_acceptance("${LOAD_EXE}" "${_root}/embedded-profile" "${_root}/embedded")
run_acceptance("${LOAD_EXE}" "${_root}/embedded-profile" "${_root}/embedded" --cold)
message(STATUS "macOS Debug + embedded seed/cold acceptance passed. Artifacts: ${_root}")
