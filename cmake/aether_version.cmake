# Pin for aether-objects (Obj/Domain) and related Aether libraries.
# Directory of this module (safe when AppTraverse is an add_subdirectory/Android
# parent project). Prefer this over CMAKE_SOURCE_DIR for owned patches.
get_filename_component(APPTRAVERSE_CMAKE_DIR "${CMAKE_CURRENT_LIST_DIR}" ABSOLUTE)
get_filename_component(APPTRAVERSE_REPO_ROOT "${APPTRAVERSE_CMAKE_DIR}/.." ABSOLUTE)

# The only aether-objects patch order. CMakeLists.txt passes this list to CPM
# PATCHES and cmake/scripts/verify-aether-patch-chain.sh reads it from here.
# CPM applies patches only when it populates a new source directory; the
# directory hash follows this list of file names, not their contents, so a
# changed patch needs a new file name.
set(APPTRAVERSE_AETHER_OBJECTS_PATCH_NAMES
  local-persistent-load
  domain-storage-access
  reject-unknown-stored-class
  objptr-reset-flags
  force-materialize
  should-save-version
  origin-facet
  obj-facet-factory
  domain-registered-objects
  clone-save-remap
  clone-canonical-state
  clone-reference-visitor
  emscripten-domain-include
  nested-obj-archive-deseri
  graph-load-instance-identity
)
set(APPTRAVERSE_AETHER_OBJECTS_PATCHES)
foreach(patch_name IN LISTS APPTRAVERSE_AETHER_OBJECTS_PATCH_NAMES)
  list(APPEND APPTRAVERSE_AETHER_OBJECTS_PATCHES
    "${APPTRAVERSE_CMAKE_DIR}/patches/aether-objects-${patch_name}.patch")
endforeach()

# Pinned SHA verified with a clean CPM cache (GraphSerializationScope + LoadStoredLayers).
set(APPTRAVERSE_AETHER_OBJECTS_GIT_TAG
  "79283b667cabf689da1059d618fe2a22abf51db4")
set(APPTRAVERSE_AETHER_MISCPP_GIT_TAG "main")
set(APPTRAVERSE_AETHER_NUMERIC_GIT_TAG "main")
set(APPTRAVERSE_AETHER_TELE_GIT_TAG "main")
