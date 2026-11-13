add_executable(apptraverse_android_supply_exporter
  window_catalog_export_android.cpp
  ../window_catalog_graph_android.cpp
  ../window_catalog_graph_builder_android.cpp
)
target_link_libraries(apptraverse_android_supply_exporter PRIVATE
  surfaces_demo_window_catalog_common
  apptraverse
  apptraverse_ui_android_presenters
)
target_include_directories(apptraverse_android_supply_exporter PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/..
  ${CMAKE_CURRENT_SOURCE_DIR}/../../common
)
if(MSVC)
  target_compile_options(apptraverse_android_supply_exporter PRIVATE /utf-8)
endif()

if(APPTRAVERSE_ANDROID_WINDOW_CATALOG_SUPPLY_HEADER)
  add_custom_command(
    OUTPUT "${APPTRAVERSE_ANDROID_WINDOW_CATALOG_SUPPLY_HEADER}"
    COMMAND apptraverse_android_supply_exporter
            "${APPTRAVERSE_ANDROID_WINDOW_CATALOG_SUPPLY_HEADER}"
    DEPENDS apptraverse_android_supply_exporter
    VERBATIM
    COMMENT "Embed distilled Android WindowCatalog supply")
  add_custom_target(apptraverse_android_window_catalog_supply
    DEPENDS "${APPTRAVERSE_ANDROID_WINDOW_CATALOG_SUPPLY_HEADER}")
endif()
