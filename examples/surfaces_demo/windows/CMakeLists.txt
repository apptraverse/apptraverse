set(_surfaces_win_sources
  main.cpp
  win_app.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../common/surfaces_lifecycle.cpp
)

function(apptraverse_add_surfaces_demo_executable target distill_lib evolution_lib windows_lib)
  add_executable(${target} ${_surfaces_win_sources})
  target_compile_definitions(${target} PRIVATE APPTRAVERSE_ENABLE_DISTILLATION)
  target_link_libraries(${target} PRIVATE
    ${distill_lib}
    ${evolution_lib}
    ${windows_lib}
    user32
    gdi32
  )
  target_include_directories(${target} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../common
    ${CMAKE_SOURCE_DIR}/examples/main_window_runtime_demo/windows
  )
  if(MSVC)
    target_compile_options(${target} PRIVATE /utf-8)
    set_target_properties(${target} PROPERTIES WIN32_EXECUTABLE TRUE)
    target_link_options(${target} PRIVATE /ENTRY:mainCRTStartup)
  endif()
  if(MINGW)
    set_target_properties(${target} PROPERTIES EXCLUDE_FROM_ALL TRUE)
  endif()
endfunction()

apptraverse_add_surfaces_demo_executable(win32_surfaces_demo
  surfaces_demo_distill surfaces_demo_evolution surfaces_demo_win32)
apptraverse_add_surfaces_demo_executable(win32_surfaces_demo_v1
  surfaces_demo_distill_v1 surfaces_demo_evolution_v1 surfaces_demo_win32_v1)

add_executable(win32_surfaces_demo_load_only ${_surfaces_win_sources})
target_link_libraries(win32_surfaces_demo_load_only PRIVATE
  surfaces_demo_win32
  surfaces_demo_evolution
  user32
  gdi32
)
target_include_directories(win32_surfaces_demo_load_only PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/../common
  ${CMAKE_SOURCE_DIR}/examples/main_window_runtime_demo/windows
)
if(MSVC)
  target_compile_options(win32_surfaces_demo_load_only PRIVATE /utf-8)
  set_target_properties(win32_surfaces_demo_load_only PROPERTIES WIN32_EXECUTABLE TRUE)
  target_link_options(win32_surfaces_demo_load_only PRIVATE /ENTRY:mainCRTStartup)
endif()

if(WIN32 AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/surface_v1_package")
  foreach(_demo_target win32_surfaces_demo_v1)
    add_custom_command(TARGET ${_demo_target} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/surface_v1_package"
        "$<TARGET_FILE_DIR:${_demo_target}>/surface_v1_package"
      COMMENT "Copy bundled Surface v1 package next to ${_demo_target}"
    )
  endforeach()
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
    "${PROJECT_SOURCE_DIR}/build/win64-ninja-msvc-debug/examples/surfaces_demo/generated_distilled/embedded_distilled_window_catalog_supply.h"
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
  if(NOT (CMAKE_BUILD_TYPE STREQUAL "Debug" AND _catalog_demo STREQUAL "win32_window_catalog_demo"))
    target_sources(${_catalog_demo} PRIVATE "${_window_catalog_embed_header}")
    target_include_directories(${_catalog_demo} PRIVATE
      "${CMAKE_CURRENT_BINARY_DIR}/../generated_distilled")
    add_dependencies(${_catalog_demo} apptraverse_window_catalog_embedded_supply)
  endif()
  target_link_libraries(${_catalog_demo} PRIVATE
    surfaces_demo_window_catalog
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
