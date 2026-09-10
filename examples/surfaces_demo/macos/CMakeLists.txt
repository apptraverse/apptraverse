enable_language(OBJCXX)

set(CMAKE_OBJCXX_STANDARD 20)
set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)

set(_surfaces_mac_sources
  main.mm
  mac_app.mm
  mac_presenters.mm
  ${CMAKE_CURRENT_SOURCE_DIR}/../common/surfaces_lifecycle.cpp
)

# ARC for AppKit ownership in .mm; C++ RTTI stays off via apptraverse_compile_policy.
set_source_files_properties(
  main.mm mac_app.mm mac_presenters.mm
  PROPERTIES COMPILE_FLAGS "-fobjc-arc"
)

add_executable(macos_surfaces_demo ${_surfaces_mac_sources})
target_compile_definitions(macos_surfaces_demo PRIVATE
  APPTRAVERSE_ENABLE_DISTILLATION
)
target_link_libraries(macos_surfaces_demo PRIVATE
  surfaces_demo_distill
)

add_executable(macos_surfaces_demo_load_only ${_surfaces_mac_sources})
# Load-only: model + desktop/mac presenters — no distill bootstrap.

foreach(_surfaces_demo macos_surfaces_demo macos_surfaces_demo_load_only)
  target_link_libraries(${_surfaces_demo} PRIVATE
    surfaces_demo_model
    surfaces_demo_desktop
    "-framework AppKit"
    "-framework Foundation"
  )
  target_include_directories(${_surfaces_demo} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../common
    ${CMAKE_CURRENT_SOURCE_DIR}/../desktop
  )
  # Ensure ObjC++ TUs also get -fno-rtti (policy is on the C++ link interface).
  target_compile_options(${_surfaces_demo} PRIVATE
    $<$<COMPILE_LANGUAGE:OBJCXX>:-fno-rtti>
  )
endforeach()
