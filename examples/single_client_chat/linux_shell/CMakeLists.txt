find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK3 REQUIRED IMPORTED_TARGET gtk+-3.0)
find_package(Threads REQUIRED)

add_executable(linux_single_client_chat
  main.cpp
  linux_runtime.cpp
  ../common/aether_p2p_framing.cpp
  ../common/aether_p2p_transport.cpp
)

target_link_libraries(linux_single_client_chat PRIVATE
  apptraverse
  single_client_chat_common
  PkgConfig::GTK3
  Threads::Threads
)

target_include_directories(linux_single_client_chat PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/../common
)

target_compile_features(linux_single_client_chat PRIVATE cxx_std_20)
