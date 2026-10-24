# Generate surfaces_embedded_release.h from release.json (build-time only).
# cmake -DCONFIG_JSON=... -DOUTPUT=... -P generate_surfaces_release_metadata.cmake

if(NOT CONFIG_JSON OR NOT OUTPUT)
  message(FATAL_ERROR "CONFIG_JSON and OUTPUT are required")
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

set(_header
  "#ifndef APPTRAVERSE_SURFACES_EMBEDDED_RELEASE_H_\n#define APPTRAVERSE_SURFACES_EMBEDDED_RELEASE_H_\n\nnamespace apptraverse::surfaces_demo {\n\n// Build-time Release R from release.json (not runtime state/active-release).\n// Semantic range: uint32_t (validated at generate time).\ninline constexpr unsigned kEmbeddedRelease = ${_release}u;\n\n}  // namespace apptraverse::surfaces_demo\n\n#endif  // APPTRAVERSE_SURFACES_EMBEDDED_RELEASE_H_\n"
)

file(WRITE "${OUTPUT}.tmp" "${_header}")
file(RENAME "${OUTPUT}.tmp" "${OUTPUT}")
