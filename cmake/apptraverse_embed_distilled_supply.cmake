# Build-time embed distilled RamDomainStorage layers into a generated header.

function(apptraverse_embed_distilled_supply target exporter_exe header_basename)
  if(NOT TARGET ${target})
    message(FATAL_ERROR "apptraverse_embed_distilled_supply: unknown target ${target}")
  endif()
  if(NOT TARGET ${exporter_exe})
    message(FATAL_ERROR "apptraverse_embed_distilled_supply: unknown exporter ${exporter_exe}")
  endif()
  if(NOT header_basename)
    message(FATAL_ERROR "apptraverse_embed_distilled_supply: header_basename required")
  endif()

  set(_extra_deps "")
  if(ARGC GREATER 3)
    foreach(_dep IN LISTS ARGN)
      list(APPEND _extra_deps "${_dep}")
    endforeach()
  endif()

  set(_gen_dir "${CMAKE_CURRENT_BINARY_DIR}/generated_distilled/${target}")
  file(MAKE_DIRECTORY "${_gen_dir}")
  set(_header "${_gen_dir}/${header_basename}")

  add_custom_command(
    OUTPUT "${_header}"
    COMMAND "$<TARGET_FILE:${exporter_exe}>" "${_header}"
    DEPENDS ${exporter_exe} ${_extra_deps}
    VERBATIM
    COMMENT "Embed distilled domain supply for ${target}")

  target_sources(${target} PRIVATE "${_header}")
  target_include_directories(${target} PRIVATE "${_gen_dir}")

  set(_embed_var "APPTRAVERSE_EMBEDDED_DISTILLED_HEADER_${target}")
  set("${_embed_var}" "${_header}" PARENT_SCOPE)
endfunction()
