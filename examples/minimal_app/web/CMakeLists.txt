if(NOT EMSCRIPTEN)
  message(FATAL_ERROR "minimal_app/web requires Emscripten")
endif()
if(NOT CMAKE_CROSSCOMPILING_EMULATOR)
  message(FATAL_ERROR "minimal_app/web supply export requires CMAKE_CROSSCOMPILING_EMULATOR (node)")
endif()

include(${CMAKE_SOURCE_DIR}/cmake/apptraverse_embed_distilled_supply.cmake)

set(_minimal_web_embed_graph_sources
  ${CMAKE_CURRENT_SOURCE_DIR}/minimal_app_web_graph.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../common/minimal_app_graph_builder.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../common/minimal_application.cpp)

# Build-time exporter: Emscripten executable run under node (toolchain
# CROSSCOMPILING_EMULATOR); NODERAWFS writes the header to the host build tree.
add_executable(apptraverse_export_minimal_distilled_graph_web
  minimal_app_export_web.cpp
  minimal_app_web_graph.cpp
)
target_compile_definitions(apptraverse_export_minimal_distilled_graph_web PRIVATE
  APPTRAVERSE_ENABLE_DISTILLATION
  EMSCRIPTEN_HAS_UNBOUND_TYPE_NAMES=0
)
target_include_directories(apptraverse_export_minimal_distilled_graph_web PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_SOURCE_DIR}/examples/minimal_app/common
)
target_link_libraries(apptraverse_export_minimal_distilled_graph_web PRIVATE
  minimal_app_common
  apptraverse_ui_web
)
target_compile_options(apptraverse_export_minimal_distilled_graph_web PRIVATE
  -fno-rtti
  -fno-exceptions
)
target_link_options(apptraverse_export_minimal_distilled_graph_web PRIVATE
  -sENVIRONMENT=node
  -sNODERAWFS=1
  -sEXIT_RUNTIME=1
  -sINITIAL_MEMORY=67108864
  --bind
)
set_target_properties(apptraverse_export_minimal_distilled_graph_web PROPERTIES
  SUFFIX ".js"
  CROSSCOMPILING_EMULATOR "${CMAKE_CROSSCOMPILING_EMULATOR}"
)

add_executable(web_minimal_app
  minimal_app_web_main.cpp
  minimal_app_web_app.cpp
)

target_compile_definitions(web_minimal_app PRIVATE
  APPTRAVERSE_ENABLE_DISTILLATION
  EMSCRIPTEN_HAS_UNBOUND_TYPE_NAMES=0
  $<$<CONFIG:Debug>:APPTRAVERSE_DEBUG_FRESH_START>
)

apptraverse_embed_distilled_supply(
  web_minimal_app
  apptraverse_export_minimal_distilled_graph_web
  embedded_distilled_minimal_app_supply.h
  ${_minimal_web_embed_graph_sources})
add_dependencies(web_minimal_app apptraverse_export_minimal_distilled_graph_web)

target_include_directories(web_minimal_app PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_SOURCE_DIR}/examples/minimal_app/common
)

target_link_libraries(web_minimal_app PRIVATE
  minimal_app_common
  apptraverse_ui_web
)

target_compile_options(web_minimal_app PRIVATE
  -pthread
  -fno-rtti
  -fno-exceptions
)

target_link_options(web_minimal_app PRIVATE
  -pthread
  -sPTHREAD_POOL_SIZE=1
  -sINITIAL_MEMORY=268435456
  -sALLOW_MEMORY_GROWTH=0
  -sFORCE_FILESYSTEM=1
  -sEXIT_RUNTIME=0
  -lidbfs.js
  --bind
  -sEXPORTED_FUNCTIONS=_main,_ApptraverseWebUiPushButtonClicked,_ApptraverseWebUiListSelectionChanged,_ApptraverseWebUiListRowActivated,_ApptraverseMinimalAppRequestSave,_ApptraverseWebIdbfsLoadDone,_ApptraverseWebIdbfsLoadFailed,_ApptraverseWebIdbfsStoreDone,_ApptraverseWebIdbfsStoreFailed
  -sALLOW_TABLE_GROWTH=1
  -sEXPORTED_RUNTIME_METHODS=FS,addRunDependency,removeRunDependency,UTF8ToString$<$<NOT:$<CONFIG:MinSizeRel>>:,count_emval_handles>
  --shell-file=${CMAKE_CURRENT_SOURCE_DIR}/minimal_app_shell.html
)

set_target_properties(web_minimal_app PROPERTIES
  OUTPUT_NAME "web_minimal_app"
  SUFFIX ".html"
)

# Explicit current-supply consumer for cold USER validation. Ordinary Debug
# starts isolated state; this target intentionally loads the configured USER.
add_executable(web_minimal_app_load_only minimal_app_web_main.cpp minimal_app_web_app.cpp)
foreach(_property INCLUDE_DIRECTORIES COMPILE_OPTIONS LINK_OPTIONS LINK_LIBRARIES)
  get_target_property(_value web_minimal_app ${_property})
  set_property(TARGET web_minimal_app_load_only PROPERTY ${_property} "${_value}")
endforeach()
target_compile_definitions(web_minimal_app_load_only PRIVATE EMSCRIPTEN_HAS_UNBOUND_TYPE_NAMES=0)
add_dependencies(web_minimal_app_load_only web_minimal_app)
set_target_properties(web_minimal_app_load_only PROPERTIES SUFFIX ".html")

add_executable(apptraverse_web_presenter_lifecycle_test
  web_presenter_lifecycle_test.cpp
  minimal_app_web_graph.cpp
)

target_compile_definitions(apptraverse_web_presenter_lifecycle_test PRIVATE
  APPTRAVERSE_ENABLE_DISTILLATION
  EMSCRIPTEN_HAS_UNBOUND_TYPE_NAMES=0
)

target_include_directories(apptraverse_web_presenter_lifecycle_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_SOURCE_DIR}/examples/minimal_app/common
)

target_link_libraries(apptraverse_web_presenter_lifecycle_test PRIVATE
  minimal_app_common
  apptraverse_ui_web
)

target_compile_options(apptraverse_web_presenter_lifecycle_test PRIVATE
  -pthread
  -fno-rtti
  -fno-exceptions
)

target_link_options(apptraverse_web_presenter_lifecycle_test PRIVATE
  -pthread
  -sPTHREAD_POOL_SIZE=1
  -sINITIAL_MEMORY=268435456
  -sALLOW_MEMORY_GROWTH=0
  -sEXIT_RUNTIME=0
  --bind
  -sEXPORTED_FUNCTIONS=_main,_ApptraverseWebUiPushButtonClicked,_ApptraverseWebUiListSelectionChanged,_ApptraverseWebUiListRowActivated
  -sEXPORTED_RUNTIME_METHODS=count_emval_handles
  --shell-file=${CMAKE_CURRENT_SOURCE_DIR}/web_presenter_lifecycle_shell.html
)

set_target_properties(apptraverse_web_presenter_lifecycle_test PROPERTIES
  OUTPUT_NAME "apptraverse_web_presenter_lifecycle_test"
  SUFFIX ".html"
)
