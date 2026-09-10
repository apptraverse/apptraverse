if(NOT EMSCRIPTEN)
  message(FATAL_ERROR "surfaces_demo/web requires an Emscripten toolchain")
endif()

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
  -sEXPORTED_FUNCTIONS=_main,_AppTraverseWebAddCurrent,_AppTraverseWebRemoveCurrent,_AppTraverseWebSelectSurface,_AppTraverseWebStop,_AppTraverseWebSyncDone,_AppTraverseWebRequestSync
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
