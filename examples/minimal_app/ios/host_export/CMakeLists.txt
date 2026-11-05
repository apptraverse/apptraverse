add_executable(apptraverse_export_minimal_distilled_graph_ios
  minimal_app_export_ios.cpp
  ../minimal_app_graph_ios.cpp)
target_link_libraries(apptraverse_export_minimal_distilled_graph_ios PRIVATE
  minimal_app_common
  apptraverse
  apptraverse_ui_ios_presenters)
target_include_directories(apptraverse_export_minimal_distilled_graph_ios PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/..
  ${CMAKE_CURRENT_SOURCE_DIR}/../../common)
