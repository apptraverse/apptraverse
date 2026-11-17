if(APPTRAVERSE_BUILD_LEGACY_EXAMPLES)
  # Historical evolution/profile inspection is an explicit headless tool.
  add_executable(win32_window_catalog_history ../common/surface_history_main.cpp ../common/surface_graph_headless.cpp ../common/surfaces_lifecycle.cpp)
  target_link_libraries(win32_window_catalog_history PRIVATE window_catalog_evolution window_catalog_distill)
  add_executable(win32_window_catalog_v1_history ../common/surface_history_main.cpp ../common/surface_graph_headless.cpp ../common/surfaces_lifecycle.cpp)
  target_link_libraries(win32_window_catalog_v1_history PRIVATE window_catalog_evolution_v1 window_catalog_distill_v1)
  foreach(_history win32_window_catalog_history win32_window_catalog_v1_history)
    target_compile_definitions(${_history} PRIVATE APPTRAVERSE_SURFACE_HISTORY_HEADLESS APPTRAVERSE_ENABLE_DISTILLATION)
  endforeach()
  add_custom_command(TARGET win32_window_catalog_v1_history POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/surface_v1_package"
      "$<TARGET_FILE_DIR:win32_window_catalog_v1_history>/surface_v1_package" VERBATIM)
  set(_surface_ui_sources surface_ui_main.cpp ../common/surface_ui_session.cpp)
  foreach(_surface_target win32_window_catalog win32_window_catalog_v1 win32_window_catalog_load_only)
    add_executable(${_surface_target} ${_surface_ui_sources})
    target_link_libraries(${_surface_target} PRIVATE surface_generic_ui apptraverse_ui_windows user32 gdi32)
    target_compile_options(${_surface_target} PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
    if(MSVC)
      target_sources(${_surface_target} PRIVATE window_catalog.manifest)
      target_compile_definitions(${_surface_target} PRIVATE WINVER=0x0601 _WIN32_WINNT=0x0601)
      set_target_properties(${_surface_target} PROPERTIES WIN32_EXECUTABLE TRUE)
      target_link_options(${_surface_target} PRIVATE /ENTRY:mainCRTStartup /SUBSYSTEM:WINDOWS,6.01)
    endif()
  endforeach()
  target_compile_definitions(win32_window_catalog_v1 PRIVATE APPTRAVERSE_SURFACE_UI_V1)
  foreach(_producer win32_window_catalog win32_window_catalog_v1)
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
      target_compile_definitions(${_producer} PRIVATE APPTRAVERSE_SURFACE_UI_DEBUG_BOOTSTRAP)
      target_sources(${_producer} PRIVATE ../common/surface_ui_supply.cpp)
    else()
      set(APPTRAVERSE_SURFACE_UI_DEBUG_SUPPLY_DIR "" CACHE PATH "Surface S0 supplies prepared by Debug")
      if(NOT EXISTS "${APPTRAVERSE_SURFACE_UI_DEBUG_SUPPLY_DIR}/${_producer}/embedded_surface_ui_supply.h")
        message(FATAL_ERROR "Provide matching Debug Surface supplies through APPTRAVERSE_SURFACE_UI_DEBUG_SUPPLY_DIR")
      endif()
      target_include_directories(${_producer} PRIVATE "${APPTRAVERSE_SURFACE_UI_DEBUG_SUPPLY_DIR}/${_producer}")
    endif()
  endforeach()
  set(_surface_supply_dir "${CMAKE_CURRENT_BINARY_DIR}/generated_distilled/win32_window_catalog")
  set(_surface_supply "${_surface_supply_dir}/embedded_surface_ui_supply.h")
  file(MAKE_DIRECTORY "${_surface_supply_dir}")
  if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_custom_command(OUTPUT "${_surface_supply}"
      COMMAND $<TARGET_FILE:win32_window_catalog> --distill-only "${_surface_supply_dir}"
      DEPENDS win32_window_catalog VERBATIM)
  else()
    add_custom_command(OUTPUT "${_surface_supply}"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${APPTRAVERSE_SURFACE_UI_DEBUG_SUPPLY_DIR}/win32_window_catalog/embedded_surface_ui_supply.h" "${_surface_supply}"
      DEPENDS "${APPTRAVERSE_SURFACE_UI_DEBUG_SUPPLY_DIR}/win32_window_catalog/embedded_surface_ui_supply.h" VERBATIM)
  endif()
  target_sources(win32_window_catalog_load_only PRIVATE "${_surface_supply}")
  target_include_directories(win32_window_catalog_load_only PRIVATE "${_surface_supply_dir}")
endif()

# Debug prepares a fresh graph; consumers load the supply prepared by Debug.
set(_window_catalog_win_sources
  window_catalog_main.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../common/window_catalog_lifecycle.cpp
)

set(_window_catalog_embed_header
  "${CMAKE_CURRENT_BINARY_DIR}/../generated_distilled/embedded_distilled_window_catalog_supply.h")

add_executable(win32_window_catalog_demo ${_window_catalog_win_sources})
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  target_compile_definitions(win32_window_catalog_demo PRIVATE
    APPTRAVERSE_WINDOW_CATALOG_DEBUG_BOOTSTRAP)
  target_sources(win32_window_catalog_demo PRIVATE
    ../common/window_catalog_distillation.cpp)
else()
  target_compile_definitions(win32_window_catalog_demo PRIVATE
    APPTRAVERSE_WINDOW_CATALOG_USE_EMBEDDED_DISTILL)
endif()

add_executable(win32_window_catalog_demo_load_only ${_window_catalog_win_sources})
target_compile_definitions(win32_window_catalog_demo_load_only PRIVATE
  APPTRAVERSE_WINDOW_CATALOG_USE_EMBEDDED_DISTILL
  APPTRAVERSE_WINDOW_CATALOG_LOAD_ONLY
)

set(_window_catalog_embed_dir "${CMAKE_CURRENT_BINARY_DIR}/../generated_distilled")
file(MAKE_DIRECTORY "${_window_catalog_embed_dir}")
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  # The regular Debug application owns distillation, including this non-GUI mode.
  add_custom_command(OUTPUT "${_window_catalog_embed_header}"
    COMMAND $<TARGET_FILE:win32_window_catalog_demo>
            --distill-only "${_window_catalog_embed_dir}"
    DEPENDS win32_window_catalog_demo
    VERBATIM COMMENT "Prepare WindowCatalog supply with the Debug application")
else()
  set(APPTRAVERSE_WINDOW_CATALOG_DEBUG_SUPPLY_HEADER
    "${PROJECT_SOURCE_DIR}/build/win64-ninja-msvc-debug/examples/window_catalog/generated_distilled/embedded_distilled_window_catalog_supply.h"
    CACHE FILEPATH "WindowCatalog supply prepared by the Debug application")
  if(NOT EXISTS "${APPTRAVERSE_WINDOW_CATALOG_DEBUG_SUPPLY_HEADER}")
    message(FATAL_ERROR
      "Build Debug win32_window_catalog_demo_load_only first, or set APPTRAVERSE_WINDOW_CATALOG_DEBUG_SUPPLY_HEADER to a header prepared by Debug --distill-only.")
  endif()
  add_custom_command(OUTPUT "${_window_catalog_embed_header}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${APPTRAVERSE_WINDOW_CATALOG_DEBUG_SUPPLY_HEADER}"
            "${_window_catalog_embed_header}"
    DEPENDS "${APPTRAVERSE_WINDOW_CATALOG_DEBUG_SUPPLY_HEADER}"
    VERBATIM COMMENT "Embed the WindowCatalog supply prepared by Debug")
endif()
add_custom_target(apptraverse_window_catalog_embedded_supply
  DEPENDS "${_window_catalog_embed_header}")
set(APPTRAVERSE_WINDOW_CATALOG_EMBED_INCLUDE_DIR
    "${_window_catalog_embed_dir}" CACHE INTERNAL "")

foreach(_catalog_demo win32_window_catalog_demo win32_window_catalog_demo_load_only)
  if(MSVC)
    target_sources(${_catalog_demo} PRIVATE window_catalog.manifest)
    target_compile_definitions(${_catalog_demo} PRIVATE
      WINVER=0x0601 _WIN32_WINNT=0x0601)
    target_link_options(${_catalog_demo} PRIVATE /SUBSYSTEM:WINDOWS,6.01)
  endif()
  if(NOT (CMAKE_BUILD_TYPE STREQUAL "Debug" AND _catalog_demo STREQUAL "win32_window_catalog_demo"))
    target_sources(${_catalog_demo} PRIVATE "${_window_catalog_embed_header}")
    target_include_directories(${_catalog_demo} PRIVATE
      "${CMAKE_CURRENT_BINARY_DIR}/../generated_distilled")
    add_dependencies(${_catalog_demo} apptraverse_window_catalog_embedded_supply)
  endif()
  target_link_libraries(${_catalog_demo} PRIVATE
    window_catalog_window_catalog
    apptraverse_ui_windows
    user32
    gdi32
  )
  target_include_directories(${_catalog_demo} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../common
  )
  if(MSVC)
    target_compile_options(${_catalog_demo} PRIVATE /utf-8)
    set_target_properties(${_catalog_demo} PROPERTIES WIN32_EXECUTABLE TRUE)
    target_link_options(${_catalog_demo} PRIVATE /ENTRY:mainCRTStartup)
    target_link_libraries(${_catalog_demo} PRIVATE apptraverse_compile_policy)
    target_link_options(${_catalog_demo} PRIVATE
      "$<$<CONFIG:MinSizeRel>:/LTCG;/GUARD:NO>"
      "$<$<NOT:$<CONFIG:Debug>>:/MAP:$<TARGET_FILE_DIR:${_catalog_demo}>/$<TARGET_FILE_BASE_NAME:${_catalog_demo}>.map>"
    )
  endif()
  if(MINGW)
    set_target_properties(${_catalog_demo} PROPERTIES EXCLUDE_FROM_ALL TRUE)
  endif()
endforeach()
