add_library(dynamic_objects_demo_model STATIC
  common/dynamic_model.cpp
)

target_include_directories(dynamic_objects_demo_model PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/common
)

target_link_libraries(dynamic_objects_demo_model PUBLIC apptraverse)
target_compile_features(dynamic_objects_demo_model PUBLIC cxx_std_20)

# Dev-only graph construction. Linked by distill demos and fixture tests only.
add_library(dynamic_objects_demo_distill STATIC
  common/dynamic_distill.cpp
)
target_link_libraries(dynamic_objects_demo_distill PUBLIC
  dynamic_objects_demo_model
)
target_compile_features(dynamic_objects_demo_distill PUBLIC cxx_std_20)

if(WIN32)
  add_subdirectory(windows)
endif()
