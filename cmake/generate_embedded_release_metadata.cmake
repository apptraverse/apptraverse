# Generate embedded release header from release.json (build-time only).
# cmake -DCONFIG_JSON=... -DOUTPUT=... -DNAMESPACE=apptraverse::foo -P generate_embedded_release_metadata.cmake

if(NOT CONFIG_JSON OR NOT OUTPUT OR NOT NAMESPACE)
  message(FATAL_ERROR "CONFIG_JSON, OUTPUT, and NAMESPACE are required")
endif()

if(NOT EXISTS "${CONFIG_JSON}")
  message(FATAL_ERROR "release config not found: ${CONFIG_JSON}")
endif()

file(READ "${CONFIG_JSON}" _json)

string(JSON _release ERROR_VARIABLE _json_err GET "${_json}" "release")
if(_json_err)
  message(FATAL_ERROR "release.json: missing or invalid 'release' field: ${_json_err}")
endif()

string(JSON _type ERROR_VARIABLE _type_err TYPE "${_json}" "release")
if(_type_err)
  message(FATAL_ERROR "release.json: cannot read type of 'release': ${_type_err}")
endif()

if(NOT _type STREQUAL "NUMBER")
  message(FATAL_ERROR "release.json: 'release' must be a JSON number, got type ${_type}")
endif()

if(_release MATCHES "\\.")
  message(FATAL_ERROR "release.json: 'release' must be an integer, not fractional")
endif()

if(_release MATCHES "-")
  message(FATAL_ERROR "release.json: 'release' must be non-negative")
endif()

if(NOT _release MATCHES "^[0-9]+$")
  message(FATAL_ERROR "release.json: 'release' must be a non-negative integer")
endif()

if(_release GREATER 4294967295)
  message(FATAL_ERROR "release.json: 'release' exceeds uint32_t range")
endif()

string(REPLACE "::" ";" _ns_parts "${NAMESPACE}")
list(LENGTH _ns_parts _ns_len)
math(EXPR _ns_last_idx "${_ns_len} - 1")
list(GET _ns_parts ${_ns_last_idx} _inner_ns)
string(REPLACE "::" "_" _guard_ns "${NAMESPACE}")
string(TOUPPER "${_guard_ns}" _guard_ns)

set(_header
  "#ifndef APPTRAVERSE_EMBEDDED_RELEASE_${_guard_ns}_H_\n#define APPTRAVERSE_EMBEDDED_RELEASE_${_guard_ns}_H_\n\nnamespace ${NAMESPACE} {\n\ninline constexpr unsigned kEmbeddedRelease = ${_release}u;\n\n}  // namespace ${_inner_ns}\n\n#endif\n"
)

file(WRITE "${OUTPUT}.tmp" "${_header}")
file(RENAME "${OUTPUT}.tmp" "${OUTPUT}")
