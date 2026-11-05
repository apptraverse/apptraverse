# Build-host exporter of the Android MinimalApp supply. Distills the graph with
# the Android presenter classes (no JNI); the APK embeds the generated header.

add_executable(apptraverse_export_minimal_distilled_graph_android
  minimal_app_export_android.cpp
  ../minimal_app_android_graph.cpp
)
target_link_libraries(apptraverse_export_minimal_distilled_graph_android PRIVATE
  minimal_app_common
  apptraverse
  apptraverse_ui_android_presenters
)
target_include_directories(apptraverse_export_minimal_distilled_graph_android PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/..
  ${CMAKE_CURRENT_SOURCE_DIR}/../../common
)
if(MSVC)
  target_compile_options(apptraverse_export_minimal_distilled_graph_android PRIVATE /utf-8)
endif()

# Set by examples/minimal_app/android/native/CMakeLists.txt for its nested
# host build; the header follows the exporter and everything it links.
if(APPTRAVERSE_ANDROID_MINIMAL_SUPPLY_HEADER)
  add_custom_command(
    OUTPUT "${APPTRAVERSE_ANDROID_MINIMAL_SUPPLY_HEADER}"
    COMMAND apptraverse_export_minimal_distilled_graph_android
            "${APPTRAVERSE_ANDROID_MINIMAL_SUPPLY_HEADER}"
    DEPENDS apptraverse_export_minimal_distilled_graph_android
    VERBATIM
    COMMENT "Embed distilled Android MinimalApp supply")
  add_custom_target(apptraverse_android_minimal_supply
    DEPENDS "${APPTRAVERSE_ANDROID_MINIMAL_SUPPLY_HEADER}")
endif()
