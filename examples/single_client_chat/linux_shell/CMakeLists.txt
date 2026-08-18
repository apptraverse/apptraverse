cmake_minimum_required(VERSION 3.16)
project(apptraverse_linux_chat_shell LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK3 REQUIRED IMPORTED_TARGET gtk+-3.0)

add_executable(apptraverse_linux_chat_shell main.cpp)
target_link_libraries(apptraverse_linux_chat_shell PRIVATE PkgConfig::GTK3)
