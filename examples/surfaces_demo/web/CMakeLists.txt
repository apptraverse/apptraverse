if(NOT EMSCRIPTEN)
  message(FATAL_ERROR "surfaces_demo/web requires an Emscripten toolchain")
endif()
if(NOT CMAKE_CROSSCOMPILING_EMULATOR)
  message(FATAL_ERROR "surfaces_demo/web requires CMAKE_CROSSCOMPILING_EMULATOR (node)")
endif()

include(${CMAKE_SOURCE_DIR}/cmake/apptraverse_embed_distilled_supply.cmake)

set(_window_catalog_web_embed_graph_sources
  ${CMAKE_CURRENT_SOURCE_DIR}/window_catalog_graph_web.cpp
  ${CMAKE_SOURCE_DIR}/examples/surfaces_demo/common/window_catalog_graph_builder.cpp
  ${CMAKE_SOURCE_DIR}/examples/surfaces_demo/common/window_catalog_model.cpp)

add_executable(apptraverse_export_window_catalog_distilled_graph_web
  window_catalog_export_web.cpp
  window_catalog_graph_web.cpp
)
target_compile_definitions(apptraverse_export_window_catalog_distilled_graph_web PRIVATE
  APPTRAVERSE_ENABLE_DISTILLATION
  EMSCRIPTEN_HAS_UNBOUND_TYPE_NAMES=0
)
target_include_directories(apptraverse_export_window_catalog_distilled_graph_web PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_SOURCE_DIR}/examples/surfaces_demo/common
)
target_link_libraries(apptraverse_export_window_catalog_distilled_graph_web PRIVATE
  surfaces_demo_window_catalog_common
  apptraverse_ui_web
)
target_compile_options(apptraverse_export_window_catalog_distilled_graph_web PRIVATE
  -fno-rtti
  -fno-exceptions
)
target_link_options(apptraverse_export_window_catalog_distilled_graph_web PRIVATE
  -sENVIRONMENT=node
  -sNODERAWFS=1
  -sEXIT_RUNTIME=1
  -sINITIAL_MEMORY=67108864
  --bind
)
set_target_properties(apptraverse_export_window_catalog_distilled_graph_web PROPERTIES
  SUFFIX ".js"
  CROSSCOMPILING_EMULATOR "${CMAKE_CROSSCOMPILING_EMULATOR}"
)

add_executable(web_surfaces_demo
  main.cpp
  web_app.cpp
  web_bridge.cpp
  web_surface_presenter.cpp
  ${CMAKE_SOURCE_DIR}/examples/surfaces_demo/common/surfaces_lifecycle.cpp
  ${CMAKE_SOURCE_DIR}/examples/surfaces_demo/common/surfaces_shutdown_trace.cpp
)

target_compile_definitions(web_surfaces_demo PRIVATE
  APPTRAVERSE_ENABLE_DISTILLATION
)

target_include_directories(web_surfaces_demo PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_SOURCE_DIR}/examples/surfaces_demo/common
)

target_link_libraries(web_surfaces_demo PRIVATE
  surfaces_demo_distill
)

# pthread: one model worker; DOM stays on the browser main thread.
# FORCE_FILESYSTEM + IDBFS: DirectoryDomainStorage under /persistent.
# ALLOW_MEMORY_GROWTH: object graph / publications are not fixed-size.
# EXCEPTION_CATCHING: common code uses throw (e.g. schema Load paths).
# EXPORTED_*: toolbar/tab bridge + FS.syncfs from the shell.
target_compile_options(web_surfaces_demo PRIVATE
  -pthread
  -fexceptions
)

target_link_options(web_surfaces_demo PRIVATE
  -pthread
  -fexceptions
  -sPTHREAD_POOL_SIZE=1
  # Fixed heap: pthread + ALLOW_MEMORY_GROWTH has corrupted ae::Ptr cycle
  # teardown during structural Apply (BuildDecrementGraphImpl OOB).
  -sINITIAL_MEMORY=268435456
  -sALLOW_MEMORY_GROWTH=0
  -sFORCE_FILESYSTEM=1
  -sEXIT_RUNTIME=0
  -lidbfs.js
  -sEXPORTED_FUNCTIONS=_main,_AppTraverseWebAddCurrent,_AppTraverseWebRemoveCurrent,_AppTraverseWebSelectSurface,_AppTraverseWebReportPresentationSize,_AppTraverseWebStop,_AppTraverseWebSyncDone,_AppTraverseWebRequestSync
  -sEXPORTED_RUNTIME_METHODS=FS,addRunDependency,removeRunDependency,ccall,cwrap,UTF8ToString
  --shell-file=${CMAKE_CURRENT_SOURCE_DIR}/shell.html
)

set_target_properties(web_surfaces_demo PROPERTIES
  OUTPUT_NAME "web_surfaces_demo"
  SUFFIX ".html"
)

# Same common sources must compile under em++ / -fno-rtti.
add_executable(web_surfaces_model_compile_check
  ${CMAKE_SOURCE_DIR}/tests/surfaces_model_test.cpp
  ${CMAKE_SOURCE_DIR}/examples/surfaces_demo/common/surfaces_lifecycle.cpp
  ${CMAKE_SOURCE_DIR}/examples/surfaces_demo/common/surfaces_shutdown_trace.cpp
)
target_compile_definitions(web_surfaces_model_compile_check PRIVATE
  APPTRAVERSE_ENABLE_DISTILLATION
)
target_include_directories(web_surfaces_model_compile_check PRIVATE
  ${CMAKE_SOURCE_DIR}/examples/surfaces_demo/common
)
target_link_libraries(web_surfaces_model_compile_check PRIVATE
  surfaces_demo_distill
)
target_compile_options(web_surfaces_model_compile_check PRIVATE
  -pthread
  -fexceptions
)
target_link_options(web_surfaces_model_compile_check PRIVATE
  -pthread
  -fexceptions
  -sPTHREAD_POOL_SIZE=1
  -sALLOW_MEMORY_GROWTH=1
  -sNODERAWFS=1
)
set_target_properties(web_surfaces_model_compile_check PROPERTIES
  OUTPUT_NAME "web_surfaces_model_compile_check"
  SUFFIX ".js"
)

add_executable(web_window_catalog_demo
  window_catalog_main.cpp
  window_catalog_web_app.cpp
  window_catalog_web_persistence.cpp
  window_catalog_graph_web.cpp
)

target_compile_definitions(web_window_catalog_demo PRIVATE
  APPTRAVERSE_ENABLE_DISTILLATION
  APPTRAVERSE_WEB_CATALOG_DOM_TAGGING
  EMSCRIPTEN_HAS_UNBOUND_TYPE_NAMES=0
)

apptraverse_embed_distilled_supply(
  web_window_catalog_demo
  apptraverse_export_window_catalog_distilled_graph_web
  embedded_distilled_window_catalog_web_supply.h
  ${_window_catalog_web_embed_graph_sources})
add_dependencies(web_window_catalog_demo
  apptraverse_export_window_catalog_distilled_graph_web)

target_include_directories(web_window_catalog_demo PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_SOURCE_DIR}/examples/surfaces_demo/common
)

target_link_libraries(web_window_catalog_demo PRIVATE
  surfaces_demo_window_catalog_common
  apptraverse_ui_web
  apptraverse
)

target_compile_options(web_window_catalog_demo PRIVATE
  -pthread
)

target_link_options(web_window_catalog_demo PRIVATE
  -pthread
  -sPTHREAD_POOL_SIZE=1
  -sINITIAL_MEMORY=268435456
  -sALLOW_MEMORY_GROWTH=0
  -sFORCE_FILESYSTEM=1
  -sEXIT_RUNTIME=0
  -lidbfs.js
  --bind
  -sEXPORTED_FUNCTIONS=_main,_ApptraverseWebUiPushButtonClicked,_ApptraverseWebUiListSelectionChanged,_ApptraverseWebUiListRowActivated,_ApptraverseWebIdbfsLoadDone,_ApptraverseWebIdbfsLoadFailed,_ApptraverseWebIdbfsStoreDone,_ApptraverseWebIdbfsStoreFailed,_ApptraverseWindowCatalogRequestSave,_ApptraverseWindowCatalogRequestShutdown,_ApptraverseWindowCatalogMobileSwipe,_ApptraverseWindowCatalogMobilePageShown,_ApptraverseWindowCatalogViewportChanged
  -sALLOW_TABLE_GROWTH=1
  -sEXPORTED_RUNTIME_METHODS=FS,addRunDependency,removeRunDependency,ccall,cwrap,UTF8ToString,count_emval_handles
  --shell-file=${CMAKE_CURRENT_SOURCE_DIR}/window_catalog_shell.html
)

set_target_properties(web_window_catalog_demo PROPERTIES
  OUTPUT_NAME "web_window_catalog_demo"
  SUFFIX ".html"
)

add_executable(apptraverse_window_catalog_web_embedded_session_test
  ${CMAKE_SOURCE_DIR}/tests/window_catalog_web_embedded_session_test.cpp
  window_catalog_graph_web.cpp
)
target_compile_definitions(apptraverse_window_catalog_web_embedded_session_test PRIVATE
  APPTRAVERSE_ENABLE_DISTILLATION
  EMSCRIPTEN_HAS_UNBOUND_TYPE_NAMES=0
)
target_include_directories(apptraverse_window_catalog_web_embedded_session_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_BINARY_DIR}/examples/surfaces_demo/web/generated_distilled/web_window_catalog_demo
  ${CMAKE_SOURCE_DIR}/examples/surfaces_demo/common
)
target_link_libraries(apptraverse_window_catalog_web_embedded_session_test PRIVATE
  surfaces_demo_window_catalog_common
  apptraverse_ui_web
  apptraverse
)
target_compile_options(apptraverse_window_catalog_web_embedded_session_test PRIVATE
  -fno-rtti
  -fno-exceptions
)
target_link_options(apptraverse_window_catalog_web_embedded_session_test PRIVATE
  -sENVIRONMENT=node
  -sNODERAWFS=1
  -sEXIT_RUNTIME=1
  -sINITIAL_MEMORY=134217728
  --bind
)
set_target_properties(apptraverse_window_catalog_web_embedded_session_test PROPERTIES
  SUFFIX ".js"
  CROSSCOMPILING_EMULATOR "${CMAKE_CROSSCOMPILING_EMULATOR}"
)
add_dependencies(apptraverse_window_catalog_web_embedded_session_test
  web_window_catalog_demo)
