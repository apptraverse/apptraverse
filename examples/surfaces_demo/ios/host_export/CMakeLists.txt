# Host-side Debug preparation for the simulator's embedded consumer. It uses
# exactly the same iOS graph/builders; no simulator executable runs on macOS.
add_executable(apptraverse_export_window_catalog_ios
  window_catalog_export_ios.cpp
  ../window_catalog_graph_ios.cpp
  ../window_catalog_graph_builder_ios.cpp
  ../../common/window_catalog_distillation.cpp)
target_link_libraries(apptraverse_export_window_catalog_ios PRIVATE
  surfaces_demo_window_catalog_common apptraverse_ui_ios_presenters)
target_include_directories(apptraverse_export_window_catalog_ios PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/..
  ${CMAKE_CURRENT_SOURCE_DIR}/../../common)
