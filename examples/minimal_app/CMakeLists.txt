add_library(minimal_app_common STATIC
  common/minimal_application.cpp
  common/minimal_app_graph_builder.cpp
)
target_include_directories(minimal_app_common PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/common
)
target_link_libraries(minimal_app_common PUBLIC apptraverse)
target_compile_features(minimal_app_common PUBLIC cxx_std_20)
if(MSVC)
  target_compile_options(minimal_app_common PRIVATE /utf-8)
endif()

add_library(minimal_app_headless STATIC
  headless/minimal_app_graph_headless.cpp
)
target_include_directories(minimal_app_headless PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/headless
  ${CMAKE_CURRENT_SOURCE_DIR}/common
)
target_link_libraries(minimal_app_headless PUBLIC minimal_app_common)
target_compile_features(minimal_app_headless PUBLIC cxx_std_20)
if(MSVC)
  target_compile_options(minimal_app_headless PRIVATE /utf-8)
endif()

if(WIN32 AND APPTRAVERSE_BUILD_EXAMPLES)
  add_subdirectory(windows)
endif()

if(EMSCRIPTEN AND APPTRAVERSE_BUILD_EXAMPLES)
  add_subdirectory(web)
endif()
