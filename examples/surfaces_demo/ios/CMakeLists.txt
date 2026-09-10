enable_language(OBJCXX)

set(CMAKE_OBJCXX_STANDARD 20)
set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)

set(_surfaces_ios_sources
  main.mm
  ios_app.mm
  ios_presenters.mm
  ${CMAKE_CURRENT_SOURCE_DIR}/../common/surfaces_lifecycle.cpp
)

# ARC for UIKit ownership in .mm; C++ RTTI stays off via apptraverse_compile_policy.
set_source_files_properties(
  main.mm ios_app.mm ios_presenters.mm
  PROPERTIES COMPILE_FLAGS "-fobjc-arc"
)

add_executable(ios_surfaces_demo ${_surfaces_ios_sources})
target_compile_definitions(ios_surfaces_demo PRIVATE
  APPTRAVERSE_ENABLE_DISTILLATION
)
target_link_libraries(ios_surfaces_demo PRIVATE
  surfaces_demo_distill
)

add_executable(ios_surfaces_demo_load_only ${_surfaces_ios_sources})
# Load-only: model + mobile/iOS presenters — no distill bootstrap.

foreach(_surfaces_demo ios_surfaces_demo ios_surfaces_demo_load_only)
  target_link_libraries(${_surfaces_demo} PRIVATE
    surfaces_demo_model
    surfaces_demo_mobile
    "-framework UIKit"
    "-framework Foundation"
    "-framework CoreGraphics"
  )
  target_include_directories(${_surfaces_demo} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../common
    ${CMAKE_CURRENT_SOURCE_DIR}/../mobile
  )
  # Ensure ObjC++ TUs also get -fno-rtti (policy is on the C++ link interface).
  target_compile_options(${_surfaces_demo} PRIVATE
    $<$<COMPILE_LANGUAGE:OBJCXX>:-fno-rtti>
  )
  set_target_properties(${_surfaces_demo} PROPERTIES
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in
    MACOSX_BUNDLE_BUNDLE_NAME ${_surfaces_demo}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
  )
endforeach()

set_target_properties(ios_surfaces_demo PROPERTIES
  MACOSX_BUNDLE_GUI_IDENTIFIER com.apptraverse.surfaces
)
set_target_properties(ios_surfaces_demo_load_only PROPERTIES
  MACOSX_BUNDLE_GUI_IDENTIFIER com.apptraverse.surfaces.loadonly
)
