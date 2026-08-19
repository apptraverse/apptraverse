# Multi-platform single-client chat example.
# Android Gradle project lives under android/ and is not built by desktop CMake.

add_subdirectory(common)

if(WIN32)
  add_subdirectory(windows)
endif()

if(UNIX AND NOT APPLE)
  add_subdirectory(linux_shell)
endif()
