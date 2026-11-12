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

set(APPTRAVERSE_AETHER_GIT_TAG "59153adf1fb8baba088b28b50051fb961e8d6eaf")
set(APPTRAVERSE_AETHER_OBJECTS_GIT_TAG "80fbb4c33bebd660c9bdf0687311cee30de74f39")
set(APPTRAVERSE_AETHER_MISCPP_GIT_TAG "6839624442621a5b060ae610e91f480278ebaf6e")
set(APPTRAVERSE_AETHER_NUMERIC_GIT_TAG "9d79bd79188420533bab07c437d617aedfaf6b1a")
set(APPTRAVERSE_AETHER_TELE_GIT_TAG "main")
set(APPTRAVERSE_GCEM_GIT_TAG "f182c6f3d6e0742eb9eef4fff506a3928d4c5107")
