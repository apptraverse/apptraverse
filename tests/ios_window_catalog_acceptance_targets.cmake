if(IOS AND CMAKE_BUILD_TYPE STREQUAL "Debug" AND APPTRAVERSE_BUILD_IOS_ACCEPTANCE)
  foreach(_kind IN ITEMS debug load_only)
    set(_test apptraverse_ios_window_catalog_acceptance_${_kind})
    string(REPLACE "_" "" _bundle_suffix "${_kind}")
    add_library(${_test}_swift STATIC ${PROJECT_SOURCE_DIR}/tests/ios_window_catalog_acceptance.swift)
    set_target_properties(${_test}_swift PROPERTIES Swift_MODULE_NAME IosWindowCatalogAcceptance_${_kind})
    target_link_libraries(${_test}_swift PRIVATE apptraverse_ui_ios_swift)
    target_compile_options(${_test}_swift PRIVATE
      "SHELL:-import-objc-header ${PROJECT_SOURCE_DIR}/examples/surfaces_demo/ios/window_catalog_composition.h"
      "SHELL:-Xcc -I${PROJECT_SOURCE_DIR}/include")
    add_executable(${_test} ${PROJECT_SOURCE_DIR}/examples/surfaces_demo/ios/window_catalog_link.mm)
    if(_kind STREQUAL "debug")
      set(_composition window_catalog_ios_composition)
    else()
      set(_composition window_catalog_ios_composition_load_only)
    endif()
    target_link_libraries(${_test} PRIVATE ${_test}_swift ${_composition} apptraverse_ui_ios)
    set_target_properties(${_test} PROPERTIES LINKER_LANGUAGE OBJCXX MACOSX_BUNDLE TRUE
      MACOSX_BUNDLE_INFO_PLIST ${PROJECT_SOURCE_DIR}/examples/surfaces_demo/ios/window_catalog_Info.plist.in
      MACOSX_BUNDLE_BUNDLE_NAME ${_test}
      MACOSX_BUNDLE_GUI_IDENTIFIER org.apptraverse.WindowCatalog.ios.acceptance.${_bundle_suffix}
      MACOSX_BUNDLE_SHORT_VERSION_STRING 0.1 MACOSX_BUNDLE_BUNDLE_VERSION 1)
  endforeach()
endif()
