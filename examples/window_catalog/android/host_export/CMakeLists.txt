add_executable(wc_android_exporter
  window_catalog_export_android.cpp
  ../window_catalog_graph_android.cpp
  ../window_catalog_graph_builder_android.cpp
)
# Keep the private target's object directory short on Windows, including when
# compatible test supplies use an isolated host-exporter CMake cache.
set_target_properties(wc_android_exporter PROPERTIES
  OUTPUT_NAME "apptraverse_android_supply_exporter")

target_link_libraries(wc_android_exporter PRIVATE
  window_catalog_window_catalog_common
  apptraverse
  apptraverse_ui_android_presenters
)
target_include_directories(wc_android_exporter PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/..
  ${CMAKE_CURRENT_SOURCE_DIR}/../../common
)
if(MSVC)
  target_compile_options(wc_android_exporter PRIVATE /utf-8)
endif()

set(_wc_test_supply_args)
if(NOT "${APPTRAVERSE_ANDROID_WINDOW_CATALOG_TEST_LAYOUT_PADDING}" STREQUAL "")
  list(APPEND _wc_test_supply_args "${APPTRAVERSE_ANDROID_WINDOW_CATALOG_TEST_LAYOUT_PADDING}")
endif()
if(APPTRAVERSE_ANDROID_WINDOW_CATALOG_SUPPLY_HEADER)
  add_custom_command(
    OUTPUT "${APPTRAVERSE_ANDROID_WINDOW_CATALOG_SUPPLY_HEADER}"
    COMMAND wc_android_exporter
            "${APPTRAVERSE_ANDROID_WINDOW_CATALOG_SUPPLY_HEADER}" ${_wc_test_supply_args}
    DEPENDS wc_android_exporter
    VERBATIM
    COMMENT "Embed distilled Android WindowCatalog supply")
  add_custom_target(apptraverse_android_window_catalog_supply
    DEPENDS "${APPTRAVERSE_ANDROID_WINDOW_CATALOG_SUPPLY_HEADER}")
endif()
