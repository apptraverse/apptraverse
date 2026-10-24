# Central AppTraverse C++ compile policy (no RTTI, reusable by future targets).
#
# Link AppTraverse-owned C++ targets to apptraverse_compile_policy:
# - PUBLIC from the `apptraverse` library (covers normal consumers automatically)
# - PRIVATE when a target does not link `apptraverse` (header-only / helper tests)
#
# Future surfaces_demo / SharedNode targets that link `apptraverse` inherit
# /GR- or -fno-rtti without adding the flag manually.

add_library(apptraverse_compile_policy INTERFACE)

if(MSVC)
  # CMake defaults CMAKE_CXX_FLAGS to /EHsc; strip it so /EHs-c- is authoritative.
  foreach(_apptraverse_cxx_flag_var CMAKE_CXX_FLAGS CMAKE_CXX_FLAGS_DEBUG
                                   CMAKE_CXX_FLAGS_RELEASE
                                   CMAKE_CXX_FLAGS_RELWITHDEBINFO
                                   CMAKE_CXX_FLAGS_MINSIZEREL)
    if(DEFINED ${_apptraverse_cxx_flag_var})
      string(REPLACE "/EHsc" "" ${_apptraverse_cxx_flag_var}
                     "${${_apptraverse_cxx_flag_var}}")
      set(${_apptraverse_cxx_flag_var} "${${_apptraverse_cxx_flag_var}}"
          CACHE STRING "Flags used by the CXX compiler during all build types."
               FORCE)
    endif()
  endforeach()

  target_compile_options(apptraverse_compile_policy INTERFACE
    /GR-
    /EHs-c-
    /Zc:preprocessor
    /Zc:__cplusplus
    /Zc:externConstexpr
    /bigobj
    /wd4530
    $<$<CONFIG:MinSizeRel>:/O1;/Ob1;/Oi;/Os;/Oy;/GL;/Gy;/GF;/GS->
    $<$<CONFIG:MinSizeRel>:/fp:fast;/fp:except->
  )
  target_compile_definitions(apptraverse_compile_policy INTERFACE
    _HAS_EXCEPTIONS=0
  )
else()
  target_compile_options(apptraverse_compile_policy INTERFACE -fno-rtti -fno-exceptions)
endif()
