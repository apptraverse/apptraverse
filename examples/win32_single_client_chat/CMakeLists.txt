add_executable(win32_single_client_chat
  main.cpp
)

target_link_libraries(win32_single_client_chat PRIVATE apptraverse)

target_include_directories(win32_single_client_chat PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
)

if(MSVC)
  target_compile_options(win32_single_client_chat PRIVATE /utf-8)
endif()
