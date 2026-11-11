# Native-host iOS Simulator build, using MacPorts C++20 and Xcode Swift/SDK.
set(CMAKE_SYSTEM_NAME iOS)
execute_process(COMMAND xcrun --sdk iphonesimulator --show-sdk-path
  OUTPUT_VARIABLE _ios_sdk OUTPUT_STRIP_TRAILING_WHITESPACE
  COMMAND_ERROR_IS_FATAL ANY)
set(CMAKE_OSX_SYSROOT "${_ios_sdk}" CACHE PATH "iOS Simulator SDK")
if(NOT CMAKE_OSX_ARCHITECTURES)
  set(CMAKE_OSX_ARCHITECTURES "${CMAKE_HOST_SYSTEM_PROCESSOR}" CACHE STRING "Simulator architecture")
endif()
set(CMAKE_OSX_DEPLOYMENT_TARGET 17.2 CACHE STRING "iOS Simulator deployment target")
set(CMAKE_C_COMPILER /opt/local/bin/clang-mp-20 CACHE FILEPATH "C compiler")
set(CMAKE_CXX_COMPILER /opt/local/bin/clang++-mp-20 CACHE FILEPATH "C++20 compiler")
set(CMAKE_OBJC_COMPILER "${CMAKE_C_COMPILER}" CACHE FILEPATH "Objective-C compiler")
set(CMAKE_OBJCXX_COMPILER "${CMAKE_CXX_COMPILER}" CACHE FILEPATH "Objective-C++ compiler")
foreach(_lang C CXX OBJC OBJCXX)
  set(CMAKE_${_lang}_COMPILER_TARGET "${CMAKE_OSX_ARCHITECTURES}-apple-ios${CMAKE_OSX_DEPLOYMENT_TARGET}-simulator")
endforeach()
set(CMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED NO)
