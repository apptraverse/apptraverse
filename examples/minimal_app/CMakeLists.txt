add_library(minimal_app_common STATIC
  common/minimal_application.cpp
)
target_include_directories(minimal_app_common PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/common
  ${CMAKE_CURRENT_SOURCE_DIR}/windows
)
target_link_libraries(minimal_app_common PUBLIC apptraverse apptraverse_ui_windows)
target_compile_features(minimal_app_common PUBLIC cxx_std_20)
if(MSVC)
  target_compile_options(minimal_app_common PRIVATE /utf-8)
endif()

add_subdirectory(windows)
