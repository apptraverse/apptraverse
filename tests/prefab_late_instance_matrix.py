#!/usr/bin/env python3
"""Late WindowC from Tool prefab after R0 bootstrap (iteration 10B)."""

import argparse
import json
import shutil
import subprocess
from pathlib import Path

from application_evolution_matrix import CheckFailed, manifest, unchanged

WINDOW_A = 61001
WINDOW_B = 61002
WINDOW_C_BUILTIN = 61003
MAIN_PREFAB = 60001
TOOL_PREFAB = 60002
MAIN_PREFAB_BASE = 60005
TOOL_PREFAB_BASE = 60006

TOOL_WIDTH = 480
TOOL_POSX = 20
MAIN_POSX = 10

FORBIDDEN_INSTANCE_IDS = frozenset(
    {
        WINDOW_A,
        WINDOW_B,
        WINDOW_C_BUILTIN,
        MAIN_PREFAB,
        TOOL_PREFAB,
        MAIN_PREFAB_BASE,
        TOOL_PREFAB_BASE,
    }
)


def run(exe, state_root, op):
    completed = subprocess.run(
        [str(exe), "--state-dir", str(state_root), "--op", op],
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        timeout=90,
    )
    record = None
    for line in completed.stdout.splitlines():
        try:
            record = json.loads(line)
        except ValueError:
            continue
    return completed.returncode, record, completed


def run_ok(exe, state_root, op):
    code, rec, comp = run(exe, state_root, op)
    if code != 0:
        raise CheckFailed(
            f"expected exit 0 for {op}, got {code}: stderr={comp.stderr!r} stdout={comp.stdout!r}"
        )
    if not rec or not rec.get("ok"):
        raise CheckFailed(f"missing ok for {op}: {rec}")
    return rec


def layer_key(obj_id: int, class_id: int, version: int) -> str:
    return f"{obj_id}/{class_id}/{version}"


def assert_no_s1(manifest_map, obj_id: int, class_id: int):
    key = layer_key(obj_id, class_id, 1)
    if key in manifest_map:
        raise CheckFailed(f"unexpected USER S1 layer {key}")


def assert_identity(rec, window_c_id: int):
    if int(rec.get("window_a_instance_id", 0)) != WINDOW_A:
        raise CheckFailed("WindowA instance ObjId")
    if int(rec.get("window_a_prefab_id", 0)) != MAIN_PREFAB:
        raise CheckFailed("WindowA prefab ObjId")
    if int(rec.get("window_c_instance_id", 0)) != window_c_id:
        raise CheckFailed("WindowC instance ObjId changed")
    if int(rec.get("window_c_prefab_id", 0)) != TOOL_PREFAB:
        raise CheckFailed("WindowC prefab ObjId")
    if not rec.get("alias_ok"):
        raise CheckFailed("alias must point to WindowA")
    if rec.get("window_b_loaded"):
        raise CheckFailed("WindowB must stay lazy")


def assert_unique_ids(rec):
    inst = int(rec["window_c_instance_id"])
    base = int(rec["instance_base_id"])
    if inst in FORBIDDEN_INSTANCE_IDS or base in FORBIDDEN_INSTANCE_IDS:
        raise CheckFailed("WindowC/base ObjId collides with built-in ids")
    if inst == base:
        raise CheckFailed("instance and base ObjId must differ")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--r0", required=True)
    parser.add_argument("--r1", required=True)
    parser.add_argument("--r2", required=True)
    parser.add_argument("--work-root", required=True)
    args = parser.parse_args()

    r0 = Path(args.r0)
    r1 = Path(args.r1)
    r2 = Path(args.r2)
    work_root = Path(args.work_root)
    state_root = work_root / "state"
    user_root = state_root / "user"

    if state_root.exists():
        shutil.rmtree(state_root)
    state_root.mkdir(parents=True)
    user_root.mkdir(parents=True, exist_ok=True)

    run_ok(r0, state_root, "r0-init")

    created = run_ok(r1, state_root, "create-tool-window")
    window_c_id = int(created["window_c_instance_id"])
    window_c_base_id = int(created["window_c_instance_base_id"])
    if window_c_id in FORBIDDEN_INSTANCE_IDS:
        raise CheckFailed("generated WindowC id is reserved")
    if window_c_base_id in FORBIDDEN_INSTANCE_IDS:
        raise CheckFailed("generated base id is reserved")

    rec = run_ok(r1, state_root, "inspect-window-c")
    assert_identity(rec, window_c_id)
    assert_unique_ids(rec)
    class_id = int(rec["window_class_id"])
    if int(rec.get("width_c", 0)) != TOOL_WIDTH:
        raise CheckFailed("R1 cold load: width_c")
    if rec.get("user_s1_instance") or rec.get("user_s1_base"):
        raise CheckFailed("R1: USER S1 must be absent for WindowC")
    user_manifest = manifest(user_root)
    assert_no_s1(user_manifest, window_c_id, class_id)
    assert_no_s1(user_manifest, window_c_base_id, class_id)

    before_r2 = manifest(user_root)
    rec = run_ok(r2, state_root, "inspect-window-c")
    after_r2_read = manifest(user_root)
    unchanged(before_r2, after_r2_read, "R2 read-only inspect")
    assert_identity(rec, window_c_id)
    if int(rec.get("width_c", 0)) != TOOL_WIDTH:
        raise CheckFailed("R2 cold load: width_c")
    if int(rec.get("posx_c", 0)) != TOOL_POSX:
        raise CheckFailed("R2 cold load: prefab S1 posx on WindowC")
    if int(rec.get("posx_a", 0)) != MAIN_POSX:
        raise CheckFailed("R2: WindowA main prefab posx")
    if rec.get("user_s1_instance") or rec.get("user_s1_base"):
        raise CheckFailed("R2: USER S1 must stay absent for WindowC")

    run_ok(r1, state_root, "touch-save-workspace")

    rec = run_ok(r2, state_root, "inspect-window-c")
    assert_identity(rec, window_c_id)
    if int(rec.get("width_c", 0)) != TOOL_WIDTH:
        raise CheckFailed("R2 after R1 touch-save: width_c")
    if int(rec.get("posx_c", 0)) != TOOL_POSX:
        raise CheckFailed("R2 after R1 touch-save: posx_c")

    print("prefab_late_instance_matrix OK", flush=True)


if __name__ == "__main__":
    try:
        main()
    except CheckFailed as exc:
        print(f"FAIL: {exc}", flush=True)
        raise SystemExit(1) from exc
