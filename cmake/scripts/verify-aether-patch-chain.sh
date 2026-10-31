#!/usr/bin/env bash
# Applies the aether-objects patch list from cmake/aether_version.cmake, in its
# order, to a fresh clone of the pinned SHA.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
PATCH_DIR="$ROOT/cmake/patches"
VERSION_CMAKE="$ROOT/cmake/aether_version.cmake"
PIN="$(sed -n '/APPTRAVERSE_AETHER_OBJECTS_GIT_TAG/{n;s/[^0-9a-f]//gp;}' "$VERSION_CMAKE")"
mapfile -t PATCHES < <(sed -n '/^set(APPTRAVERSE_AETHER_OBJECTS_PATCH_NAMES/,/^)/{/^ /s/ //gp;}' "$VERSION_CMAKE")
test "${#PATCHES[@]}" -gt 0
WORK="${TMPDIR:-/tmp}/aether-patch-verify-$$"
DEST="$WORK/tree"
rm -rf "$WORK"
mkdir -p "$WORK"
git clone https://github.com/aethernetio/aether-objects.git "$DEST"
cd "$DEST"
git checkout "$PIN"
for p in "${PATCHES[@]}"; do
  echo "apply $p"
  patch -p1 --batch --forward < "$PATCH_DIR/aether-objects-$p.patch" || exit 1
done
grep -q 'cycle_detector.Remove(obj_id);' src/aether-objects/obj/domain.cpp
! grep -q 'pending_stored_layers' src/aether-objects/obj/domain.cpp
rm -rf "$WORK"
echo "full patch chain OK (${#PATCHES[@]} patches, pin $PIN)"
