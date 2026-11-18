# Include from tests/CMakeLists.txt after Apple example composition targets exist.
if(APPLE AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS" AND
   CMAKE_BUILD_TYPE STREQUAL "Debug" AND
   TARGET window_catalog_macos_composition AND
   TARGET window_catalog_macos_composition_load_only)
  foreach(_kind IN ITEMS debug load_only)
    set(_test apptraverse_macos_window_catalog_acceptance_${_kind})
    add_library(${_test}_swift STATIC
      ${CMAKE_CURRENT_SOURCE_DIR}/macos_window_catalog_acceptance.swift)
    set_target_properties(${_test}_swift PROPERTIES
      Swift_MODULE_NAME MacWindowCatalogAcceptance_${_kind})
    target_link_libraries(${_test}_swift PRIVATE apptraverse_ui_macos_swift)
    target_compile_options(${_test}_swift PRIVATE
      "SHELL:-import-objc-header ${CMAKE_CURRENT_SOURCE_DIR}/macos_window_catalog_acceptance.h"
      "SHELL:-Xcc -I${PROJECT_SOURCE_DIR}/examples/window_catalog/macos"
      "SHELL:-Xcc -I${PROJECT_SOURCE_DIR}/include")
    add_executable(${_test}
      ${CMAKE_CURRENT_SOURCE_DIR}/macos_window_catalog_acceptance_link.mm)
    if(_kind STREQUAL "debug")
      set(_composition window_catalog_macos_composition)
    else()
      set(_composition window_catalog_macos_composition_load_only)
    endif()
    target_link_libraries(${_test} PRIVATE ${_test}_swift
      ${_composition} apptraverse_ui_macos)
    target_compile_options(${_test} PRIVATE
      $<$<COMPILE_LANGUAGE:OBJCXX>:-fno-rtti>
      $<$<COMPILE_LANGUAGE:OBJCXX>:-fno-objc-exceptions>)
    set_target_properties(${_test} PROPERTIES LINKER_LANGUAGE OBJCXX)
  endforeach()
  add_test(NAME apptraverse_macos_window_catalog_acceptance
    COMMAND ${CMAKE_COMMAND}
      -DDEBUG_EXE=$<TARGET_FILE:apptraverse_macos_window_catalog_acceptance_debug>
      -DLOAD_EXE=$<TARGET_FILE:apptraverse_macos_window_catalog_acceptance_load_only>
      -DARTIFACT_ROOT=${CMAKE_BINARY_DIR}/macos-acceptance
      -P ${CMAKE_CURRENT_SOURCE_DIR}/macos_window_catalog_acceptance.cmake)
  set_tests_properties(apptraverse_macos_window_catalog_acceptance PROPERTIES
    TIMEOUT 180 RUN_SERIAL TRUE)
endif()

if(APPLE AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS" AND
   CMAKE_BUILD_TYPE STREQUAL "Debug" AND TARGET minimal_app_macos_composition)
  add_library(apptraverse_macos_minimal_app_acceptance_swift STATIC
    macos_minimal_app_acceptance.swift)
  set_target_properties(apptraverse_macos_minimal_app_acceptance_swift PROPERTIES
    Swift_MODULE_NAME MacMinimalAppAcceptance)
  target_link_libraries(apptraverse_macos_minimal_app_acceptance_swift PRIVATE
    apptraverse_ui_macos_swift)
  target_compile_options(apptraverse_macos_minimal_app_acceptance_swift PRIVATE
    "SHELL:-import-objc-header ${PROJECT_SOURCE_DIR}/examples/minimal_app/macos/minimal_app_macos_composition.h"
    "SHELL:-Xcc -I${PROJECT_SOURCE_DIR}/include")
  add_executable(apptraverse_macos_minimal_app_acceptance
    macos_ui_backend_lifecycle_test_link.mm)
  target_link_libraries(apptraverse_macos_minimal_app_acceptance PRIVATE
    apptraverse_macos_minimal_app_acceptance_swift minimal_app_macos_composition
    apptraverse_ui_macos)
  set_target_properties(apptraverse_macos_minimal_app_acceptance PROPERTIES
    LINKER_LANGUAGE OBJCXX)
  add_test(NAME apptraverse_macos_minimal_app_acceptance
    COMMAND apptraverse_macos_minimal_app_acceptance)
  set_tests_properties(apptraverse_macos_minimal_app_acceptance PROPERTIES
    TIMEOUT 45 RUN_SERIAL TRUE)
endif()
