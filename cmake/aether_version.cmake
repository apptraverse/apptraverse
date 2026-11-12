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
  local-persistent-load-telemetry-v1
  domain-storage-access
  reject-unknown-stored-class-telemetry-v1
  objptr-reset-flags
  force-materialize
  should-save-version-scope
  origin-s0
  obj-layer-factory
  get-most-related-stored-factory-v2
  domain-registered-objects
  clone-save-remap
  clone-canonical-scope
  clone-reference-visitor
  emscripten-domain-buffer-split
  nested-obj-archive-deseri
  graph-load-instance-identity
  origin-layer-lookup
  historical-base-snapshot-scope
  domain-hierarchy-v2
  scoped-layer-hydration-v1
  force-materialize-object-layer-v1
  historical-materialized-version-scope-v1
  storage-constant-query-v1
  object-scope-metadata-v2
  domain-local-storage-precedence-v1
  scoped-object-reference-visitor-v1
  domain-ancestor-factory-v1
  domain-scoped-object-id-sequence-v1
  ptr-releasing-edge-v1
)
set(APPTRAVERSE_AETHER_OBJECTS_PATCHES)
foreach(patch_name IN LISTS APPTRAVERSE_AETHER_OBJECTS_PATCH_NAMES)
  list(APPEND APPTRAVERSE_AETHER_OBJECTS_PATCHES
    "${APPTRAVERSE_CMAKE_DIR}/patches/aether-objects-${patch_name}.patch")
endforeach()

# Pinned SHA verified with a clean CPM cache (GraphSerializationScope + LoadStoredLayers).
set(APPTRAVERSE_AETHER_OBJECTS_GIT_TAG
  "80fbb4c33bebd660c9bdf0687311cee30de74f39")
set(APPTRAVERSE_AETHER_MISCPP_GIT_TAG "6839624442621a5b060ae610e91f480278ebaf6e")
set(APPTRAVERSE_AETHER_NUMERIC_GIT_TAG "9d79bd79188420533bab07c437d617aedfaf6b1a")
set(APPTRAVERSE_AETHER_TELE_GIT_TAG "main")
