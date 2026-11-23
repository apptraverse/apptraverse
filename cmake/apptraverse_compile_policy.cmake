# Shared by all AppTraverse consumers. No C++ RTTI or C++ exceptions.
add_library(apptraverse_compile_policy INTERFACE)
if(MSVC)
  # Win7 is the minimum desktop Windows contract. v145/VS2026 ships an STL
  # filesystem implementation with hard CreateFile2/CopyFile2 imports; target
  # macros cannot make that prebuilt runtime Win7-compatible. Use desktop v143
  # (VS2022, e.g. 14.44), not shims for undocumented STL implementation symbols.
  if(WIN32 AND MSVC_VERSION GREATER_EQUAL 1950)
    message(FATAL_ERROR
      "AppTraverse requires Windows 7 compatibility. MSVC ${MSVC_VERSION} "
      "uses a Windows 10+ STL runtime. Configure with Visual Studio 2022 "
      "desktop v143 (for example 14.44), or select its cl.exe in a new build directory.")
  endif()
  if(WIN32)
    target_compile_definitions(apptraverse_compile_policy INTERFACE
      WINVER=0x0601 _WIN32_WINNT=0x0601 _STL_WIN32_WINNT=0x0601)
  endif()
  set(_apptraverse_size_inline /Ob1)
  set(_apptraverse_size_fp /fp:precise)
  set(_apptraverse_size_icf /OPT:ICF=5)
  if(APPTRAVERSE_MINIMUM_WINDOWS_X86)
    set(_apptraverse_size_inline /Ob2)
    set(_apptraverse_size_fp /fp:fast)
    # Release/RelWithDebInfo retain their ordinary link policy.
    set(_apptraverse_size_icf "$<IF:$<CONFIG:MinSizeRel>,/OPT:ICF=10,/OPT:ICF=5>")
  endif()
  target_compile_options(apptraverse_compile_policy INTERFACE
    /GR- /EHs-c- /utf-8 /Zc:preprocessor /Zc:__cplusplus /Zc:externConstexpr /bigobj
    $<$<NOT:$<CONFIG:Debug>>:/Gy;/Gw;/GF>
    $<$<CONFIG:MinSizeRel>:/O1;${_apptraverse_size_inline};/Oi;/Os;/Oy;/GL;/GS->
    $<$<CONFIG:MinSizeRel>:${_apptraverse_size_fp};/fp:except->)
  target_compile_definitions(apptraverse_compile_policy INTERFACE _HAS_EXCEPTIONS=0)
  target_link_options(apptraverse_compile_policy INTERFACE
    $<$<NOT:$<CONFIG:Debug>>:/OPT:REF;${_apptraverse_size_icf}>)
else()
  target_compile_options(apptraverse_compile_policy INTERFACE
    "$<$<COMPILE_LANGUAGE:CXX,OBJCXX>:-fno-rtti;-fno-exceptions>")
  if(EMSCRIPTEN)
    # Emscripten selects libembind's TypeID ABI from link-driver flags. Setting
    # these only on compilation mixes address-based no-RTTI IDs with RTTI IDs,
    # so ordinary std::string bindings are unbound at runtime (__getTypeName OOB).
    target_link_options(apptraverse_compile_policy INTERFACE
      -fno-rtti -fno-exceptions)
  endif()
endif()
