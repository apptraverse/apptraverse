add_executable(win32_single_client_chat
  main.cpp
  ../common/aether_p2p_framing.cpp
  ../common/aether_p2p_transport.cpp
)

target_link_libraries(win32_single_client_chat PRIVATE
  apptraverse
  single_client_chat_common
)

target_include_directories(win32_single_client_chat PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/../common
)

if(MSVC)
  target_compile_options(win32_single_client_chat PRIVATE /utf-8)
endif()

# MinGW's objects.a + --whole-archive link path fails to resolve local template/RTTI
# symbols from main when Aether headers are pulled in. MSVC is the supported Win32
# executable toolchain for this example; GNU CI still builds library + CTest.
if(MINGW)
  set_target_properties(win32_single_client_chat PROPERTIES EXCLUDE_FROM_ALL TRUE)
endif()
