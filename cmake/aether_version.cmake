# Pin for aether-client-cpp (network/client) and aether-objects (Obj/Domain).
# Directory of this module (safe when AppTraverse is an add_subdirectory/Android
# parent project). Prefer this over CMAKE_SOURCE_DIR for owned patches.
get_filename_component(APPTRAVERSE_CMAKE_DIR "${CMAKE_CURRENT_LIST_DIR}" ABSOLUTE)
get_filename_component(APPTRAVERSE_REPO_ROOT "${APPTRAVERSE_CMAKE_DIR}/.." ABSOLUTE)
set(APPTRAVERSE_AETHER_OBJECTS_SCOPE_PATCH
  "${APPTRAVERSE_CMAKE_DIR}/patches/aether-objects-domain-graph-serialization-scope.patch")

set(APPTRAVERSE_AETHER_GIT_TAG "0b0e3b54b9ffa730c41597c8b18f6a75255bded3")
set(APPTRAVERSE_AETHER_OBJECTS_GIT_TAG "main")
set(APPTRAVERSE_AETHER_MISCPP_GIT_TAG "main")
set(APPTRAVERSE_AETHER_NUMERIC_GIT_TAG "main")
set(APPTRAVERSE_AETHER_TELE_GIT_TAG "main")
