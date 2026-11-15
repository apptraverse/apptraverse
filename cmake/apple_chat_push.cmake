# Signing/provisioning is deployment-owned. Never embed provider credentials.
set(APPTRAVERSE_APPLE_PUSH_ENVIRONMENT "" CACHE STRING "Apple push: empty (off), sandbox or production")
if(APPTRAVERSE_APPLE_PUSH_ENVIRONMENT)
  if(NOT APPTRAVERSE_APPLE_PUSH_ENVIRONMENT MATCHES "^(sandbox|production)$")
    message(FATAL_ERROR "Apple push environment must be sandbox or production")
  endif()
  set(_push_environment "${APPTRAVERSE_APPLE_PUSH_ENVIRONMENT}")
  if(_push_environment STREQUAL "sandbox")
    set(_push_entitlement_environment development)
  else()
    set(_push_entitlement_environment production)
  endif()
  if(IOS)
    set(_push_entitlement_key aps-environment)
  else()
    set(_push_entitlement_key com.apple.developer.aps-environment)
  endif()
  file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/shared_node_chat.entitlements"
    CONTENT "<?xml version=\"1.0\"?><plist version=\"1.0\"><dict><key>${_push_entitlement_key}</key><string>${_push_entitlement_environment}</string></dict></plist>")
endif()
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/apple_push_environment.txt"
  CONTENT "${APPTRAVERSE_APPLE_PUSH_ENVIRONMENT}")
set_property(TARGET shared_node_chat APPEND PROPERTY LINK_DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/apple_push_environment.txt")
add_custom_command(TARGET shared_node_chat POST_BUILD
  COMMAND ${CMAKE_COMMAND} "-DPLIST=$<TARGET_BUNDLE_DIR:shared_node_chat>/${_push_plist}"
    "-DENVIRONMENT=${APPTRAVERSE_APPLE_PUSH_ENVIRONMENT}" -P "${PROJECT_SOURCE_DIR}/cmake/set_apple_push_plist.cmake")
