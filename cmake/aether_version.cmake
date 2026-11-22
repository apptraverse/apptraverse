# Pin for aether-client-cpp (network/client) and aether-objects (Obj/Domain).
# Directory of this module (safe when AppTraverse is an add_subdirectory/Android
# parent project). Prefer this over CMAKE_SOURCE_DIR for owned patches.
get_filename_component(APPTRAVERSE_CMAKE_DIR "${CMAKE_CURRENT_LIST_DIR}" ABSOLUTE)
get_filename_component(APPTRAVERSE_REPO_ROOT "${APPTRAVERSE_CMAKE_DIR}/.." ABSOLUTE)
set(APPTRAVERSE_AETHER_OBJECTS_SCOPE_PATCH
  "${APPTRAVERSE_CMAKE_DIR}/patches/aether-objects-domain-graph-serialization-scope.patch")
set(APPTRAVERSE_AETHER_OBJECTS_FACTORY_PATCH
  "${APPTRAVERSE_CMAKE_DIR}/patches/aether-objects-compat-stored-factory-v1.patch")
set(APPTRAVERSE_AETHER_OBJECTS_PATCHES
  "${APPTRAVERSE_AETHER_OBJECTS_SCOPE_PATCH}"
  "${APPTRAVERSE_AETHER_OBJECTS_FACTORY_PATCH}")

set(APPTRAVERSE_AETHER_GIT_TAG "c6b2c90b3ac224230695a9ebc774ba4c6301828c")
set(APPTRAVERSE_AETHER_OBJECTS_GIT_TAG "952dcd6a9243db763fd7288a60497e4aac4d2ac6")
set(APPTRAVERSE_AETHER_MISCPP_GIT_TAG "6839624442621a5b060ae610e91f480278ebaf6e")
set(APPTRAVERSE_AETHER_NUMERIC_GIT_TAG "d5969e7a50f1fbbf0a195bb8b256ad0c9c15115c")
set(APPTRAVERSE_AETHER_TELE_GIT_TAG "d7b05610e03750e017b8d227d468dd4950449af1")
set(APPTRAVERSE_GCEM_GIT_TAG "f182c6f3d6e0742eb9eef4fff506a3928d4c5107")

# The explicitly selected owned Objects source is prepared once for the workspace.
if(CPM_aether-objects_SOURCE)
  if(NOT EXISTS "${CPM_aether-objects_SOURCE}/src/aether-objects/obj/domain_buffer.h")
    message(FATAL_ERROR "Prepare the owned local Objects native graph extension before configuring")
  endif()
  set(APPTRAVERSE_AETHER_OBJECTS_PATCHES "")
endif()

# The published Objects revision already contains the tested native graph extension.
set(APPTRAVERSE_AETHER_OBJECTS_PATCHES "")
