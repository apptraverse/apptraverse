add_executable(aether_presence_monitor main.cpp)

target_link_libraries(aether_presence_monitor PRIVATE
  apptraverse
  user32
  gdi32
)

target_include_directories(aether_presence_monitor PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_SOURCE_DIR}/include
)

target_compile_definitions(aether_presence_monitor PRIVATE
  AE_TELE_LOG_CONSOLE=0
  APPTRAVERSE_AETHER_SHA="${APPTRAVERSE_AETHER_GIT_TAG}"
)

if(MSVC)
  target_compile_options(aether_presence_monitor PRIVATE /utf-8 /W4)
  set_target_properties(aether_presence_monitor PROPERTIES
    WIN32_EXECUTABLE TRUE
  )
  target_link_options(aether_presence_monitor PRIVATE
    /ENTRY:wWinMainCRTStartup
  )
endif()

if(MINGW)
  set_target_properties(aether_presence_monitor PROPERTIES EXCLUDE_FROM_ALL TRUE)
endif()
