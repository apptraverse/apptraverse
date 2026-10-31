#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
PATCH_DIR="$ROOT/cmake/patches"
PIN=79283b667cabf689da1059d618fe2a22abf51db4
WORK="${TMPDIR:-/tmp}/aether-patch-verify-$$"
DEST="$WORK/tree"
rm -rf "$WORK"
mkdir -p "$WORK"
git clone https://github.com/aethernetio/aether-objects.git "$DEST"
cd "$DEST"
git checkout "$PIN"
PATCHES=(
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
)
for p in "${PATCHES[@]}"; do
  patch -p1 --batch --forward < "$PATCH_DIR/aether-objects-$p.patch" || exit 1
done
patch -p1 --batch --forward < "$PATCH_DIR/aether-objects-cold-load-root-flush-v6.patch" || exit 1
patch -p1 --batch --forward < "$PATCH_DIR/aether-objects-reset-object-load-state-v6r1.patch" || exit 1
patch -p1 --batch --forward < "$PATCH_DIR/aether-objects-deferred-load-completion-v6r2.patch" || exit 1
grep -q ResetObjectLoadState src/aether-objects/obj/domain.cpp
grep -q 'work.emplace_back' src/aether-objects/obj/domain.cpp
grep -q HasIncompleteStoredLayers src/aether-objects/obj/domain.h
grep -q DomainLoadRootState src/aether-objects/obj/domain.h
grep -q FlushIncompleteStoredLayers src/aether-objects/obj/domain.h
grep -q TryLoadStoredLayer src/aether-objects/obj/domain.cpp
rm -rf "$WORK"
echo "full patch chain OK"
