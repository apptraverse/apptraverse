# Pin for aether-objects (Obj/Domain) and related Aether libraries.
# Directory of this module (safe when AppTraverse is an add_subdirectory/Android
# parent project). Prefer this over CMAKE_SOURCE_DIR for owned patches.
get_filename_component(APPTRAVERSE_CMAKE_DIR "${CMAKE_CURRENT_LIST_DIR}" ABSOLUTE)
get_filename_component(APPTRAVERSE_REPO_ROOT "${APPTRAVERSE_CMAKE_DIR}/.." ABSOLUTE)
set(APPTRAVERSE_AETHER_OBJECTS_LOCAL_PERSISTENT_LOAD_PATCH
  "${APPTRAVERSE_CMAKE_DIR}/patches/aether-objects-local-persistent-load.patch")
set(APPTRAVERSE_AETHER_OBJECTS_DOMAIN_STORAGE_ACCESS_PATCH
  "${APPTRAVERSE_CMAKE_DIR}/patches/aether-objects-domain-storage-access.patch")
set(APPTRAVERSE_AETHER_OBJECTS_REJECT_UNKNOWN_STORED_CLASS_PATCH
  "${APPTRAVERSE_CMAKE_DIR}/patches/aether-objects-reject-unknown-stored-class.patch")
set(APPTRAVERSE_AETHER_OBJECTS_OBJPTR_RESET_FLAGS_PATCH
  "${APPTRAVERSE_CMAKE_DIR}/patches/aether-objects-objptr-reset-flags.patch")
set(APPTRAVERSE_AETHER_OBJECTS_FORCE_MATERIALIZE_PATCH
  "${APPTRAVERSE_CMAKE_DIR}/patches/aether-objects-force-materialize.patch")
set(APPTRAVERSE_AETHER_OBJECTS_SHOULD_SAVE_VERSION_PATCH
  "${APPTRAVERSE_CMAKE_DIR}/patches/aether-objects-should-save-version.patch")
set(APPTRAVERSE_AETHER_OBJECTS_ORIGIN_FACET_PATCH
  "${APPTRAVERSE_CMAKE_DIR}/patches/aether-objects-origin-facet.patch")
set(APPTRAVERSE_AETHER_OBJECTS_OBJ_FACET_FACTORY_PATCH
  "${APPTRAVERSE_CMAKE_DIR}/patches/aether-objects-obj-facet-factory.patch")
set(APPTRAVERSE_AETHER_OBJECTS_DOMAIN_REGISTERED_OBJECTS_PATCH
  "${APPTRAVERSE_CMAKE_DIR}/patches/aether-objects-domain-registered-objects.patch")
set(APPTRAVERSE_AETHER_OBJECTS_CLONE_SAVE_REMAP_PATCH
  "${APPTRAVERSE_CMAKE_DIR}/patches/aether-objects-clone-save-remap.patch")
set(APPTRAVERSE_AETHER_OBJECTS_CLONE_CANONICAL_STATE_PATCH
  "${APPTRAVERSE_CMAKE_DIR}/patches/aether-objects-clone-canonical-state.patch")
set(APPTRAVERSE_AETHER_OBJECTS_CLONE_REFERENCE_VISITOR_PATCH
  "${APPTRAVERSE_CMAKE_DIR}/patches/aether-objects-clone-reference-visitor.patch")
set(APPTRAVERSE_AETHER_OBJECTS_EMSCRIPTEN_DOMAIN_INCLUDE_PATCH
  "${APPTRAVERSE_CMAKE_DIR}/patches/aether-objects-emscripten-domain-include.patch")
set(APPTRAVERSE_AETHER_OBJECTS_NESTED_OBJ_ARCHIVE_DESERI_PATCH
  "${APPTRAVERSE_CMAKE_DIR}/patches/aether-objects-nested-obj-archive-deseri.patch")
set(APPTRAVERSE_AETHER_OBJECTS_COLD_LOAD_ROOT_FLUSH_PATCH
  "${APPTRAVERSE_CMAKE_DIR}/patches/aether-objects-cold-load-root-flush-v6.patch")
set(APPTRAVERSE_AETHER_OBJECTS_RESET_OBJECT_LOAD_STATE_PATCH
  "${APPTRAVERSE_CMAKE_DIR}/patches/aether-objects-reset-object-load-state-v6r1.patch")

# Pinned SHA verified with a clean CPM cache (GraphSerializationScope + LoadStoredLayers).
# Cold-load: v6 flush; v6r1 additive patch clears cycle/pending on evict/rematerialize.
set(APPTRAVERSE_AETHER_OBJECTS_GIT_TAG
  "79283b667cabf689da1059d618fe2a22abf51db4")
set(APPTRAVERSE_AETHER_MISCPP_GIT_TAG "main")
set(APPTRAVERSE_AETHER_NUMERIC_GIT_TAG "main")
set(APPTRAVERSE_AETHER_TELE_GIT_TAG "main")
