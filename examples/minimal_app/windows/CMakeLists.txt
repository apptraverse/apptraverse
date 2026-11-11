set(_minimal_app_win_sources minimal_app_main.cpp minimal_app_model_session.cpp)
set(_minimal_embed_dir "${CMAKE_CURRENT_BINARY_DIR}/generated_distilled")
set(_minimal_embed_header "${_minimal_embed_dir}/embedded_distilled_minimal_app_supply.h")
file(MAKE_DIRECTORY "${_minimal_embed_dir}")
add_executable(win32_minimal_app ${_minimal_app_win_sources})
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  target_compile_definitions(win32_minimal_app PRIVATE APPTRAVERSE_MINIMAL_APP_DEBUG_BOOTSTRAP)
  target_sources(win32_minimal_app PRIVATE minimal_app_graph.cpp minimal_app_distillation.cpp)
  add_custom_command(OUTPUT "${_minimal_embed_header}"
    COMMAND $<TARGET_FILE:win32_minimal_app> --distill-only "${_minimal_embed_dir}"
    DEPENDS win32_minimal_app
    VERBATIM COMMENT "Prepare MinimalApp supply with the Debug application")
else()
  target_compile_definitions(win32_minimal_app PRIVATE APPTRAVERSE_MINIMAL_APP_USE_EMBEDDED_DISTILL)
  set(APPTRAVERSE_MINIMAL_APP_DEBUG_SUPPLY_HEADER
    "${PROJECT_SOURCE_DIR}/build/win64-ninja-msvc-debug/examples/minimal_app/windows/generated_distilled/embedded_distilled_minimal_app_supply.h"
    CACHE FILEPATH "MinimalApp supply prepared by Debug")
  add_custom_command(OUTPUT "${_minimal_embed_header}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${APPTRAVERSE_MINIMAL_APP_DEBUG_SUPPLY_HEADER}" "${_minimal_embed_header}"
    DEPENDS "${APPTRAVERSE_MINIMAL_APP_DEBUG_SUPPLY_HEADER}" VERBATIM)
endif()
add_custom_target(apptraverse_minimal_app_embedded_supply DEPENDS "${_minimal_embed_header}")
set(APPTRAVERSE_MINIMAL_APP_EMBED_INCLUDE_DIR "${_minimal_embed_dir}" CACHE INTERNAL "")
add_executable(win32_minimal_app_load_only ${_minimal_app_win_sources})
target_compile_definitions(win32_minimal_app_load_only PRIVATE
  APPTRAVERSE_MINIMAL_APP_LOAD_ONLY APPTRAVERSE_MINIMAL_APP_USE_EMBEDDED_DISTILL)

foreach(_minimal_demo win32_minimal_app win32_minimal_app_load_only)
  if(NOT (CMAKE_BUILD_TYPE STREQUAL "Debug" AND _minimal_demo STREQUAL "win32_minimal_app"))
    target_sources(${_minimal_demo} PRIVATE "${_minimal_embed_header}")
    target_include_directories(${_minimal_demo} PRIVATE "${_minimal_embed_dir}")
    add_dependencies(${_minimal_demo} apptraverse_minimal_app_embedded_supply)
  endif()
  target_link_libraries(${_minimal_demo} PRIVATE
    minimal_app_common
    apptraverse
    apptraverse_ui_windows
    user32
    gdi32
  )
  target_include_directories(${_minimal_demo} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../common
  )
  if(MSVC)
    target_compile_options(${_minimal_demo} PRIVATE /utf-8)
    set_target_properties(${_minimal_demo} PROPERTIES WIN32_EXECUTABLE TRUE)
    target_link_options(${_minimal_demo} PRIVATE /ENTRY:mainCRTStartup)
    target_link_libraries(${_minimal_demo} PRIVATE apptraverse_compile_policy)
    target_link_options(${_minimal_demo} PRIVATE
      "$<$<CONFIG:MinSizeRel>:/LTCG;/GUARD:NO>"
      "$<$<NOT:$<CONFIG:Debug>>:/MAP:$<TARGET_FILE_DIR:${_minimal_demo}>/$<TARGET_FILE_BASE_NAME:${_minimal_demo}>.map>"
    )
  endif()
  if(MINGW)
    set_target_properties(${_minimal_demo} PROPERTIES EXCLUDE_FROM_ALL TRUE)
  endif()
endforeach()
