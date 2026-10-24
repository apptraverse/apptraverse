# Embed Surfaces Demo Release R from release.json into a target (header in build tree).

function(apptraverse_embed_surfaces_release_metadata target config_json)
  if(NOT TARGET ${target})
    message(FATAL_ERROR "apptraverse_embed_surfaces_release_metadata: unknown target ${target}")
  endif()

  get_filename_component(_config_abs "${config_json}" ABSOLUTE BASE_DIR "${CMAKE_SOURCE_DIR}")
  if(NOT EXISTS "${_config_abs}")
    message(FATAL_ERROR "release config not found: ${_config_abs}")
  endif()

  set(_gen_dir "${CMAKE_CURRENT_BINARY_DIR}/generated_release/${target}")
  file(MAKE_DIRECTORY "${_gen_dir}")
  set(_header "${_gen_dir}/surfaces_embedded_release.h")

  add_custom_command(
    OUTPUT "${_header}"
    COMMAND
      ${CMAKE_COMMAND}
      "-DCONFIG_JSON=${_config_abs}"
      "-DOUTPUT=${_header}"
      -P "${CMAKE_SOURCE_DIR}/cmake/generate_surfaces_release_metadata.cmake"
    DEPENDS
      "${_config_abs}"
      "${CMAKE_SOURCE_DIR}/cmake/generate_surfaces_release_metadata.cmake"
    VERBATIM
    COMMENT "Generate embedded release metadata for ${target}")

  target_sources(${target} PRIVATE "${_header}")
  target_include_directories(${target} PRIVATE "${_gen_dir}")
  if(TARGET apptraverse_compile_policy)
    target_link_libraries(${target} PRIVATE apptraverse_compile_policy)
  endif()
  set_target_properties(${target} PROPERTIES APPTRAVERSE_RELEASE_CONFIG "${_config_abs}")
endfunction()
