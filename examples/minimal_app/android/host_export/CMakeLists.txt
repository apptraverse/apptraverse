# Build-host exporter of the Android MinimalApp supply. Distills the graph with
# the Android presenter classes (no JNI); the APK embeds the generated header.

add_executable(apptraverse_android_minimal_exporter
  minimal_app_export_android.cpp
  ../minimal_app_android_graph.cpp
)
# Keep the host executable out of the deeply nested Android project path.
set_target_properties(apptraverse_android_minimal_exporter PROPERTIES
  OUTPUT_NAME "minimal_android_supply_exporter"
  PDB_NAME "minimal_android_supply_exporter"
  COMPILE_PDB_NAME "minimal_android_supply_exporter"
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}")

target_link_libraries(apptraverse_android_minimal_exporter PRIVATE
  minimal_app_common
  apptraverse
  apptraverse_ui_android_presenters
)
target_include_directories(apptraverse_android_minimal_exporter PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/..
  ${CMAKE_CURRENT_SOURCE_DIR}/../../common
)
if(MSVC)
  target_compile_options(apptraverse_android_minimal_exporter PRIVATE /utf-8)
endif()

# Set by examples/minimal_app/android/native/CMakeLists.txt for its nested
# host build; the header follows the exporter and everything it links.
if(APPTRAVERSE_ANDROID_MINIMAL_SUPPLY_HEADER)
  add_custom_command(
    OUTPUT "${APPTRAVERSE_ANDROID_MINIMAL_SUPPLY_HEADER}"
    COMMAND apptraverse_android_minimal_exporter
            "${APPTRAVERSE_ANDROID_MINIMAL_SUPPLY_HEADER}"
    DEPENDS apptraverse_android_minimal_exporter
    VERBATIM
    COMMENT "Embed distilled Android MinimalApp supply")
  add_custom_target(apptraverse_android_minimal_supply
    DEPENDS "${APPTRAVERSE_ANDROID_MINIMAL_SUPPLY_HEADER}")
endif()
