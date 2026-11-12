# Size policy is scoped to native iOS MinSizeRel, including its dependencies.
# Swift retains its platform ABI and CMake's -Osize whole-module compilation.
option(APPTRAVERSE_IOS_MIN_SIZE_OPTIMIZATIONS
  "Use -Oz, ThinLTO, hidden C++ symbols and dead stripping for iOS MinSizeRel" ON)

function(apptraverse_ios_strip_min_size target)
  if(IOS AND CMAKE_BUILD_TYPE STREQUAL "MinSizeRel" AND
     APPTRAVERSE_IOS_MIN_SIZE_OPTIMIZATIONS)
    add_custom_command(TARGET ${target} POST_BUILD
      COMMAND "${CMAKE_STRIP}" -x "$<TARGET_FILE:${target}>"
      VERBATIM COMMENT "Remove local symbols from ${target}")
  endif()
endfunction()

if(NOT IOS OR NOT CMAKE_BUILD_TYPE STREQUAL "MinSizeRel" OR
   NOT APPTRAVERSE_IOS_MIN_SIZE_OPTIMIZATIONS)
  return()
endif()
if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  message(FATAL_ERROR "iOS size optimization requires the Clang toolchain")
endif()

# Match the bitcode archiver to the compiler (MacPorts LLVM may be newer than
# Xcode's archive tools). The Clang link driver also selects its matching libLTO.
foreach(_tool llvm-ar llvm-ranlib)
  execute_process(COMMAND "${CMAKE_CXX_COMPILER}" "-print-prog-name=${_tool}"
    OUTPUT_VARIABLE _ios_${_tool} OUTPUT_STRIP_TRAILING_WHITESPACE
    COMMAND_ERROR_IS_FATAL ANY)
  if(NOT EXISTS "${_ios_${_tool}}")
    message(FATAL_ERROR "Matching ${_tool} is required for iOS ThinLTO")
  endif()
endforeach()
set(CMAKE_AR "${_ios_llvm-ar}")
set(CMAKE_RANLIB "${_ios_llvm-ranlib}")
# Nested dependency project() and enable_language() may restore cached binutils.
# Pin the archive rules as well, so every bitcode archive has a readable index.
foreach(_language C CXX OBJC OBJCXX Swift)
  set(CMAKE_${_language}_ARCHIVE_CREATE
    "\"${_ios_llvm-ar}\" qc <TARGET> <LINK_FLAGS> <OBJECTS>")
  set(CMAKE_${_language}_ARCHIVE_APPEND
    "\"${_ios_llvm-ar}\" q <TARGET> <LINK_FLAGS> <OBJECTS>")
  set(CMAKE_${_language}_ARCHIVE_FINISH "\"${_ios_llvm-ranlib}\" <TARGET>")
endforeach()

foreach(_flag -Oz -flto=thin -ffunction-sections -fdata-sections -fvisibility=hidden)
  add_compile_options("$<$<COMPILE_LANGUAGE:C,CXX,OBJC,OBJCXX>:${_flag}>")
endforeach()
foreach(_flag -fno-exceptions -fno-rtti -fvisibility-inlines-hidden)
  add_compile_options("$<$<COMPILE_LANGUAGE:CXX,OBJCXX>:${_flag}>")
endforeach()
add_compile_options("$<$<COMPILE_LANGUAGE:OBJC,OBJCXX>:-fno-objc-exceptions>")
add_link_options(-flto=thin "LINKER:-dead_strip")
