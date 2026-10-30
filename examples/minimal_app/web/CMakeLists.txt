if(NOT EMSCRIPTEN)
  message(FATAL_ERROR "minimal_app/web requires Emscripten")
endif()

add_executable(web_minimal_app
  minimal_app_web_main.cpp
  minimal_app_web_app.cpp
  minimal_app_web_graph.cpp
)

target_compile_definitions(web_minimal_app PRIVATE
  APPTRAVERSE_ENABLE_DISTILLATION
  EMSCRIPTEN_HAS_UNBOUND_TYPE_NAMES=0
)

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
  -sEXIT_RUNTIME=0
  --bind
  -sEXPORTED_FUNCTIONS=_main,_ApptraverseWebUiPushButtonClicked,_ApptraverseWebUiListSelectionChanged,_ApptraverseWebUiListRowActivated
  -sALLOW_TABLE_GROWTH=1
  -sEXPORTED_RUNTIME_METHODS=addRunDependency,removeRunDependency,UTF8ToString
  --shell-file=${CMAKE_CURRENT_SOURCE_DIR}/minimal_app_shell.html
)

set_target_properties(web_minimal_app PROPERTIES
  OUTPUT_NAME "web_minimal_app"
  SUFFIX ".html"
)

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
