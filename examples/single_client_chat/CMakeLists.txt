# Multi-platform single-client chat example.
# Android Gradle project lives under android/ and is not built by desktop CMake.

if(WIN32)
  add_subdirectory(windows)
endif()
