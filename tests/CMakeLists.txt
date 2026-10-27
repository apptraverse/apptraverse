option(APPTRAVERSE_BUILD_EVOLUTION_TESTS
  "Build isolated application/state evolution fixtures and their Python matrix" ON)

if(APPTRAVERSE_BUILD_EVOLUTION_TESTS)
  find_package(Python3 3.9 REQUIRED COMPONENTS Interpreter)

  function(apptraverse_add_evolution_fixture target exe_revision schema_revision evolution_mode)
    add_executable(${target} application_evolution_fixture.cpp)
    target_link_libraries(${target} PRIVATE apptraverse)
    target_compile_definitions(${target} PRIVATE
      APPTRAVERSE_EXE_REVISION=${exe_revision}
      APPTRAVERSE_SCHEMA_REVISION=${schema_revision}
      APPTRAVERSE_EVOLUTION_MODE=${evolution_mode}
    )
  endfunction()

  apptraverse_add_evolution_fixture(apptraverse_evolution_stable_a 0 0 0)
  apptraverse_add_evolution_fixture(apptraverse_evolution_stable_b 1 0 0)
  apptraverse_add_evolution_fixture(apptraverse_evolution_stable_c 2 0 0)
  apptraverse_add_evolution_fixture(apptraverse_evolution_schema_a 0 0 1)
  apptraverse_add_evolution_fixture(apptraverse_evolution_schema_b 1 1 1)
  apptraverse_add_evolution_fixture(apptraverse_evolution_schema_c 2 2 1)

  add_custom_target(apptraverse_application_evolution_fixtures DEPENDS
    apptraverse_evolution_stable_a
    apptraverse_evolution_stable_b
    apptraverse_evolution_stable_c
    apptraverse_evolution_schema_a
    apptraverse_evolution_schema_b
    apptraverse_evolution_schema_c
  )

  file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/application evolution states")
  add_test(
    NAME apptraverse_application_evolution_matrix
    COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_LIST_DIR}/application_evolution_matrix.py"
      --stable
        "$<TARGET_FILE:apptraverse_evolution_stable_a>"
        "$<TARGET_FILE:apptraverse_evolution_stable_b>"
        "$<TARGET_FILE:apptraverse_evolution_stable_c>"
      --evolving
        "$<TARGET_FILE:apptraverse_evolution_schema_a>"
        "$<TARGET_FILE:apptraverse_evolution_schema_b>"
        "$<TARGET_FILE:apptraverse_evolution_schema_c>"
      --work-root "${CMAKE_CURRENT_BINARY_DIR}/application evolution states"
  )
  set_tests_properties(apptraverse_application_evolution_matrix PROPERTIES
    TIMEOUT 180
    LABELS "serialization;evolution"
  )
  # A/B share one Window ClassId. v0 stores x/y, v1 stores width/height.
  add_executable(apptraverse_window_layers_a window_schema_layers_fixture.cpp)
  add_executable(apptraverse_window_layers_b window_schema_layers_fixture.cpp)
  add_executable(apptraverse_window_layers_c window_schema_layers_fixture.cpp)
  target_link_libraries(apptraverse_window_layers_a PRIVATE apptraverse)
  target_link_libraries(apptraverse_window_layers_b PRIVATE apptraverse)
  target_link_libraries(apptraverse_window_layers_c PRIVATE apptraverse)
  target_compile_definitions(apptraverse_window_layers_a PRIVATE
    APPTRAVERSE_WINDOW_EXE_REVISION=0)
  target_compile_definitions(apptraverse_window_layers_b PRIVATE
    APPTRAVERSE_WINDOW_EXE_REVISION=1)
  target_compile_definitions(apptraverse_window_layers_c PRIVATE
    APPTRAVERSE_WINDOW_EXE_REVISION=2)
  add_custom_target(apptraverse_window_schema_layers_fixtures DEPENDS
    apptraverse_window_layers_a apptraverse_window_layers_b
    apptraverse_window_layers_c)
  add_dependencies(apptraverse_application_evolution_fixtures
    apptraverse_window_schema_layers_fixtures)

  file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/window layer states")
  add_test(
    NAME apptraverse_window_schema_layers_roundtrip
    COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_LIST_DIR}/window_schema_layers_matrix.py"
      --a "$<TARGET_FILE:apptraverse_window_layers_a>"
      --b "$<TARGET_FILE:apptraverse_window_layers_b>"
      --c "$<TARGET_FILE:apptraverse_window_layers_c>"
      --work-root "${CMAKE_CURRENT_BINARY_DIR}/window layer states"
  )
  set_tests_properties(apptraverse_window_schema_layers_roundtrip PROPERTIES
    TIMEOUT 90
    LABELS "serialization;evolution;rollback"
  )

  add_executable(apptraverse_event_downgrade_a event_downgrade_fixture.cpp)
  add_executable(apptraverse_event_downgrade_a_late event_downgrade_fixture.cpp)
  add_executable(apptraverse_event_downgrade_b event_downgrade_fixture.cpp)
  target_link_libraries(apptraverse_event_downgrade_a PRIVATE apptraverse)
  target_link_libraries(apptraverse_event_downgrade_a_late PRIVATE apptraverse)
  target_link_libraries(apptraverse_event_downgrade_b PRIVATE apptraverse)
  target_compile_definitions(apptraverse_event_downgrade_a PRIVATE
    APPTRAVERSE_EVENT_EXE_REVISION=0)
  target_compile_definitions(apptraverse_event_downgrade_a_late PRIVATE
    APPTRAVERSE_EVENT_EXE_REVISION=2)
  target_compile_definitions(apptraverse_event_downgrade_b PRIVATE
    APPTRAVERSE_EVENT_EXE_REVISION=1)
  add_custom_target(apptraverse_event_downgrade_fixtures DEPENDS
    apptraverse_event_downgrade_a
    apptraverse_event_downgrade_a_late
    apptraverse_event_downgrade_b)
  add_dependencies(apptraverse_application_evolution_fixtures
    apptraverse_event_downgrade_fixtures)

  file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/event downgrade states")
  add_test(
    NAME apptraverse_event_downgrade_matrix
    COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_LIST_DIR}/event_downgrade_matrix.py"
      --a "$<TARGET_FILE:apptraverse_event_downgrade_a>"
      --b "$<TARGET_FILE:apptraverse_event_downgrade_b>"
      --work-root "${CMAKE_CURRENT_BINARY_DIR}/event downgrade states"
  )
  set_tests_properties(apptraverse_event_downgrade_matrix PROPERTIES
    TIMEOUT 60
    LABELS "serialization;evolution;rollback"
  )

  add_test(
    NAME apptraverse_event_downgrade_chain_matrix
    COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_LIST_DIR}/event_downgrade_chain_matrix.py"
      --a "$<TARGET_FILE:apptraverse_event_downgrade_a>"
      --a-late "$<TARGET_FILE:apptraverse_event_downgrade_a_late>"
      --b "$<TARGET_FILE:apptraverse_event_downgrade_b>"
      --work-root "${CMAKE_CURRENT_BINARY_DIR}/event downgrade states"
  )
  set_tests_properties(apptraverse_event_downgrade_chain_matrix PROPERTIES
    TIMEOUT 60
    LABELS "serialization;evolution;rollback"
  )

  add_executable(apptraverse_event_version_a event_version_fixture.cpp)
  add_executable(apptraverse_event_version_b event_version_fixture.cpp)
  target_link_libraries(apptraverse_event_version_a PRIVATE apptraverse)
  target_link_libraries(apptraverse_event_version_b PRIVATE apptraverse)
  target_compile_definitions(apptraverse_event_version_a PRIVATE
    APPTRAVERSE_EVENT_VERSION_EXE_REVISION=0)
  target_compile_definitions(apptraverse_event_version_b PRIVATE
    APPTRAVERSE_EVENT_VERSION_EXE_REVISION=1)
  add_custom_target(apptraverse_event_version_fixtures DEPENDS
    apptraverse_event_version_a
    apptraverse_event_version_b)
  add_dependencies(apptraverse_application_evolution_fixtures
    apptraverse_event_version_fixtures)

  file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/event version states")
  add_test(
    NAME apptraverse_event_version_matrix
    COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_LIST_DIR}/event_version_matrix.py"
      --a "$<TARGET_FILE:apptraverse_event_version_a>"
      --b "$<TARGET_FILE:apptraverse_event_version_b>"
      --work-root "${CMAKE_CURRENT_BINARY_DIR}/event version states"
  )
  set_tests_properties(apptraverse_event_version_matrix PROPERTIES
    TIMEOUT 90
    LABELS "serialization;evolution;journal"
  )

  if(APPTRAVERSE_BUILD_LEGACY_EXAMPLES AND WIN32)
    add_dependencies(apptraverse_application_evolution_fixtures
      win32_surfaces_demo win32_surfaces_demo_v1)
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/surfaces demo version states")
    add_test(
      NAME apptraverse_surfaces_demo_version_matrix
      COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_LIST_DIR}/surfaces_demo_version_matrix.py"
        --a "$<TARGET_FILE:win32_surfaces_demo>"
        --b "$<TARGET_FILE:win32_surfaces_demo_v1>"
        --work-root "${CMAKE_CURRENT_BINARY_DIR}/surfaces demo version states"
    )
    set_tests_properties(apptraverse_surfaces_demo_version_matrix PROPERTIES
      TIMEOUT 120
      LABELS "serialization;evolution;surfaces"
    )

    add_test(
      NAME apptraverse_surfaces_demo_lifecycle_matrix
      COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_LIST_DIR}/surfaces_demo_lifecycle_matrix.py"
        --a "$<TARGET_FILE:win32_surfaces_demo>"
        --b "$<TARGET_FILE:win32_surfaces_demo_v1>"
        --work-root "${CMAKE_CURRENT_BINARY_DIR}/surfaces demo lifecycle states"
    )
    set_tests_properties(apptraverse_surfaces_demo_lifecycle_matrix PROPERTIES
      TIMEOUT 180
      LABELS "serialization;surfaces;lifecycle"
    )
    add_test(
      NAME apptraverse_surfaces_demo_lifecycle_corruption_guard
      COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_LIST_DIR}/surfaces_demo_lifecycle_corruption_guard.py"
        --a "$<TARGET_FILE:win32_surfaces_demo>"
        --b "$<TARGET_FILE:win32_surfaces_demo_v1>"
        --work-root "${CMAKE_CURRENT_BINARY_DIR}/surfaces demo lifecycle states"
    )
    set_tests_properties(apptraverse_surfaces_demo_lifecycle_corruption_guard PROPERTIES
      TIMEOUT 180
      LABELS "serialization;surfaces;lifecycle;rollback"
    )
  endif()
endif()

add_executable(apptraverse_ui_desktop_window_geometry_test
  ui/ui_desktop_window_geometry_test.cpp
)
target_link_libraries(apptraverse_ui_desktop_window_geometry_test PRIVATE
  apptraverse
)
add_test(
  NAME apptraverse_ui_desktop_window_geometry_test
  COMMAND apptraverse_ui_desktop_window_geometry_test
)

add_executable(apptraverse_node_local_commit_journal_scaling_test
  node_local_commit_journal_scaling_test.cpp
)
target_link_libraries(apptraverse_node_local_commit_journal_scaling_test PRIVATE
  apptraverse
  apptraverse_ui_extended
)
add_test(
  NAME apptraverse_node_local_commit_journal_scaling_test
  COMMAND apptraverse_node_local_commit_journal_scaling_test
)

add_executable(apptraverse_ui_edit_box_headless_test
  ui/ui_edit_box_headless_test.cpp
)
target_link_libraries(apptraverse_ui_edit_box_headless_test PRIVATE apptraverse apptraverse_ui_extended)
add_test(
  NAME apptraverse_ui_edit_box_headless_test
  COMMAND apptraverse_ui_edit_box_headless_test
)

add_executable(apptraverse_ui_control_materialization_test
  ui/ui_control_materialization_test.cpp
)
target_link_libraries(apptraverse_ui_control_materialization_test PRIVATE apptraverse apptraverse_ui_extended)
add_test(
  NAME apptraverse_ui_control_materialization_test
  COMMAND apptraverse_ui_control_materialization_test
)

add_executable(apptraverse_event_sourced_core_test
  event_sourced_core_test.cpp
)

target_link_libraries(apptraverse_event_sourced_core_test PRIVATE apptraverse)

add_test(
  NAME apptraverse_event_sourced_core_test
  COMMAND apptraverse_event_sourced_core_test
)

find_package(Python3 3.9 REQUIRED COMPONENTS Interpreter)
include(${CMAKE_SOURCE_DIR}/cmake/apptraverse_surfaces_release_metadata.cmake)

add_executable(apptraverse_release_metadata_probe release_metadata_probe.cpp)
target_compile_features(apptraverse_release_metadata_probe PRIVATE cxx_std_20)
target_link_libraries(apptraverse_release_metadata_probe PRIVATE
  apptraverse_compile_policy)
apptraverse_embed_surfaces_release_metadata(
  apptraverse_release_metadata_probe
  "${CMAKE_SOURCE_DIR}/examples/surfaces_demo/release.json")

add_executable(apptraverse_release_metadata_probe_alt release_metadata_probe.cpp)
target_compile_features(apptraverse_release_metadata_probe_alt PRIVATE cxx_std_20)
target_link_libraries(apptraverse_release_metadata_probe_alt PRIVATE
  apptraverse_compile_policy)
apptraverse_embed_surfaces_release_metadata(
  apptraverse_release_metadata_probe_alt
  "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/release_metadata/release_1.json")

set(_release_metadata_gen
    "${CMAKE_SOURCE_DIR}/cmake/generate_surfaces_release_metadata.cmake")
set(_release_metadata_fail_out
    "${CMAKE_CURRENT_BINARY_DIR}/release_metadata_config_fail.h")

add_test(
  NAME apptraverse_release_metadata_probe
  COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_LIST_DIR}/release_metadata_probe_test.py"
    --probe "$<TARGET_FILE:apptraverse_release_metadata_probe>"
    --config "${CMAKE_SOURCE_DIR}/examples/surfaces_demo/release.json"
  WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}")

add_test(
  NAME apptraverse_release_metadata_probe_isolated
  COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_LIST_DIR}/release_metadata_probe_test.py"
    --probe "$<TARGET_FILE:apptraverse_release_metadata_probe>"
    --config "${CMAKE_SOURCE_DIR}/examples/surfaces_demo/release.json"
    --isolated-cwd
  WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}")

add_test(
  NAME apptraverse_release_metadata_probe_dual
  COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_LIST_DIR}/release_metadata_probe_dual_test.py"
    --probe0 "$<TARGET_FILE:apptraverse_release_metadata_probe>"
    --config0 "${CMAKE_SOURCE_DIR}/examples/surfaces_demo/release.json"
    --probe1 "$<TARGET_FILE:apptraverse_release_metadata_probe_alt>"
    --config1 "${CMAKE_CURRENT_LIST_DIR}/fixtures/release_metadata/release_1.json"
  WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}")

function(_apptraverse_release_metadata_config_fail_test test_name config_path)
  add_test(
    NAME ${test_name}
    COMMAND ${CMAKE_COMMAND}
      "-DCONFIG_JSON=${config_path}"
      "-DOUTPUT=${_release_metadata_fail_out}"
      -P "${_release_metadata_gen}")
  set_tests_properties(${test_name} PROPERTIES WILL_FAIL TRUE)
endfunction()

_apptraverse_release_metadata_config_fail_test(
  apptraverse_release_metadata_config_missing_field
  "${CMAKE_CURRENT_LIST_DIR}/fixtures/release_metadata/no_release_field.json")
_apptraverse_release_metadata_config_fail_test(
  apptraverse_release_metadata_config_string_release
  "${CMAKE_CURRENT_LIST_DIR}/fixtures/release_metadata/string_release.json")
_apptraverse_release_metadata_config_fail_test(
  apptraverse_release_metadata_config_negative_release
  "${CMAKE_CURRENT_LIST_DIR}/fixtures/release_metadata/negative_release.json")
_apptraverse_release_metadata_config_fail_test(
  apptraverse_release_metadata_config_fractional_release
  "${CMAKE_CURRENT_LIST_DIR}/fixtures/release_metadata/fractional_release.json")
_apptraverse_release_metadata_config_fail_test(
  apptraverse_release_metadata_config_overflow_release
  "${CMAKE_CURRENT_LIST_DIR}/fixtures/release_metadata/overflow_release.json")

add_test(
  NAME apptraverse_release_metadata_config_missing_file
  COMMAND ${CMAKE_COMMAND}
    "-DCONFIG_JSON=${CMAKE_CURRENT_BINARY_DIR}/release_metadata_no_such.json"
    "-DOUTPUT=${_release_metadata_fail_out}"
    -P "${_release_metadata_gen}")
set_tests_properties(apptraverse_release_metadata_config_missing_file
                     PROPERTIES WILL_FAIL TRUE)

include(${CMAKE_SOURCE_DIR}/cmake/apptraverse_embed_distilled_supply.cmake)

add_library(apptraverse_distilled_lazy_graph_bootstrap STATIC
  distilled_lazy_graph_bootstrap.cpp
)
target_link_libraries(apptraverse_distilled_lazy_graph_bootstrap PUBLIC apptraverse)
target_include_directories(apptraverse_distilled_lazy_graph_bootstrap PUBLIC
  ${CMAKE_CURRENT_LIST_DIR}
)

add_executable(apptraverse_export_lazy_distilled_graph
  export_lazy_distilled_graph.cpp
)
target_link_libraries(apptraverse_export_lazy_distilled_graph PRIVATE
  apptraverse_distilled_lazy_graph_bootstrap
  apptraverse
)

add_executable(apptraverse_lazy_distilled_load_consumer
  lazy_distilled_load_consumer.cpp
)
target_link_libraries(apptraverse_lazy_distilled_load_consumer PRIVATE
  apptraverse_distilled_lazy_graph_bootstrap
  apptraverse
)
apptraverse_embed_distilled_supply(
  apptraverse_lazy_distilled_load_consumer
  apptraverse_export_lazy_distilled_graph
  embedded_distilled_lazy_doc_supply.h
)

add_test(
  NAME apptraverse_lazy_distilled_embedded_matrix
  COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_LIST_DIR}/lazy_distilled_embedded_test.py"
    --consumer "$<TARGET_FILE:apptraverse_lazy_distilled_load_consumer>"
)
set_tests_properties(apptraverse_lazy_distilled_embedded_matrix PROPERTIES
  TIMEOUT 60
  LABELS "serialization;distilled"
)

if(TARGET apptraverse_export_surfaces_distilled_graph)
  add_executable(apptraverse_surfaces_distilled_load_consumer
    surfaces_distilled_load_consumer.cpp
  )
  target_link_libraries(apptraverse_surfaces_distilled_load_consumer PRIVATE
    surfaces_demo_presentation
    apptraverse_surfaces
    apptraverse
  )
  target_include_directories(apptraverse_surfaces_distilled_load_consumer PRIVATE
    ${CMAKE_SOURCE_DIR}/examples/surfaces_demo/common
  )
  apptraverse_embed_surfaces_release_metadata(
    apptraverse_surfaces_distilled_load_consumer
    "${CMAKE_SOURCE_DIR}/examples/surfaces_demo/release.json")
  apptraverse_embed_distilled_supply(
    apptraverse_surfaces_distilled_load_consumer
    apptraverse_export_surfaces_distilled_graph
    embedded_distilled_surfaces_supply.h
  )

  add_test(
    NAME apptraverse_surfaces_distilled_embedded_matrix
    COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_LIST_DIR}/surfaces_distilled_load_test.py"
      --consumer "$<TARGET_FILE:apptraverse_surfaces_distilled_load_consumer>"
      --config "${CMAKE_SOURCE_DIR}/examples/surfaces_demo/release.json"
  )
  set_tests_properties(apptraverse_surfaces_distilled_embedded_matrix PROPERTIES
    TIMEOUT 90
    LABELS "serialization;distilled;surfaces"
  )
endif()

if(APPTRAVERSE_BUILD_EVOLUTION_TESTS)
  include(${CMAKE_SOURCE_DIR}/cmake/apptraverse_embed_release_metadata.cmake)

  function(apptraverse_add_prefab_export_exe target revision)
    add_executable(${target}
      export_prefab_release_backing.cpp
      prefab_release_model.cpp
    )
    target_compile_definitions(${target} PRIVATE
      APPTRAVERSE_PREFAB_EXE_REVISION=${revision})
    target_link_libraries(${target} PRIVATE apptraverse)
    target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_LIST_DIR})
  endfunction()

  function(apptraverse_add_prefab_release_exe target revision embed_exporter header_basename)
    add_executable(${target}
      prefab_release_fixture.cpp
      prefab_release_model.cpp
      prefab_fixture_state.cpp
    )
    target_link_libraries(${target} PRIVATE apptraverse)
    target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_LIST_DIR})
    target_compile_definitions(${target} PRIVATE
      APPTRAVERSE_PREFAB_EXE_REVISION=${revision}
      APPTRAVERSE_PREFAB_FIXTURE_IMPL=1)
    apptraverse_embed_distilled_supply(
      ${target}
      ${embed_exporter}
      ${header_basename})
  endfunction()

  apptraverse_add_prefab_export_exe(apptraverse_export_prefab_release_r0_backing 0)
  target_compile_definitions(apptraverse_export_prefab_release_r0_backing PRIVATE
    APPTRAVERSE_PREFAB_EXPORT_GRAPH_NAMESPACE=prefab_release_r0
    APPTRAVERSE_PREFAB_EXPORT_HEADER_GUARD=APPTRAVERSE_EMBEDDED_PREFAB_RELEASE_R0_SUPPLY_H_
    "APPTRAVERSE_PREFAB_EXPORT_MAIN_TITLE=\"main-title-A\"")

  apptraverse_add_prefab_export_exe(apptraverse_export_prefab_release_r1_backing 0)
  target_compile_definitions(apptraverse_export_prefab_release_r1_backing PRIVATE
    APPTRAVERSE_PREFAB_EXPORT_GRAPH_NAMESPACE=prefab_release_r1
    APPTRAVERSE_PREFAB_EXPORT_HEADER_GUARD=APPTRAVERSE_EMBEDDED_PREFAB_RELEASE_R1_SUPPLY_H_
    "APPTRAVERSE_PREFAB_EXPORT_MAIN_TITLE=\"main-title-B\"")

  apptraverse_add_prefab_export_exe(apptraverse_export_prefab_release_r2_backing 2)
  target_compile_definitions(apptraverse_export_prefab_release_r2_backing PRIVATE
    APPTRAVERSE_PREFAB_EXPORT_GRAPH_NAMESPACE=prefab_release_r2
    APPTRAVERSE_PREFAB_EXPORT_HEADER_GUARD=APPTRAVERSE_EMBEDDED_PREFAB_RELEASE_R2_SUPPLY_H_
    "APPTRAVERSE_PREFAB_EXPORT_MAIN_TITLE=\"main-title-A\"")

  add_executable(apptraverse_export_prefab_release_backing_v30
    export_prefab_release_backing.cpp
    prefab_release_model.cpp
  )
  target_compile_definitions(apptraverse_export_prefab_release_backing_v30 PRIVATE
    APPTRAVERSE_PREFAB_EXE_REVISION=2
    APPTRAVERSE_PREFAB_EXPORT_MAIN_POSX=30
    APPTRAVERSE_PREFAB_EXPORT_GRAPH_NAMESPACE=prefab_release_v30
    APPTRAVERSE_PREFAB_EXPORT_HEADER_GUARD=APPTRAVERSE_EMBEDDED_PREFAB_RELEASE_SUPPLY_V30_H_
    "APPTRAVERSE_PREFAB_EXPORT_MAIN_TITLE=\"main-title-A\"")
  target_link_libraries(apptraverse_export_prefab_release_backing_v30 PRIVATE apptraverse)
  target_include_directories(apptraverse_export_prefab_release_backing_v30 PRIVATE
    ${CMAKE_CURRENT_LIST_DIR})

  apptraverse_add_prefab_release_exe(apptraverse_prefab_release_r0 0
    apptraverse_export_prefab_release_r0_backing
    embedded_distilled_prefab_release_r0_supply.h)
  apptraverse_embed_release_metadata(apptraverse_prefab_release_r0
    "${CMAKE_CURRENT_LIST_DIR}/release_lifecycle/releases/r0/release.json"
    apptraverse::test::prefab_release_lifecycle)

  apptraverse_add_prefab_release_exe(apptraverse_prefab_release_r1 1
    apptraverse_export_prefab_release_r1_backing
    embedded_distilled_prefab_release_r1_supply.h)
  apptraverse_embed_release_metadata(apptraverse_prefab_release_r1
    "${CMAKE_CURRENT_LIST_DIR}/release_lifecycle/releases/r1/release.json"
    apptraverse::test::prefab_release_lifecycle)

  apptraverse_add_prefab_release_exe(apptraverse_prefab_release_r2 2
    apptraverse_export_prefab_release_r2_backing
    embedded_distilled_prefab_release_r2_supply.h)
  apptraverse_embed_release_metadata(apptraverse_prefab_release_r2
    "${CMAKE_CURRENT_LIST_DIR}/release_lifecycle/releases/r2/release.json"
    apptraverse::test::prefab_release_lifecycle)
  apptraverse_embed_distilled_supply(
    apptraverse_prefab_release_r2
    apptraverse_export_prefab_release_backing_v30
    embedded_distilled_prefab_release_supply_v30.h)

  add_executable(apptraverse_export_prefab_release_r1_package
    export_prefab_release_r1_package.cpp
    prefab_release_model.cpp
  )
  target_compile_definitions(apptraverse_export_prefab_release_r1_package PRIVATE
    APPTRAVERSE_PREFAB_EXE_REVISION=0)
  target_link_libraries(apptraverse_export_prefab_release_r1_package PRIVATE apptraverse)
  target_include_directories(apptraverse_export_prefab_release_r1_package PRIVATE
    ${CMAKE_CURRENT_LIST_DIR})

  set(_prefab_r1_package "${CMAKE_CURRENT_BINARY_DIR}/release_lifecycle/r1/supply.bin")
  file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/release_lifecycle/r1")
  add_custom_command(
    OUTPUT "${_prefab_r1_package}"
    COMMAND apptraverse_export_prefab_release_r1_package "${_prefab_r1_package}"
    DEPENDS apptraverse_export_prefab_release_r1_package
    VERBATIM
    COMMENT "Generate prefab R1 supply.bin")
  add_custom_target(apptraverse_prefab_r1_supply_bin ALL
    DEPENDS "${_prefab_r1_package}")

  add_executable(apptraverse_export_prefab_release_r2_package
    export_prefab_release_r2_package.cpp
    prefab_release_model.cpp
  )
  target_compile_definitions(apptraverse_export_prefab_release_r2_package PRIVATE
    APPTRAVERSE_PREFAB_EXE_REVISION=2)
  target_link_libraries(apptraverse_export_prefab_release_r2_package PRIVATE apptraverse)
  target_include_directories(apptraverse_export_prefab_release_r2_package PRIVATE
    ${CMAKE_CURRENT_LIST_DIR})

  add_test(
    NAME apptraverse_prefab_active_release_matrix
    COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_LIST_DIR}/prefab_active_release_matrix.py"
      --r0 "$<TARGET_FILE:apptraverse_prefab_release_r0>"
      --r2 "$<TARGET_FILE:apptraverse_prefab_release_r2>"
      --r1-package-tool "$<TARGET_FILE:apptraverse_export_prefab_release_r1_package>"
      --r2-package-tool "$<TARGET_FILE:apptraverse_export_prefab_release_r2_package>"
      --work-root "${CMAKE_CURRENT_BINARY_DIR}/active_release_states"
  )
  set_tests_properties(apptraverse_prefab_active_release_matrix PROPERTIES
    TIMEOUT 120
    LABELS "serialization;prefab;evolution;lifecycle;active-release"
  )

  add_executable(apptraverse_prefab_release_r1_supply_equivalence_test
    prefab_release_r1_supply_equivalence_test.cpp
  )
  target_link_libraries(apptraverse_prefab_release_r1_supply_equivalence_test
    PRIVATE apptraverse)
  target_include_directories(apptraverse_prefab_release_r1_supply_equivalence_test
    PRIVATE ${CMAKE_CURRENT_LIST_DIR})
  apptraverse_embed_distilled_supply(
    apptraverse_prefab_release_r1_supply_equivalence_test
    apptraverse_export_prefab_release_r0_backing
    embedded_distilled_prefab_release_r0_supply.h)
  apptraverse_embed_distilled_supply(
    apptraverse_prefab_release_r1_supply_equivalence_test
    apptraverse_export_prefab_release_r1_backing
    embedded_distilled_prefab_release_r1_supply.h)
  add_dependencies(apptraverse_prefab_release_r1_supply_equivalence_test
    apptraverse_prefab_r1_supply_bin)
  add_test(
    NAME apptraverse_prefab_release_r1_supply_equivalence_test
    COMMAND apptraverse_prefab_release_r1_supply_equivalence_test
      "${_prefab_r1_package}")
  set_tests_properties(apptraverse_prefab_release_r1_supply_equivalence_test
    PROPERTIES TIMEOUT 60 LABELS "serialization;prefab;evolution;equivalence")

  add_test(
    NAME apptraverse_prefab_release_r1_user_equivalence_matrix
    COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_LIST_DIR}/prefab_release_r1_user_equivalence_matrix.py"
      --r0 "$<TARGET_FILE:apptraverse_prefab_release_r0>"
      --r1 "$<TARGET_FILE:apptraverse_prefab_release_r1>"
      --package-tool "$<TARGET_FILE:apptraverse_export_prefab_release_r1_package>"
      --work-root "${CMAKE_CURRENT_BINARY_DIR}/r1_user_equivalence_states"
  )
  set_tests_properties(apptraverse_prefab_release_r1_user_equivalence_matrix
    PROPERTIES TIMEOUT 120
    LABELS "serialization;prefab;evolution;equivalence;lifecycle")

  add_test(
    NAME apptraverse_prefab_dynamic_layer_roundtrip_matrix
    COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_LIST_DIR}/prefab_dynamic_layer_roundtrip_matrix.py"
      --r0 "$<TARGET_FILE:apptraverse_prefab_release_r0>"
      --r1 "$<TARGET_FILE:apptraverse_prefab_release_r1>"
      --r2 "$<TARGET_FILE:apptraverse_prefab_release_r2>"
      --package-tool "$<TARGET_FILE:apptraverse_export_prefab_release_r1_package>"
      --work-root "${CMAKE_CURRENT_BINARY_DIR}/dynamic_layer_roundtrip_states"
  )
  set_tests_properties(apptraverse_prefab_dynamic_layer_roundtrip_matrix
    PROPERTIES TIMEOUT 120
    LABELS "serialization;prefab;evolution;lifecycle;roundtrip"
  )

  add_test(
    NAME apptraverse_prefab_late_instance_matrix
    COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_LIST_DIR}/prefab_late_instance_matrix.py"
      --r0 "$<TARGET_FILE:apptraverse_prefab_release_r0>"
      --r1 "$<TARGET_FILE:apptraverse_prefab_release_r1>"
      --r2 "$<TARGET_FILE:apptraverse_prefab_release_r2>"
      --work-root "${CMAKE_CURRENT_BINARY_DIR}/late_instance_states"
  )
  set_tests_properties(apptraverse_prefab_late_instance_matrix
    PROPERTIES TIMEOUT 120
    LABELS "serialization;prefab;evolution;lifecycle;late-instance"
  )

  add_test(
    NAME apptraverse_prefab_compaction_roundtrip_matrix
    COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_LIST_DIR}/prefab_compaction_roundtrip_matrix.py"
      --r0 "$<TARGET_FILE:apptraverse_prefab_release_r0>"
      --r1 "$<TARGET_FILE:apptraverse_prefab_release_r1>"
      --r2 "$<TARGET_FILE:apptraverse_prefab_release_r2>"
      --work-root "${CMAKE_CURRENT_BINARY_DIR}/compaction_roundtrip_states"
  )
  set_tests_properties(apptraverse_prefab_compaction_roundtrip_matrix
    PROPERTIES TIMEOUT 120
    LABELS "serialization;prefab;evolution;lifecycle;compaction"
  )

  add_test(
    NAME apptraverse_prefab_old_exe_compaction_guard_matrix
    COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_LIST_DIR}/prefab_old_exe_compaction_guard_matrix.py"
      --r0 "$<TARGET_FILE:apptraverse_prefab_release_r0>"
      --r1 "$<TARGET_FILE:apptraverse_prefab_release_r1>"
      --r2 "$<TARGET_FILE:apptraverse_prefab_release_r2>"
      --work-root "${CMAKE_CURRENT_BINARY_DIR}/old_exe_compaction_guard_states"
  )
  set_tests_properties(apptraverse_prefab_old_exe_compaction_guard_matrix
    PROPERTIES TIMEOUT 120
    LABELS "serialization;prefab;evolution;lifecycle;compaction;guard"
  )

  add_executable(apptraverse_prefab_release_supply_split_test
    prefab_release_supply_split_test.cpp
  )
  target_link_libraries(apptraverse_prefab_release_supply_split_test PRIVATE apptraverse)
  target_include_directories(apptraverse_prefab_release_supply_split_test PRIVATE
    ${CMAKE_CURRENT_LIST_DIR})
  apptraverse_embed_distilled_supply(
    apptraverse_prefab_release_supply_split_test
    apptraverse_export_prefab_release_r0_backing
    embedded_distilled_prefab_release_r0_supply.h)
  apptraverse_embed_distilled_supply(
    apptraverse_prefab_release_supply_split_test
    apptraverse_export_prefab_release_r2_backing
    embedded_distilled_prefab_release_r2_supply.h)
  add_test(NAME apptraverse_prefab_release_supply_split_test
    COMMAND apptraverse_prefab_release_supply_split_test)

  add_executable(apptraverse_prefab_unknown_old prefab_release_unknown_event.cpp)
  add_executable(apptraverse_prefab_unknown_new prefab_release_unknown_event.cpp)
  target_compile_definitions(apptraverse_prefab_unknown_old PRIVATE
    APPTRAVERSE_PREFAB_UNKNOWN_EXE_REVISION=0)
  target_compile_definitions(apptraverse_prefab_unknown_new PRIVATE
    APPTRAVERSE_PREFAB_UNKNOWN_EXE_REVISION=1)
  target_link_libraries(apptraverse_prefab_unknown_old PRIVATE apptraverse)
  target_link_libraries(apptraverse_prefab_unknown_new PRIVATE apptraverse)

  file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/prefab release states")
  add_test(
    NAME apptraverse_prefab_release_matrix
    COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_LIST_DIR}/prefab_release_matrix.py"
      --r0 "$<TARGET_FILE:apptraverse_prefab_release_r0>"
      --r2 "$<TARGET_FILE:apptraverse_prefab_release_r2>"
      --unknown-old "$<TARGET_FILE:apptraverse_prefab_unknown_old>"
      --unknown-new "$<TARGET_FILE:apptraverse_prefab_unknown_new>"
      --work-root "${CMAKE_CURRENT_BINARY_DIR}/prefab release states"
  )
  set_tests_properties(apptraverse_prefab_release_matrix PROPERTIES
    TIMEOUT 120
    LABELS "serialization;prefab;evolution"
  )

  add_test(
    NAME apptraverse_prefab_release_lifecycle_matrix
    COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_LIST_DIR}/prefab_release_lifecycle_matrix.py"
      --r0 "$<TARGET_FILE:apptraverse_prefab_release_r0>"
      --package-tool "$<TARGET_FILE:apptraverse_export_prefab_release_r1_package>"
      --work-root "${CMAKE_CURRENT_BINARY_DIR}/release_lifecycle/states"
  )
  set_tests_properties(apptraverse_prefab_release_lifecycle_matrix PROPERTIES
    TIMEOUT 120
    LABELS "serialization;prefab;evolution;lifecycle"
  )
endif()

if(APPTRAVERSE_BUILD_LEGACY_EXAMPLES)

add_executable(apptraverse_native_object_graph_serialization_test
  native_object_graph_serialization_test.cpp
)

target_link_libraries(apptraverse_native_object_graph_serialization_test PRIVATE
  apptraverse
)

add_test(
  NAME apptraverse_native_object_graph_serialization_test
  COMMAND apptraverse_native_object_graph_serialization_test
)

add_executable(apptraverse_closed_event_graph_test
  closed_event_graph_test.cpp
)

target_link_libraries(apptraverse_closed_event_graph_test PRIVATE
  apptraverse
)

add_test(
  NAME apptraverse_closed_event_graph_test
  COMMAND apptraverse_closed_event_graph_test
)

add_executable(apptraverse_journal_retention_test
  journal_retention_test.cpp
)

target_link_libraries(apptraverse_journal_retention_test PRIVATE apptraverse)

add_test(
  NAME apptraverse_journal_retention_test
  COMMAND apptraverse_journal_retention_test
)

add_executable(apptraverse_model_runtime_stop_test
  model_runtime_stop_test.cpp
)

target_link_libraries(apptraverse_model_runtime_stop_test PRIVATE apptraverse)

add_test(
  NAME apptraverse_model_runtime_stop_test
  COMMAND apptraverse_model_runtime_stop_test
)


add_executable(apptraverse_publication_channel_test
  publication_channel_test.cpp
)

target_include_directories(apptraverse_publication_channel_test PRIVATE
  ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(apptraverse_publication_channel_test PRIVATE
  apptraverse_compile_policy
)

add_test(
  NAME apptraverse_publication_channel_test
  COMMAND apptraverse_publication_channel_test
)


endif() # APPTRAVERSE_BUILD_LEGACY_EXAMPLES

if(WIN32)
  add_executable(apptraverse_surfaces_win32_library_consumer_test
    surfaces_win32_library_consumer_test.cpp
    surfaces_win32_library_consumer_presentation.cpp
  )
  target_link_libraries(apptraverse_surfaces_win32_library_consumer_test PRIVATE
    apptraverse_surfaces_windows
    user32
    gdi32
  )
  target_include_directories(apptraverse_surfaces_win32_library_consumer_test PRIVATE
    ${CMAKE_SOURCE_DIR}/include
  )
  if(MSVC)
    target_compile_options(apptraverse_surfaces_win32_library_consumer_test PRIVATE /utf-8)
  endif()
  add_test(
    NAME apptraverse_surfaces_win32_library_consumer_test
    COMMAND apptraverse_surfaces_win32_library_consumer_test
  )

  if(APPTRAVERSE_BUILD_LEGACY_EXAMPLES)
  add_executable(apptraverse_surfaces_win32_smoke_test
    surfaces_win32_smoke_test.cpp
    ${CMAKE_SOURCE_DIR}/examples/surfaces_demo/windows/win_app.cpp
    ${CMAKE_SOURCE_DIR}/examples/surfaces_demo/common/surfaces_lifecycle.cpp
    ${CMAKE_SOURCE_DIR}/examples/surfaces_demo/common/surfaces_shutdown_trace.cpp
  )
  target_compile_definitions(apptraverse_surfaces_win32_smoke_test PRIVATE
    APPTRAVERSE_ENABLE_DISTILLATION
    "APPTRAVERSE_SURFACES_ACCEPTANCE_DIR=\"${CMAKE_SOURCE_DIR}/docs/surfaces_demo_acceptance\""
    "APPTRAVERSE_SURFACES_LOAD_ONLY_EXE=R\"($<TARGET_FILE:win32_surfaces_demo_load_only>)\""
  )
  add_dependencies(apptraverse_surfaces_win32_smoke_test
    win32_surfaces_demo_load_only)
  target_link_libraries(apptraverse_surfaces_win32_smoke_test PRIVATE
    surfaces_demo_distill
    surfaces_demo_win32
    apptraverse_surfaces_windows
    user32
    gdi32
  )
  target_include_directories(apptraverse_surfaces_win32_smoke_test PRIVATE
    ${CMAKE_SOURCE_DIR}/examples/surfaces_demo/common
    ${CMAKE_SOURCE_DIR}/examples/surfaces_demo/windows
    ${CMAKE_SOURCE_DIR}/examples/main_window_runtime_demo/windows
  )
  target_link_libraries(apptraverse_surfaces_win32_smoke_test PRIVATE
    apptraverse_ui_windows
  )
  if(MSVC)
    target_compile_options(apptraverse_surfaces_win32_smoke_test PRIVATE /utf-8)
  endif()
  add_test(
    NAME apptraverse_surfaces_win32_smoke_test
    COMMAND apptraverse_surfaces_win32_smoke_test
  )
  add_custom_target(apptraverse_surfaces_win32_smoke_check
    COMMAND $<TARGET_FILE:apptraverse_surfaces_win32_smoke_test>
    DEPENDS apptraverse_surfaces_win32_smoke_test
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Surfaces Windows multi-window smoke"
  )
  endif() # APPTRAVERSE_BUILD_LEGACY_EXAMPLES
endif()

if(APPTRAVERSE_BUILD_LEGACY_EXAMPLES AND APPTRAVERSE_BUILD_LINUX_GUI AND UNIX AND NOT APPLE AND NOT ANDROID AND NOT EMSCRIPTEN)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(GTK3 REQUIRED IMPORTED_TARGET gtk+-3.0)
  add_executable(apptraverse_surfaces_linux_smoke_test
    surfaces_linux_smoke_test.cpp
    ${CMAKE_SOURCE_DIR}/examples/surfaces_demo/linux/linux_app.cpp
    ${CMAKE_SOURCE_DIR}/examples/surfaces_demo/linux/linux_presenters.cpp
    ${CMAKE_SOURCE_DIR}/examples/surfaces_demo/linux/linux_fatal.cpp
    ${CMAKE_SOURCE_DIR}/examples/surfaces_demo/common/surfaces_lifecycle.cpp
    ${CMAKE_SOURCE_DIR}/examples/surfaces_demo/common/surfaces_shutdown_trace.cpp
  )
  target_compile_definitions(apptraverse_surfaces_linux_smoke_test PRIVATE
    APPTRAVERSE_ENABLE_DISTILLATION
  )
  target_link_libraries(apptraverse_surfaces_linux_smoke_test PRIVATE
    surfaces_demo_distill
    apptraverse_surfaces
    PkgConfig::GTK3
  )
  target_include_directories(apptraverse_surfaces_linux_smoke_test PRIVATE
    ${CMAKE_SOURCE_DIR}/examples/surfaces_demo/common
    ${CMAKE_SOURCE_DIR}/examples/surfaces_demo/linux
  )
  add_test(
    NAME apptraverse_surfaces_linux_smoke_test
    COMMAND apptraverse_surfaces_linux_smoke_test
  )
  add_custom_target(apptraverse_surfaces_linux_smoke_check
    COMMAND $<TARGET_FILE:apptraverse_surfaces_linux_smoke_test>
    DEPENDS apptraverse_surfaces_linux_smoke_test
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Surfaces Linux GTK3 multi-window smoke"
  )
endif()

if(APPTRAVERSE_BUILD_LEGACY_EXAMPLES AND APPLE AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
  enable_language(OBJCXX)
  set_source_files_properties(
    surfaces_macos_smoke_test.mm
    ${CMAKE_SOURCE_DIR}/examples/surfaces_demo/macos/mac_app.mm
    ${CMAKE_SOURCE_DIR}/examples/surfaces_demo/macos/mac_presenters.mm
    PROPERTIES COMPILE_FLAGS "-fobjc-arc"
  )
  add_executable(apptraverse_surfaces_macos_smoke_test
    surfaces_macos_smoke_test.mm
    ${CMAKE_SOURCE_DIR}/examples/surfaces_demo/macos/mac_app.mm
    ${CMAKE_SOURCE_DIR}/examples/surfaces_demo/macos/mac_presenters.mm
    ${CMAKE_SOURCE_DIR}/examples/surfaces_demo/common/surfaces_lifecycle.cpp
    ${CMAKE_SOURCE_DIR}/examples/surfaces_demo/common/surfaces_shutdown_trace.cpp
  )
  target_compile_definitions(apptraverse_surfaces_macos_smoke_test PRIVATE
    APPTRAVERSE_ENABLE_DISTILLATION
  )
  target_link_libraries(apptraverse_surfaces_macos_smoke_test PRIVATE
    surfaces_demo_distill
    apptraverse_surfaces
    surfaces_demo_macos_content
    "-framework AppKit"
    "-framework Foundation"
  )
  target_include_directories(apptraverse_surfaces_macos_smoke_test PRIVATE
    ${CMAKE_SOURCE_DIR}/examples/surfaces_demo/common
    ${CMAKE_SOURCE_DIR}/examples/surfaces_demo/macos
  )
  target_compile_options(apptraverse_surfaces_macos_smoke_test PRIVATE
    $<$<COMPILE_LANGUAGE:OBJCXX>:-fno-rtti>
  )
  # Same as the macOS host: clang++ links, Swift runtime via autolink entries.
  set_target_properties(apptraverse_surfaces_macos_smoke_test PROPERTIES
    LINKER_LANGUAGE OBJCXX
  )
  target_link_options(apptraverse_surfaces_macos_smoke_test PRIVATE
    -L${CMAKE_OSX_SYSROOT}/usr/lib/swift
    -L/usr/lib/swift
    -Wl,-rpath,/usr/lib/swift
  )
  add_test(
    NAME apptraverse_surfaces_macos_smoke_test
    COMMAND apptraverse_surfaces_macos_smoke_test
  )
  add_custom_target(apptraverse_surfaces_macos_smoke_check
    COMMAND $<TARGET_FILE:apptraverse_surfaces_macos_smoke_test>
    DEPENDS apptraverse_surfaces_macos_smoke_test
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Surfaces macOS multi-window smoke"
  )
endif()

add_executable(apptraverse_presenter_load_order_test
  presenter_load_order_test.cpp
)

target_link_libraries(apptraverse_presenter_load_order_test PRIVATE apptraverse)

add_test(
  NAME apptraverse_presenter_load_order_test
  COMMAND apptraverse_presenter_load_order_test
)

if(APPTRAVERSE_BUILD_LEGACY_EXAMPLES)
add_custom_target(apptraverse_dynamic_objects_headless_check
  COMMAND $<TARGET_FILE:apptraverse_dynamic_objects_add_test>
  COMMAND $<TARGET_FILE:apptraverse_publication_channel_test>
  COMMAND $<TARGET_FILE:apptraverse_presenter_load_order_test>
  DEPENDS
    apptraverse_dynamic_objects_add_test
    apptraverse_publication_channel_test
    apptraverse_presenter_load_order_test
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
  COMMENT "Dynamic objects Add Item headless checks"
)
endif()

if(WIN32)
  if(APPTRAVERSE_BUILD_LEGACY_EXAMPLES)
  # Windows CreateProcess parent. Debug binary (not NDEBUG): aether-objects
  # Factory layout is #ifndef NDEBUG. Missing Application is still abort().
  add_executable(apptraverse_main_window_missing_load_test
    main_window_missing_load_test.cpp
    ${CMAKE_SOURCE_DIR}/examples/main_window_runtime_demo/common/main_window_lifecycle.cpp
  )
  target_link_libraries(apptraverse_main_window_missing_load_test PRIVATE
    main_window_runtime_demo_model
  )
  add_test(
    NAME apptraverse_main_window_missing_load_test
    COMMAND apptraverse_main_window_missing_load_test
  )

  add_custom_command(TARGET apptraverse_main_window_headless_check POST_BUILD
    COMMAND $<TARGET_FILE:apptraverse_main_window_missing_load_test>
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
  )
  add_dependencies(apptraverse_main_window_headless_check
    apptraverse_main_window_missing_load_test
  )
  endif() # APPTRAVERSE_BUILD_LEGACY_EXAMPLES

  # Isolated FatalWin32 + WriteFatalStderr with NDEBUG. No aether-objects.
  add_executable(apptraverse_win32_fatal_ndebug_child
    win32_fatal_ndebug_child.cpp
    ${CMAKE_SOURCE_DIR}/src/noninteractive_crt.cpp
  )
  target_compile_definitions(apptraverse_win32_fatal_ndebug_child PRIVATE
    NDEBUG
  )
  target_include_directories(apptraverse_win32_fatal_ndebug_child PRIVATE
    ${CMAKE_SOURCE_DIR}/include
  )
  target_link_libraries(apptraverse_win32_fatal_ndebug_child PRIVATE
    apptraverse_ui_windows
    apptraverse_compile_policy
  )

  add_executable(apptraverse_win32_fatal_ndebug_test
    win32_fatal_ndebug_test.cpp
  )
  target_compile_definitions(apptraverse_win32_fatal_ndebug_test PRIVATE
    "WIN32_FATAL_NDEBUG_CHILD=R\"($<TARGET_FILE:apptraverse_win32_fatal_ndebug_child>)\""
  )
  target_link_libraries(apptraverse_win32_fatal_ndebug_test PRIVATE
    apptraverse_compile_policy
  )
  add_dependencies(apptraverse_win32_fatal_ndebug_test
    apptraverse_win32_fatal_ndebug_child
  )
  add_test(
    NAME apptraverse_win32_fatal_ndebug_test
    COMMAND apptraverse_win32_fatal_ndebug_test
  )

  add_custom_target(apptraverse_win32_fatal_ndebug_check
    COMMAND $<TARGET_FILE:apptraverse_win32_fatal_ndebug_test>
    DEPENDS apptraverse_win32_fatal_ndebug_test
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Isolated FatalWin32 NDEBUG helper check"
  )

  add_executable(apptraverse_win_ui_native_push_button_fatal_child
    win_ui_native_push_button_fatal_child.cpp
    surfaces_win32_library_consumer_presentation.cpp
    ${CMAKE_SOURCE_DIR}/src/noninteractive_crt.cpp
  )
  target_include_directories(apptraverse_win_ui_native_push_button_fatal_child
    PRIVATE ${CMAKE_SOURCE_DIR}/include)
  target_link_libraries(apptraverse_win_ui_native_push_button_fatal_child PRIVATE
    apptraverse
    apptraverse_surfaces_windows
    apptraverse_ui_windows
    apptraverse_compile_policy
  )

  add_executable(apptraverse_win_ui_native_push_button_fatal_test
    win_ui_native_push_button_fatal_test.cpp
  )
  target_compile_definitions(apptraverse_win_ui_native_push_button_fatal_test
    PRIVATE
    "WIN_UI_NATIVE_PUSH_BUTTON_FATAL_CHILD=R\"($<TARGET_FILE:apptraverse_win_ui_native_push_button_fatal_child>)\""
  )
  target_link_libraries(apptraverse_win_ui_native_push_button_fatal_test PRIVATE
    apptraverse_compile_policy
  )
  add_dependencies(apptraverse_win_ui_native_push_button_fatal_test
    apptraverse_win_ui_native_push_button_fatal_child
  )
  add_test(
    NAME apptraverse_win_ui_native_push_button_fatal_test
    COMMAND apptraverse_win_ui_native_push_button_fatal_test
  )

  add_executable(apptraverse_win_ui_native_push_button_test
    win_ui_native_push_button_test.cpp
  )
  target_link_libraries(apptraverse_win_ui_native_push_button_test PRIVATE
    apptraverse
    apptraverse_ui_windows
  )
  target_include_directories(apptraverse_win_ui_native_push_button_test PRIVATE
    ${CMAKE_SOURCE_DIR}/include
  )
  add_test(
    NAME apptraverse_win_ui_native_push_button_test
    COMMAND apptraverse_win_ui_native_push_button_test
  )

  if(APPTRAVERSE_BUILD_LEGACY_EXAMPLES)
  add_executable(apptraverse_main_window_win32_smoke_test
    main_window_win32_smoke_test.cpp
    ${CMAKE_SOURCE_DIR}/examples/main_window_runtime_demo/windows/win_app.cpp
    ${CMAKE_SOURCE_DIR}/examples/main_window_runtime_demo/windows/win_presenters.cpp
    ${CMAKE_SOURCE_DIR}/examples/main_window_runtime_demo/common/main_window_lifecycle.cpp
  )

  target_compile_definitions(apptraverse_main_window_win32_smoke_test PRIVATE
    APPTRAVERSE_ENABLE_DISTILLATION
    "WIN32_MAIN_WINDOW_DEMO_EXE=R\"($<TARGET_FILE:win32_main_window_runtime_demo>)\""
    "WIN32_MAIN_WINDOW_LOAD_ONLY_EXE=R\"($<TARGET_FILE:win32_main_window_runtime_demo_load_only>)\""
  )

  target_link_libraries(apptraverse_main_window_win32_smoke_test PRIVATE
    main_window_runtime_demo_model
    apptraverse_ui_windows
    user32
    gdi32
  )

  target_include_directories(apptraverse_main_window_win32_smoke_test PRIVATE
    ${CMAKE_SOURCE_DIR}/examples/main_window_runtime_demo/common
    ${CMAKE_SOURCE_DIR}/examples/main_window_runtime_demo/windows
  )

  add_dependencies(apptraverse_main_window_win32_smoke_test
    win32_main_window_runtime_demo
    win32_main_window_runtime_demo_load_only
  )

  if(MSVC)
    target_compile_options(apptraverse_main_window_win32_smoke_test PRIVATE /utf-8)
  endif()

  add_test(
    NAME apptraverse_main_window_win32_smoke_test
    COMMAND apptraverse_main_window_win32_smoke_test
  )

  add_custom_target(apptraverse_main_window_win32_smoke_check
    COMMAND $<TARGET_FILE:apptraverse_main_window_win32_smoke_test>
    DEPENDS apptraverse_main_window_win32_smoke_test
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Main-window Loading/main HWND smoke"
  )

  add_executable(apptraverse_dynamic_objects_win32_smoke_test
    dynamic_objects_win32_smoke_test.cpp
    ${CMAKE_SOURCE_DIR}/examples/dynamic_objects_demo/windows/win_app.cpp
    ${CMAKE_SOURCE_DIR}/examples/dynamic_objects_demo/windows/win_presenters.cpp
    ${CMAKE_SOURCE_DIR}/examples/dynamic_objects_demo/common/dynamic_lifecycle.cpp
  )

  target_compile_definitions(apptraverse_dynamic_objects_win32_smoke_test PRIVATE
    APPTRAVERSE_ENABLE_DISTILLATION
    "WIN32_DYNAMIC_OBJECTS_DEMO_EXE=R\"($<TARGET_FILE:win32_dynamic_objects_demo>)\""
    "WIN32_DYNAMIC_OBJECTS_LOAD_ONLY_EXE=R\"($<TARGET_FILE:win32_dynamic_objects_demo_load_only>)\""
  )

  target_link_libraries(apptraverse_dynamic_objects_win32_smoke_test PRIVATE
    dynamic_objects_demo_distill
    apptraverse_ui_windows
    user32
    gdi32
  )

  target_include_directories(apptraverse_dynamic_objects_win32_smoke_test PRIVATE
    ${CMAKE_SOURCE_DIR}/examples/dynamic_objects_demo/common
    ${CMAKE_SOURCE_DIR}/examples/dynamic_objects_demo/windows
    ${CMAKE_SOURCE_DIR}/examples/main_window_runtime_demo/windows
  )

  add_dependencies(apptraverse_dynamic_objects_win32_smoke_test
    win32_dynamic_objects_demo
    win32_dynamic_objects_demo_load_only
  )

  if(MSVC)
    target_compile_options(apptraverse_dynamic_objects_win32_smoke_test PRIVATE /utf-8)
  endif()

  add_test(
    NAME apptraverse_dynamic_objects_win32_smoke_test
    COMMAND apptraverse_dynamic_objects_win32_smoke_test
  )

  add_custom_target(apptraverse_dynamic_objects_win32_smoke_check
    COMMAND $<TARGET_FILE:apptraverse_dynamic_objects_win32_smoke_test>
    DEPENDS apptraverse_dynamic_objects_win32_smoke_test
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Dynamic objects Add Item Win32 smoke"
  )

  add_executable(apptraverse_dynamic_two_main_win32_test
    dynamic_two_main_win32_test.cpp
    ${CMAKE_SOURCE_DIR}/examples/dynamic_objects_demo/windows/win_presenters.cpp
  )
  target_link_libraries(apptraverse_dynamic_two_main_win32_test PRIVATE
    dynamic_objects_demo_model
    apptraverse_ui_windows
    user32
    gdi32
  )
  target_include_directories(apptraverse_dynamic_two_main_win32_test PRIVATE
    ${CMAKE_SOURCE_DIR}/examples/dynamic_objects_demo/common
    ${CMAKE_SOURCE_DIR}/examples/dynamic_objects_demo/windows
    ${CMAKE_SOURCE_DIR}/examples/main_window_runtime_demo/windows
  )
  if(MSVC)
    target_compile_options(apptraverse_dynamic_two_main_win32_test PRIVATE /utf-8)
  endif()
  add_test(
    NAME apptraverse_dynamic_two_main_win32_test
    COMMAND apptraverse_dynamic_two_main_win32_test
  )
  endif() # APPTRAVERSE_BUILD_LEGACY_EXAMPLES
endif()


add_executable(apptraverse_partitioned_model_domain_storage_test
  partitioned_model_domain_storage_test.cpp
)
target_link_libraries(apptraverse_partitioned_model_domain_storage_test PRIVATE
  apptraverse
)
add_test(
  NAME apptraverse_partitioned_model_domain_storage_test
  COMMAND apptraverse_partitioned_model_domain_storage_test
)

add_executable(apptraverse_model_persistence_runtime_ref_test
  model_persistence_runtime_ref_test.cpp
)
target_link_libraries(apptraverse_model_persistence_runtime_ref_test PRIVATE
  apptraverse
)
add_test(
  NAME apptraverse_model_persistence_runtime_ref_test
  COMMAND apptraverse_model_persistence_runtime_ref_test
)

add_executable(apptraverse_checkpoint_load_rehydration_test
  checkpoint_load_rehydration_test.cpp
)
target_link_libraries(apptraverse_checkpoint_load_rehydration_test PRIVATE
  apptraverse
)
add_test(
  NAME apptraverse_checkpoint_load_rehydration_test
  COMMAND apptraverse_checkpoint_load_rehydration_test
)

add_executable(apptraverse_ui_bitmap_publication_test
  ui_bitmap_publication_test.cpp
)
target_link_libraries(apptraverse_ui_bitmap_publication_test PRIVATE
  apptraverse
  apptraverse_ui_extended
)
add_test(
  NAME apptraverse_ui_bitmap_publication_test
  COMMAND apptraverse_ui_bitmap_publication_test
)

add_executable(apptraverse_ui_bitmap_structural_publication_test
  ui_bitmap_structural_publication_test.cpp
)
target_link_libraries(apptraverse_ui_bitmap_structural_publication_test PRIVATE
  apptraverse
  apptraverse_ui_extended
)
add_test(
  NAME apptraverse_ui_bitmap_structural_publication_test
  COMMAND apptraverse_ui_bitmap_structural_publication_test
)

add_executable(apptraverse_checkpoint_load_cycle_persistence_test
  checkpoint_load_cycle_persistence_test.cpp
)
target_link_libraries(apptraverse_checkpoint_load_cycle_persistence_test PRIVATE
  apptraverse
)
add_test(
  NAME apptraverse_checkpoint_load_cycle_persistence_test
  COMMAND apptraverse_checkpoint_load_cycle_persistence_test
)

add_executable(apptraverse_presenter_graph_persistence_test
  presenter_graph_persistence_test.cpp
)
target_link_libraries(apptraverse_presenter_graph_persistence_test PRIVATE
  apptraverse
  apptraverse_ui_extended
)
add_test(
  NAME apptraverse_presenter_graph_persistence_test
  COMMAND apptraverse_presenter_graph_persistence_test
)

if(WIN32)
  add_executable(apptraverse_presenter_win_hwnd_persistence_test
    presenter_win_hwnd_persistence_test.cpp
  )
  target_link_libraries(apptraverse_presenter_win_hwnd_persistence_test PRIVATE
    apptraverse
    apptraverse_ui_windows
  )
  add_test(
    NAME apptraverse_presenter_win_hwnd_persistence_test
    COMMAND apptraverse_presenter_win_hwnd_persistence_test
  )

  target_compile_definitions(apptraverse_ui_windows PUBLIC
    APPTRAVERSE_WIN_DESKTOP_FRAME_SYNC_TEST=1)
  add_executable(apptraverse_presenter_win_desktop_frame_sync_test
    presenter_win_desktop_frame_sync_test.cpp
  )
  target_compile_definitions(apptraverse_presenter_win_desktop_frame_sync_test PRIVATE
    APPTRAVERSE_WIN_DESKTOP_FRAME_SYNC_TEST=1)
  target_link_libraries(apptraverse_presenter_win_desktop_frame_sync_test PRIVATE
    apptraverse
    apptraverse_ui_windows
  )
  add_test(
    NAME apptraverse_presenter_win_desktop_frame_sync_test
    COMMAND apptraverse_presenter_win_desktop_frame_sync_test
  )

  add_executable(apptraverse_presenter_win_desktop_interactive_drag_e2e_test
    presenter_win_desktop_interactive_drag_e2e_test.cpp
  )
  target_compile_definitions(apptraverse_presenter_win_desktop_interactive_drag_e2e_test PRIVATE
    APPTRAVERSE_WIN_DESKTOP_FRAME_SYNC_TEST=1)
  target_link_libraries(apptraverse_presenter_win_desktop_interactive_drag_e2e_test PRIVATE
    apptraverse
    apptraverse_ui_windows
  )
  add_test(
    NAME apptraverse_presenter_win_desktop_interactive_drag_e2e_test
    COMMAND apptraverse_presenter_win_desktop_interactive_drag_e2e_test
  )

  add_executable(apptraverse_presenter_win_desktop_frame_publication_e2e_test
    presenter_win_desktop_frame_publication_e2e_test.cpp
  )
  target_compile_definitions(apptraverse_presenter_win_desktop_frame_publication_e2e_test PRIVATE
    APPTRAVERSE_WIN_DESKTOP_FRAME_SYNC_TEST=1)
  target_link_libraries(apptraverse_presenter_win_desktop_frame_publication_e2e_test PRIVATE
    apptraverse
    apptraverse_ui_windows
  )
  add_test(
    NAME apptraverse_presenter_win_desktop_frame_publication_e2e_test
    COMMAND apptraverse_presenter_win_desktop_frame_publication_e2e_test
  )
endif()

add_executable(apptraverse_presenter_platform_persist_save
  presenter_platform_persist_save.cpp
)
add_executable(apptraverse_presenter_platform_persist_load
  presenter_platform_persist_load.cpp
)
target_link_libraries(apptraverse_presenter_platform_persist_save PRIVATE
  apptraverse
)
target_link_libraries(apptraverse_presenter_platform_persist_load PRIVATE
  apptraverse
)
add_test(
  NAME apptraverse_presenter_platform_persist_roundtrip
  COMMAND ${CMAKE_COMMAND}
    -DSNAPSHOT=${CMAKE_CURRENT_BINARY_DIR}/presenter_platform_persist.bin
    -DSAVE_EXE=$<TARGET_FILE:apptraverse_presenter_platform_persist_save>
    -DLOAD_EXE=$<TARGET_FILE:apptraverse_presenter_platform_persist_load>
    -P ${CMAKE_CURRENT_LIST_DIR}/presenter_platform_persist_run.cmake
)

add_executable(apptraverse_object_origin_facet_test
  object_origin_facet_test.cpp
)
target_link_libraries(apptraverse_object_origin_facet_test PRIVATE apptraverse)
add_test(
  NAME apptraverse_object_origin_facet_test
  COMMAND apptraverse_object_origin_facet_test
)

add_executable(apptraverse_object_clone_test object_clone_test.cpp)
target_link_libraries(apptraverse_object_clone_test PRIVATE apptraverse)
add_test(
  NAME apptraverse_object_clone_test
  COMMAND apptraverse_object_clone_test
)

add_test(
  NAME apptraverse_object_origin_disk_roundtrip_test
  COMMAND ${CMAKE_COMMAND}
    -DEXE=$<TARGET_FILE:apptraverse_object_origin_facet_test>
    -DSTATE_ROOT=${CMAKE_CURRENT_BINARY_DIR}/object_origin_disk_roundtrip_state
    -P ${CMAKE_CURRENT_LIST_DIR}/object_origin_disk_roundtrip_run.cmake
)
set_tests_properties(apptraverse_object_origin_disk_roundtrip_test
  PROPERTIES TIMEOUT 120
  LABELS "serialization;origin;disk;lifecycle"
)

add_executable(apptraverse_object_origin_disk_fixture
  object_origin_disk_fixture.cpp
)
target_link_libraries(apptraverse_object_origin_disk_fixture PRIVATE apptraverse)

if(NOT Python3_EXECUTABLE)
  find_package(Python3 3.9 REQUIRED COMPONENTS Interpreter)
endif()

add_test(
  NAME apptraverse_object_origin_disk_matrix
  COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_LIST_DIR}/object_origin_disk_matrix.py"
    --fixture "$<TARGET_FILE:apptraverse_object_origin_disk_fixture>"
    --work-root "${CMAKE_CURRENT_BINARY_DIR}/object_origin_disk_states"
)
set_tests_properties(apptraverse_object_origin_disk_matrix
  PROPERTIES TIMEOUT 120
  LABELS "serialization;origin;disk;lifecycle"
)

add_executable(apptraverse_model_persistence_test
  model_persistence_test.cpp
)
target_link_libraries(apptraverse_model_persistence_test PRIVATE
  apptraverse
)
add_test(
  NAME apptraverse_model_persistence_test
  COMMAND apptraverse_model_persistence_test
)

add_executable(apptraverse_directory_domain_storage_test
  directory_domain_storage_test.cpp
)
target_link_libraries(apptraverse_directory_domain_storage_test PRIVATE apptraverse)
add_test(
  NAME apptraverse_directory_domain_storage_test
  COMMAND apptraverse_directory_domain_storage_test
)

add_executable(apptraverse_overlay_domain_storage_test
  overlay_domain_storage_test.cpp
)
target_link_libraries(apptraverse_overlay_domain_storage_test PRIVATE apptraverse)
add_test(
  NAME apptraverse_overlay_domain_storage_test
  COMMAND apptraverse_overlay_domain_storage_test
)

# WindowCatalog / MinimalApplication tests and their reusable UI.
# apptraverse_ui_desktop_window_geometry_test,
# apptraverse_win_ui_native_push_button_test and
# apptraverse_presenter_win_desktop_frame_sync_test are defined above.
function(apptraverse_add_test name source)
  add_executable(${name} ${source})
  target_link_libraries(${name} PRIVATE apptraverse ${ARGN})
  add_test(NAME ${name} COMMAND ${name})
endfunction()

apptraverse_add_test(apptraverse_application_contract_headless_test
  application_contract_headless_test.cpp)

apptraverse_add_test(apptraverse_embedded_distilled_domain_test
  embedded_distilled_domain_test.cpp)

apptraverse_add_test(apptraverse_overlay_user_backing_test
  overlay_user_backing_test.cpp)

apptraverse_add_test(apptraverse_historical_base_materialization_test
  historical_base_materialization_test.cpp)

if(WIN32 AND APPTRAVERSE_BUILD_EXAMPLES)
  include(${CMAKE_SOURCE_DIR}/cmake/apptraverse_embed_distilled_supply.cmake)
  add_executable(apptraverse_minimal_app_embedded_export_test
    minimal_app_embedded_export_test.cpp)
  target_link_libraries(apptraverse_minimal_app_embedded_export_test PRIVATE
    minimal_app_common apptraverse apptraverse_ui_windows)
  target_include_directories(apptraverse_minimal_app_embedded_export_test PRIVATE
    ${CMAKE_SOURCE_DIR}/examples/minimal_app/common
    ${CMAKE_SOURCE_DIR}/examples/minimal_app/windows)
  if(MSVC)
    target_compile_options(apptraverse_minimal_app_embedded_export_test PRIVATE
      /utf-8)
  endif()
  set(_minimal_embed_graph_sources
    ${CMAKE_SOURCE_DIR}/examples/minimal_app/windows/minimal_app_graph.cpp
    ${CMAKE_SOURCE_DIR}/examples/minimal_app/windows/minimal_app_export.cpp
    ${CMAKE_SOURCE_DIR}/examples/minimal_app/common/minimal_application.cpp)
  apptraverse_embed_distilled_supply(
    apptraverse_minimal_app_embedded_export_test
    apptraverse_export_minimal_distilled_graph
    embedded_distilled_minimal_app_supply.h
    ${_minimal_embed_graph_sources})
  add_dependencies(apptraverse_minimal_app_embedded_export_test
    apptraverse_export_minimal_distilled_graph)
  add_test(NAME apptraverse_minimal_app_embedded_export_test
    COMMAND apptraverse_minimal_app_embedded_export_test)
endif()

apptraverse_add_test(apptraverse_application_model_runtime_test
  application_model_runtime_test.cpp)

apptraverse_add_test(apptraverse_application_ui_session_test
  application_ui_session_test.cpp)

if(WIN32)
  apptraverse_add_test(apptraverse_win_application_loading_window_test
    win_application_loading_window_test.cpp apptraverse_ui_windows)
  apptraverse_add_test(apptraverse_win_application_host_test
    win_application_host_test.cpp apptraverse_ui_windows)
endif()

if(WIN32 AND APPTRAVERSE_BUILD_EXAMPLES)
  foreach(name apptraverse_window_catalog_model_test
               apptraverse_window_catalog_win32_acceptance_test)
    string(REPLACE "apptraverse_" "" source ${name})
    apptraverse_add_test(${name} ${source}.cpp surfaces_demo_window_catalog)
    if(MSVC)
      target_compile_options(${name} PRIVATE /utf-8)
    endif()
    set_tests_properties(${name} PROPERTIES LABELS "window_catalog")
  endforeach()
  target_compile_definitions(apptraverse_window_catalog_model_test PRIVATE
    APPTRAVERSE_ENABLE_DISTILLATION)
  add_dependencies(apptraverse_window_catalog_win32_acceptance_test
    win32_window_catalog_demo win32_window_catalog_demo_load_only)

  apptraverse_add_test(apptraverse_minimal_app_win32_acceptance_test
    minimal_app_win32_acceptance_test.cpp minimal_app_common)
  if(MSVC)
    target_compile_options(apptraverse_minimal_app_win32_acceptance_test PRIVATE
      /utf-8)
  endif()
  set_tests_properties(apptraverse_minimal_app_win32_acceptance_test PROPERTIES
    LABELS "minimal_app")
  add_dependencies(apptraverse_minimal_app_win32_acceptance_test
    win32_minimal_app win32_minimal_app_load_only)

  apptraverse_add_test(apptraverse_win_application_message_host_test
    win_application_message_host_test.cpp apptraverse_ui_windows)
  apptraverse_add_test(apptraverse_win_ui_command_routing_test
    win_ui_command_routing_test.cpp apptraverse_ui_windows_extended)
endif()