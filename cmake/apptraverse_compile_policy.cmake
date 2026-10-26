# Shared by all AppTraverse consumers. No C++ RTTI or C++ exceptions.
add_library(apptraverse_compile_policy INTERFACE)
if(MSVC)
  target_compile_options(apptraverse_compile_policy INTERFACE
    /GR- /EHs-c- /utf-8 /Zc:preprocessor /Zc:__cplusplus /Zc:externConstexpr /bigobj
    $<$<CONFIG:MinSizeRel>:/O1;/Ob1;/Oi;/Os;/Oy;/GL;/Gy;/GF;/GS->
    $<$<CONFIG:MinSizeRel>:/fp:fast;/fp:except->)
  target_compile_definitions(apptraverse_compile_policy INTERFACE _HAS_EXCEPTIONS=0)
else()
  target_compile_options(apptraverse_compile_policy INTERFACE
    -fno-rtti -fno-exceptions)
endif()
