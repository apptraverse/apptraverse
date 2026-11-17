# Optional service UID is a build-time product choice. Empty means the ordinary
# Aether transport is returned, with no mailbox wrapping or polling.
set(APPTRAVERSE_MAILBOX_SERVICE_UID "" CACHE STRING "Offline mailbox Aether UID; empty disables the client feature")
set(APPTRAVERSE_MAILBOX_TTL_SECONDS "86400" CACHE STRING "Lifetime of an offline transport message")
option(APPTRAVERSE_BUILD_MAILBOX_SERVICE "Build the RocksDB-backed Aether mailbox service" OFF)
option(APPTRAVERSE_BUILD_MAILBOX_STORE "Build the RocksDB mailbox backend and its tests" ${APPTRAVERSE_BUILD_MAILBOX_SERVICE})
if(APPTRAVERSE_MAILBOX_SERVICE_UID)
  string(LENGTH "${APPTRAVERSE_MAILBOX_SERVICE_UID}" _mailbox_uid_length)
  if(NOT _mailbox_uid_length EQUAL 36 OR
     NOT APPTRAVERSE_MAILBOX_SERVICE_UID MATCHES "^[0-9A-Fa-f]+-[0-9A-Fa-f]+-[0-9A-Fa-f]+-[0-9A-Fa-f]+-[0-9A-Fa-f]+$" OR
     APPTRAVERSE_MAILBOX_SERVICE_UID STREQUAL "00000000-0000-0000-0000-000000000000")
    message(FATAL_ERROR "APPTRAVERSE_MAILBOX_SERVICE_UID must be a nonzero Aether UUID")
  endif()
  foreach(_offset 8 13 18 23)
    string(SUBSTRING "${APPTRAVERSE_MAILBOX_SERVICE_UID}" ${_offset} 1 _dash)
    if(NOT _dash STREQUAL "-")
      message(FATAL_ERROR "Invalid mailbox Aether UUID")
    endif()
  endforeach()
  string(TOLOWER "${APPTRAVERSE_MAILBOX_SERVICE_UID}" APPTRAVERSE_MAILBOX_SERVICE_UID)
endif()
if(NOT APPTRAVERSE_MAILBOX_TTL_SECONDS MATCHES "^[0-9]+$" OR
   APPTRAVERSE_MAILBOX_TTL_SECONDS LESS 1 OR
   APPTRAVERSE_MAILBOX_TTL_SECONDS GREATER 604800)
  message(FATAL_ERROR "Mailbox TTL must be between 1 and 604800 seconds")
endif()
add_subdirectory(services/offline_mailbox)
