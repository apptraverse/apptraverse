add_library(surfaces_demo_model STATIC
  common/surfaces_model.cpp
)

target_include_directories(surfaces_demo_model PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/common
)

target_link_libraries(surfaces_demo_model PUBLIC apptraverse)
target_compile_features(surfaces_demo_model PUBLIC cxx_std_20)

add_library(surfaces_demo_distill STATIC
  common/surfaces_distill.cpp
)
target_link_libraries(surfaces_demo_distill PUBLIC
  surfaces_demo_model
)
target_compile_features(surfaces_demo_distill PUBLIC cxx_std_20)

# Headless-only slice: no windows/ linux/ macos/ ports yet.
