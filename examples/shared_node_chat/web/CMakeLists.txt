add_executable(shared_node_chat_web main.cpp)
target_link_libraries(shared_node_chat_web PRIVATE apptraverse_shared_chat_ui apptraverse_ui_web)
target_compile_options(shared_node_chat_web PRIVATE -pthread)
target_link_options(shared_node_chat_web PRIVATE
  --bind
  -sWASM_BIGINT=1
  -pthread -sPTHREAD_POOL_SIZE=4 -sINITIAL_MEMORY=268435456 -sALLOW_MEMORY_GROWTH=0
  -sSTACK_SIZE=1048576 -sDEFAULT_PTHREAD_STACK_SIZE=2097152 -sEXIT_RUNTIME=0
  -lidbfs.js -sFORCE_FILESYSTEM=1 -sEXPORTED_RUNTIME_METHODS=FS,UTF8ToString,stringToNewUTF8
  -sEXPORTED_FUNCTIONS=_main,_malloc,_free
  "--shell-file=${CMAKE_CURRENT_SOURCE_DIR}/shell.html")
set_target_properties(shared_node_chat_web PROPERTIES OUTPUT_NAME index SUFFIX .html)
set_property(TARGET shared_node_chat_web APPEND PROPERTY LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shell.html")
configure_file(push-worker.js ${CMAKE_CURRENT_BINARY_DIR}/push-worker.js COPYONLY)
