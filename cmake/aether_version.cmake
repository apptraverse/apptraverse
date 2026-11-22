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
  native-graph-current-v1
  fast-version-index-v1
)
set(APPTRAVERSE_AETHER_OBJECTS_PATCHES)
foreach(patch_name IN LISTS APPTRAVERSE_AETHER_OBJECTS_PATCH_NAMES)
  list(APPEND APPTRAVERSE_AETHER_OBJECTS_PATCHES
    "${APPTRAVERSE_CMAKE_DIR}/patches/aether-objects-${patch_name}.patch")
endforeach()

# Current telemetry-capable Objects base plus the complete native graph extension.
set(APPTRAVERSE_AETHER_OBJECTS_GIT_TAG
  "67ea6a9d7f36fbb2d96bc988ece6df92e238f910")
set(APPTRAVERSE_AETHER_MISCPP_GIT_TAG "6839624442621a5b060ae610e91f480278ebaf6e")
set(APPTRAVERSE_AETHER_NUMERIC_GIT_TAG "d5969e7a50f1fbbf0a195bb8b256ad0c9c15115c")
set(APPTRAVERSE_AETHER_TELE_GIT_TAG "d7b05610e03750e017b8d227d468dd4950449af1")

# Local sources are caller-owned. Require the AppTraverse graph extension and
# never let FetchContent apply the same patch chain to a shared working tree.
if(CPM_aether-objects_SOURCE)
  if(NOT EXISTS "${CPM_aether-objects_SOURCE}/src/aether-objects/obj/domain_buffer.h")
    message(FATAL_ERROR "Local Objects source must include AppTraverse's native graph patch chain; prepare it once before configuration")
  endif()
  set(APPTRAVERSE_AETHER_OBJECTS_PATCHES "")
endif()

# The published Objects revision already contains the tested native graph extension.
set(APPTRAVERSE_AETHER_OBJECTS_PATCHES "")
