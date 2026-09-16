find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK3 REQUIRED IMPORTED_TARGET gtk+-3.0)

add_executable(apptraverse_chat
  main.cpp
  linux_chat_app.cpp
  linux_fatal.cpp
)

target_link_libraries(apptraverse_chat PRIVATE
  chat_demo_runtime
  PkgConfig::GTK3
)

target_include_directories(apptraverse_chat PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/../runtime
  ${CMAKE_CURRENT_SOURCE_DIR}/../aether
  ${CMAKE_CURRENT_SOURCE_DIR}/../common
)
