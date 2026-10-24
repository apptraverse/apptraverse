# Only the reusable-window catalog consumer is built on this branch.
add_library(surfaces_demo_window_catalog STATIC
  common/window_catalog_model.cpp
  common/window_catalog_localization.cpp
  common/window_catalog_registration.cpp
  windows/window_catalog_graph.cpp
)
target_include_directories(surfaces_demo_window_catalog PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/common
  ${CMAKE_CURRENT_SOURCE_DIR}/windows)
target_link_libraries(surfaces_demo_window_catalog PUBLIC
  apptraverse apptraverse_ui_windows)
target_compile_features(surfaces_demo_window_catalog PUBLIC cxx_std_20)
if(MSVC)
  target_compile_options(surfaces_demo_window_catalog PRIVATE /utf-8)
endif()
add_subdirectory(windows)
