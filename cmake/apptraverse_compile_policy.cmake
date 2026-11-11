# Shared by all AppTraverse consumers. No C++ RTTI or C++ exceptions.
add_library(apptraverse_compile_policy INTERFACE)
if(MSVC)
  target_compile_options(apptraverse_compile_policy INTERFACE
    /GR- /EHs-c- /utf-8 /Zc:preprocessor /Zc:__cplusplus /Zc:externConstexpr /bigobj
    $<$<NOT:$<CONFIG:Debug>>:/Gy;/Gw;/GF>
    $<$<CONFIG:MinSizeRel>:/O1;/Ob1;/Oi;/Os;/Oy;/GL;/GS->
    $<$<CONFIG:MinSizeRel>:/fp:fast;/fp:except->)
  target_compile_definitions(apptraverse_compile_policy INTERFACE _HAS_EXCEPTIONS=0)
  target_link_options(apptraverse_compile_policy INTERFACE
    $<$<NOT:$<CONFIG:Debug>>:/OPT:REF;/OPT:ICF=5>)
else()
  target_compile_options(apptraverse_compile_policy INTERFACE
    -fno-rtti -fno-exceptions)
endif()
