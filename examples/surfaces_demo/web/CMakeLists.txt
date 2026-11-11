if(NOT EMSCRIPTEN)
  message(FATAL_ERROR "surfaces_demo/web requires an Emscripten toolchain")
endif()
if(NOT CMAKE_CROSSCOMPILING_EMULATOR)
  message(FATAL_ERROR "surfaces_demo/web requires CMAKE_CROSSCOMPILING_EMULATOR (node)")
endif()

include(${CMAKE_SOURCE_DIR}/cmake/apptraverse_embed_distilled_supply.cmake)

add_library(surfaces_demo_window_catalog_web STATIC window_catalog_graph_web.cpp)
target_include_directories(surfaces_demo_window_catalog_web PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_SOURCE_DIR}/examples/surfaces_demo/common")
target_link_libraries(surfaces_demo_window_catalog_web PUBLIC
  surfaces_demo_window_catalog_common apptraverse_ui_web)

set(_catalog_web_supply_dir "${CMAKE_CURRENT_BINARY_DIR}/generated_distilled/web_window_catalog_demo")
set(_catalog_web_supply "${_catalog_web_supply_dir}/embedded_distilled_window_catalog_web_supply.h")
file(MAKE_DIRECTORY "${_catalog_web_supply_dir}")
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  # Web's explicitly supported build-host producer, run through Node.
  add_executable(apptraverse_export_window_catalog_distilled_graph_web
    window_catalog_export_web.cpp window_catalog_graph_builder_web.cpp)
  target_link_libraries(apptraverse_export_window_catalog_distilled_graph_web PRIVATE
    surfaces_demo_window_catalog_web)
  target_link_options(apptraverse_export_window_catalog_distilled_graph_web PRIVATE
    -sENVIRONMENT=node -sNODERAWFS=1 -sEXIT_RUNTIME=1 -sINITIAL_MEMORY=67108864 --bind)
  set_target_properties(apptraverse_export_window_catalog_distilled_graph_web PROPERTIES
    SUFFIX ".js" CROSSCOMPILING_EMULATOR "${CMAKE_CROSSCOMPILING_EMULATOR}")
  add_custom_command(OUTPUT "${_catalog_web_supply}"
    COMMAND apptraverse_export_window_catalog_distilled_graph_web "${_catalog_web_supply}"
    DEPENDS apptraverse_export_window_catalog_distilled_graph_web
    VERBATIM COMMENT "Prepare Web WindowCatalog supply with the Debug producer")
else()
  set(APPTRAVERSE_WINDOW_CATALOG_WEB_DEBUG_SUPPLY_HEADER
    "${PROJECT_SOURCE_DIR}/build/wasm-ninja-debug/examples/surfaces_demo/web/generated_distilled/web_window_catalog_demo/embedded_distilled_window_catalog_web_supply.h"
    CACHE FILEPATH "Web supply prepared by the Debug build-host producer")
  if(NOT EXISTS "${APPTRAVERSE_WINDOW_CATALOG_WEB_DEBUG_SUPPLY_HEADER}")
    message(FATAL_ERROR "Build the Debug Web WindowCatalog supply first")
  endif()
  add_custom_command(OUTPUT "${_catalog_web_supply}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${APPTRAVERSE_WINDOW_CATALOG_WEB_DEBUG_SUPPLY_HEADER}" "${_catalog_web_supply}"
    DEPENDS "${APPTRAVERSE_WINDOW_CATALOG_WEB_DEBUG_SUPPLY_HEADER}" VERBATIM)
endif()
add_custom_target(apptraverse_window_catalog_web_supply DEPENDS "${_catalog_web_supply}")

add_executable(web_surfaces_demo
  main.cpp
  web_app.cpp
  web_bridge.cpp
  web_surface_presenter.cpp
  ${CMAKE_SOURCE_DIR}/examples/surfaces_demo/common/surfaces_lifecycle.cpp
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

foreach(_catalog_web_app web_window_catalog_demo web_window_catalog_demo_load_only)
  add_executable(${_catalog_web_app}
    window_catalog_main.cpp window_catalog_web_app.cpp
    window_catalog_web_persistence.cpp "${_catalog_web_supply}")
  add_dependencies(${_catalog_web_app} apptraverse_window_catalog_web_supply)
  target_include_directories(${_catalog_web_app} PRIVATE "${_catalog_web_supply_dir}")
  target_link_libraries(${_catalog_web_app} PRIVATE surfaces_demo_window_catalog_web)
  target_compile_options(${_catalog_web_app} PRIVATE -pthread)
  target_link_options(${_catalog_web_app} PRIVATE
    -pthread -sPTHREAD_POOL_SIZE=1 -sINITIAL_MEMORY=268435456
    -sALLOW_MEMORY_GROWTH=0 -sFORCE_FILESYSTEM=1 -sEXIT_RUNTIME=0
    -lidbfs.js --bind
    -sEXPORTED_FUNCTIONS=_main,_ApptraverseWebUiPushButtonClicked,_ApptraverseWebUiListSelectionChanged,_ApptraverseWebUiListRowActivated,_ApptraverseWebIdbfsLoadDone,_ApptraverseWebIdbfsLoadFailed,_ApptraverseWebIdbfsStoreDone,_ApptraverseWebIdbfsStoreFailed,_ApptraverseWindowCatalogRequestShutdown,_ApptraverseWindowCatalogPageHidden,_ApptraverseWindowCatalogMobileSwipe,_ApptraverseWindowCatalogMobilePageShown,_ApptraverseWindowCatalogViewportChanged,_ApptraverseWindowCatalogChildViewportChanged
    -sALLOW_TABLE_GROWTH=1
    -sEXPORTED_RUNTIME_METHODS=FS,addRunDependency,removeRunDependency,ccall,cwrap,UTF8ToString,count_emval_handles
    --shell-file=${CMAKE_CURRENT_SOURCE_DIR}/window_catalog_shell.html)
  set_target_properties(${_catalog_web_app} PROPERTIES SUFFIX ".html")
endforeach()
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  target_compile_definitions(web_window_catalog_demo PRIVATE APPTRAVERSE_WEB_CATALOG_FRESH_USER)
endif()

add_executable(apptraverse_window_catalog_web_embedded_session_test
  ${CMAKE_SOURCE_DIR}/tests/window_catalog_web_embedded_session_test.cpp
  "${_catalog_web_supply}")
add_executable(apptraverse_window_catalog_web_portable_user_fixture
  ${CMAKE_SOURCE_DIR}/tests/window_catalog_portable_user_fixture.cpp
  "${_catalog_web_supply}")
add_dependencies(apptraverse_window_catalog_web_portable_user_fixture
  apptraverse_window_catalog_web_supply)
target_include_directories(apptraverse_window_catalog_web_portable_user_fixture PRIVATE
  "${_catalog_web_supply_dir}")
target_link_libraries(apptraverse_window_catalog_web_portable_user_fixture PRIVATE
  surfaces_demo_window_catalog_web)
target_compile_definitions(apptraverse_window_catalog_web_portable_user_fixture PRIVATE
  APPTRAVERSE_PORTABLE_PLATFORM="web")
target_link_options(apptraverse_window_catalog_web_portable_user_fixture PRIVATE
  -sENVIRONMENT=node -sNODERAWFS=1 -sEXIT_RUNTIME=1 -sINITIAL_MEMORY=134217728 --bind)
set_target_properties(apptraverse_window_catalog_web_portable_user_fixture PROPERTIES SUFFIX ".js")
add_dependencies(apptraverse_window_catalog_web_embedded_session_test apptraverse_window_catalog_web_supply)
target_include_directories(apptraverse_window_catalog_web_embedded_session_test PRIVATE
  "${_catalog_web_supply_dir}")
target_link_libraries(apptraverse_window_catalog_web_embedded_session_test PRIVATE
  surfaces_demo_window_catalog_web)
target_link_options(apptraverse_window_catalog_web_embedded_session_test PRIVATE
  -sENVIRONMENT=node -sNODERAWFS=1 -sEXIT_RUNTIME=1 -sINITIAL_MEMORY=134217728 --bind)
set_target_properties(apptraverse_window_catalog_web_embedded_session_test PROPERTIES
  SUFFIX ".js" CROSSCOMPILING_EMULATOR "${CMAKE_CROSSCOMPILING_EMULATOR}")
