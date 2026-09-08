add_library(main_window_runtime_demo_common STATIC
  common/main_window_model.cpp
  common/main_window_lifecycle.cpp
)

target_include_directories(main_window_runtime_demo_common PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/common
)

target_link_libraries(main_window_runtime_demo_common PUBLIC apptraverse)
target_compile_features(main_window_runtime_demo_common PUBLIC cxx_std_20)

if(WIN32)
  add_subdirectory(windows)
endif()
