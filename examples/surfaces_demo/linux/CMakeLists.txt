find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK3 REQUIRED IMPORTED_TARGET gtk+-3.0)

set(_surfaces_linux_sources
  main.cpp
  linux_app.cpp
  linux_presenters.cpp
  linux_fatal.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../common/surfaces_lifecycle.cpp
)

add_executable(linux_surfaces_demo ${_surfaces_linux_sources})
target_compile_definitions(linux_surfaces_demo PRIVATE
  APPTRAVERSE_ENABLE_DISTILLATION
)
target_link_libraries(linux_surfaces_demo PRIVATE
  surfaces_demo_distill
)

add_executable(linux_surfaces_demo_load_only ${_surfaces_linux_sources})
# Load-only: model + desktop/linux presenters — no distill bootstrap.

foreach(_surfaces_demo linux_surfaces_demo linux_surfaces_demo_load_only)
  target_link_libraries(${_surfaces_demo} PRIVATE
    surfaces_demo_model
    surfaces_demo_desktop
    PkgConfig::GTK3
  )
  target_include_directories(${_surfaces_demo} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../common
    ${CMAKE_CURRENT_SOURCE_DIR}/../desktop
  )
endforeach()
