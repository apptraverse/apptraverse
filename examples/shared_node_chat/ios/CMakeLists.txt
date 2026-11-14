# Swift must use the same device/simulator SDK as the C++ backend.
if(CMAKE_OSX_SYSROOT MATCHES "[Ii][Pp]hone[Ss]imulator")
  set(_apptraverse_ios_swift_platform iphonesimulator)
  set(_apptraverse_ios_swift_target_suffix "-simulator")
else()
  set(_apptraverse_ios_swift_platform iphoneos)
  set(_apptraverse_ios_swift_target_suffix "")
endif()

set_source_files_properties(../apple/ChatBridge.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc -fno-objc-exceptions")
add_library(apptraverse_chat_ios_bridge STATIC ../apple/ChatBridge.mm)
target_link_libraries(apptraverse_chat_ios_bridge PRIVATE apptraverse_shared_chat_ui "-framework Foundation")
add_library(apptraverse_chat_ios_swift STATIC ../apple/ChatPresenter.swift ChatMain.swift)
set_target_properties(apptraverse_chat_ios_swift PROPERTIES Swift_MODULE_NAME ChatIos)
target_compile_options(apptraverse_chat_ios_swift PRIVATE
  "SHELL:-import-objc-header ${CMAKE_CURRENT_SOURCE_DIR}/../apple/ChatBridge.h")
add_executable(shared_node_chat MACOSX_BUNDLE ../macos/link.mm)
set_target_properties(shared_node_chat PROPERTIES
  LINKER_LANGUAGE OBJCXX
  MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in"
  MACOSX_BUNDLE_BUNDLE_NAME "SharedNode Chat"
  MACOSX_BUNDLE_GUI_IDENTIFIER org.apptraverse.SharedNodeChat.ios
  MACOSX_BUNDLE_SHORT_VERSION_STRING "0.1"
  MACOSX_BUNDLE_BUNDLE_VERSION "1")
target_link_libraries(shared_node_chat PRIVATE apptraverse_chat_ios_swift
  apptraverse_chat_ios_bridge apptraverse_shared_chat_session "-framework UIKit" "-framework SwiftUI")
get_filename_component(_swift_bin_dir ${CMAKE_Swift_COMPILER} DIRECTORY)
target_link_options(shared_node_chat PRIVATE
  -L${CMAKE_OSX_SYSROOT}/usr/lib/swift
  -L${_swift_bin_dir}/../lib/swift/${_apptraverse_ios_swift_platform}
  -Wl,-rpath,/usr/lib/swift)
