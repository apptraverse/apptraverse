#!/usr/bin/env python3
"""Generate aether-objects-a1r1-opener-lifetime-diag.patch from a v6r1-patched tree."""
from __future__ import annotations

import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PATCH_EXE = Path(r"C:\Program Files\Git\usr\bin\patch.exe")
PATCH_OUT = ROOT / "cmake/patches/aether-objects-a1r1-opener-lifetime-diag.patch"
PIN = "79283b667cabf689da1059d618fe2a22abf51db4"
PATCH_DIR = ROOT / "cmake/patches"
BASE_PATCHES = [
    "local-persistent-load",
    "domain-storage-access",
    "reject-unknown-stored-class",
    "objptr-reset-flags",
    "force-materialize",
    "should-save-version",
    "origin-facet",
    "obj-facet-factory",
    "domain-registered-objects",
    "clone-save-remap",
    "clone-canonical-state",
    "clone-reference-visitor",
    "emscripten-domain-include",
    "nested-obj-archive-deseri",
]


def run(cmd: list[str], cwd: Path, input_bytes: bytes | None = None) -> None:
    subprocess.run(cmd, cwd=cwd, check=True, input=input_bytes)


def main() -> int:
    import tempfile

    work = Path(tempfile.mkdtemp(prefix="aether-diag-gen-"))
    dest = work / "tree"
    run(["git", "clone", "-q", "https://github.com/aethernetio/aether-objects.git", str(dest)], cwd=work)
    run(["git", "checkout", "-q", PIN], cwd=dest)
    for name in BASE_PATCHES:
        run(
            [str(PATCH_EXE), "-p1", "--batch", "--forward"],
            cwd=dest,
            input_bytes=(PATCH_DIR / f"aether-objects-{name}.patch").read_bytes(),
        )
    run(
        [str(PATCH_EXE), "-p1", "--batch", "--forward"],
        cwd=dest,
        input_bytes=(PATCH_DIR / "aether-objects-cold-load-root-flush-v6.patch").read_bytes(),
    )
    run(
        [str(PATCH_EXE), "-p1", "--batch", "--forward"],
        cwd=dest,
        input_bytes=(PATCH_DIR / "aether-objects-reset-object-load-state-v6r1.patch").read_bytes(),
    )

    domain_path = dest / "src/aether-objects/obj/domain.cpp"
    obj_path = dest / "src/aether-objects/obj/obj.cpp"
    domain_before = domain_path.read_text()
    obj_before = obj_path.read_text()
    domain = domain_before
    obj = obj_before

    weak_domain = """
#if defined(__GNUC__)
extern "C" void apptraverse_a1r1_domain_construct_obj(void* domain, void* obj,
                                                      unsigned obj_id)
    __attribute__((weak));
extern "C" void apptraverse_a1r1_domain_remove_object(void* domain, void* obj,
                                                      unsigned obj_id)
    __attribute__((weak));
extern "C" void apptraverse_a1r1_try_load_stored_layer(
    void* domain_graph, unsigned storage_id, unsigned class_id, int loaded,
    int cycle_deferred, int is_err, int err_code) __attribute__((weak));
#else
extern "C" void apptraverse_a1r1_domain_construct_obj(void* domain, void* obj,
                                                      unsigned obj_id);
extern "C" void apptraverse_a1r1_domain_remove_object(void* domain, void* obj,
                                                      unsigned obj_id);
extern "C" void apptraverse_a1r1_try_load_stored_layer(
    void* domain_graph, unsigned storage_id, unsigned class_id, int loaded,
    int cycle_deferred, int is_err, int err_code);
#endif
"""

    domain = domain.replace(
        '#include "aether-objects/log.h"\n\nnamespace ae {\n',
        '#include "aether-objects/log.h"\n\n' + weak_domain + "namespace ae {\n",
        1,
    )
    domain = domain.replace(
        "  if (!result) {\n    return true;\n  }\n  if (result->IsErr()) {\n    ResetObjectLoadState(storage_id);\n",
        "  if (!result) {\n    if (apptraverse_a1r1_try_load_stored_layer) {\n"
        "      apptraverse_a1r1_try_load_stored_layer(this, storage_id.id(), class_id, 0,\n"
        "                                             1, 0, 0);\n    }\n    return true;\n  }\n"
        "  if (result->IsErr()) {\n    if (apptraverse_a1r1_try_load_stored_layer) {\n"
        "      apptraverse_a1r1_try_load_stored_layer(\n"
        "          this, storage_id.id(), class_id, 0, 0, 1,\n"
        "          static_cast<int>(result->error().error_code));\n    }\n"
        "    ResetObjectLoadState(storage_id);\n",
        1,
    )
    domain = domain.replace(
        "  loaded = true;\n  return true;\n}\n\nvoid DomainGraph::EnqueuePendingStoredLayers(\n",
        "  loaded = true;\n  if (apptraverse_a1r1_try_load_stored_layer) {\n"
        "    apptraverse_a1r1_try_load_stored_layer(this, storage_id.id(), class_id, 1,\n"
        "                                           0, 0, 0);\n  }\n  return true;\n}\n\n"
        "void DomainGraph::EnqueuePendingStoredLayers(\n",
        1,
    )
    domain = domain.replace(
        "  AddObject(obj_id, o);\n  return o;\n}\n\nbool Domain::IsLast(uint32_t class_id) const {\n",
        "  AddObject(obj_id, o);\n  if (apptraverse_a1r1_domain_construct_obj) {\n"
        "    apptraverse_a1r1_domain_construct_obj(this, o.get(), obj_id.id());\n  }\n  return o;\n}\n\n"
        "bool Domain::IsLast(uint32_t class_id) const {\n",
        1,
    )
    domain = domain.replace(
        "  if (ptr == nullptr || !ptr->registered_objects_) {\n    return;\n  }\n"
        "  ptr->registered_objects_->Remove(ptr->obj_id.id());\n}\n\nstd::vector<std::uint32_t> Domain::KnownStoredClasses(ObjId id) {\n",
        "  if (ptr == nullptr || !ptr->registered_objects_) {\n    return;\n  }\n"
        "  if (apptraverse_a1r1_domain_remove_object) {\n"
        "    apptraverse_a1r1_domain_remove_object(this, ptr, ptr->obj_id.id());\n  }\n"
        "  ptr->registered_objects_->Remove(ptr->obj_id.id());\n}\n\n"
        "std::vector<std::uint32_t> Domain::KnownStoredClasses(ObjId id) {\n",
        1,
    )

    weak_obj = """
#if defined(__GNUC__)
extern "C" void apptraverse_a1r1_obj_dtor(void* domain, void* obj, unsigned obj_id)
    __attribute__((weak));
#else
extern "C" void apptraverse_a1r1_obj_dtor(void* domain, void* obj, unsigned obj_id);
#endif
"""
    obj = obj.replace(
        '#include "aether-objects/obj/registry.h"\n\nnamespace ae {\n',
        '#include "aether-objects/obj/registry.h"\n\n' + weak_obj + "namespace ae {\n",
        1,
    )
    obj = obj.replace(
        "Obj::~Obj() {\n  if (registered_objects_) {\n",
        "Obj::~Obj() {\n  if (apptraverse_a1r1_obj_dtor) {\n"
        "    apptraverse_a1r1_obj_dtor(domain, this, obj_id.id());\n  }\n"
        "  if (registered_objects_) {\n",
        1,
    )

    import difflib

    def unified(before: str, after: str, path: str) -> str:
        lines = difflib.unified_diff(
            before.splitlines(keepends=True),
            after.splitlines(keepends=True),
            fromfile=f"a/{path}",
            tofile=f"b/{path}",
        )
        return "".join(lines)

    diff = (
        "diff --git a/src/aether-objects/obj/domain.cpp b/src/aether-objects/obj/domain.cpp\n"
        "--- a/src/aether-objects/obj/domain.cpp\n"
        "+++ b/src/aether-objects/obj/domain.cpp\n"
        + unified(domain_before, domain, "src/aether-objects/obj/domain.cpp").split("\n", 1)[-1]
        + "diff --git a/src/aether-objects/obj/obj.cpp b/src/aether-objects/obj/obj.cpp\n"
        "--- a/src/aether-objects/obj/obj.cpp\n"
        "+++ b/src/aether-objects/obj/obj.cpp\n"
        + unified(obj_before, obj, "src/aether-objects/obj/obj.cpp").split("\n", 1)[-1]
    )
    PATCH_OUT.write_text(diff.replace("\r\n", "\n"))
    print(f"Wrote {PATCH_OUT} ({len(diff.splitlines())} lines)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
