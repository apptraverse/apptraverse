#!/usr/bin/env python3
"""Late WindowC from Tool prefab: create, S1 default, user S1 delta (10B+10C)."""

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
MAIN_LABEL_CONST = 60011
TOOL_LABEL_CONST = 60012

TOOL_WIDTH = 480
TOOL_POSX = 20
TOOL_POSX_USER = 27
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


def assert_has_s1(manifest_map, obj_id: int, class_id: int):
    key = layer_key(obj_id, class_id, 1)
    if key not in manifest_map:
        raise CheckFailed(f"missing USER S1 layer {key}")


def assert_no_const_in_user(manifest_map, obj_id: int):
    prefix = f"{obj_id}/"
    for key in manifest_map:
        if key.startswith(prefix):
            raise CheckFailed(f"const layer in USER: {key}")


def assert_identity(rec, window_c_id: int, window_c_base_id: int):
    if int(rec.get("window_a_instance_id", 0)) != WINDOW_A:
        raise CheckFailed("WindowA instance ObjId")
    if int(rec.get("window_a_prefab_id", 0)) != MAIN_PREFAB:
        raise CheckFailed("WindowA prefab ObjId")
    if int(rec.get("window_c_instance_id", 0)) != window_c_id:
        raise CheckFailed("WindowC instance ObjId changed")
    if int(rec.get("window_c_prefab_id", 0)) != TOOL_PREFAB:
        raise CheckFailed("WindowC prefab ObjId")
    if int(rec.get("instance_base_id", 0)) != window_c_base_id:
        raise CheckFailed("WindowC historical base ObjId changed")
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
    assert_identity(rec, window_c_id, window_c_base_id)
    assert_unique_ids(rec)
    class_id = int(rec["window_class_id"])
    if int(rec.get("width_c", 0)) != TOOL_WIDTH:
        raise CheckFailed("R1 cold load: width_c")
    if rec.get("user_s1_instance") or rec.get("user_s1_base"):
        raise CheckFailed("R1: USER S1 must be absent for WindowC")
    user_manifest = manifest(user_root)
    assert_no_s1(user_manifest, window_c_id, class_id)
    assert_no_s1(user_manifest, window_c_base_id, class_id)
    assert_no_const_in_user(user_manifest, MAIN_LABEL_CONST)
    assert_no_const_in_user(user_manifest, TOOL_LABEL_CONST)

    before_r2 = manifest(user_root)
    rec = run_ok(r2, state_root, "inspect-window-c")
    after_r2_read = manifest(user_root)
    unchanged(before_r2, after_r2_read, "R2 read-only inspect")
    assert_identity(rec, window_c_id, window_c_base_id)
    if int(rec.get("width_c", 0)) != TOOL_WIDTH:
        raise CheckFailed("R2 cold load: width_c")
    if int(rec.get("posx_c", 0)) != TOOL_POSX:
        raise CheckFailed("R2 cold load: prefab S1 posx on WindowC")
    if int(rec.get("posx_a", 0)) != MAIN_POSX:
        raise CheckFailed("R2 readonly: WindowA main prefab posx")
    if rec.get("user_s1_instance") or rec.get("user_s1_base"):
        raise CheckFailed("R2 readonly: USER S1 must stay absent for WindowC")

    rec = run_ok(r2, state_root, "delta-posx-c")
    if int(rec.get("posx_c", 0)) != TOOL_POSX_USER:
        raise CheckFailed("R2 delta-posx-c: posx")

    rec = run_ok(r2, state_root, "inspect-window-c")
    assert_identity(rec, window_c_id, window_c_base_id)
    if int(rec.get("posx_a", 0)) != MAIN_POSX:
        raise CheckFailed("R2 after delta: WindowA posx")
    if int(rec.get("width_c", 0)) != TOOL_WIDTH:
        raise CheckFailed("R2 after delta: width_c")
    if int(rec.get("posx_c", 0)) != TOOL_POSX_USER:
        raise CheckFailed("R2 after delta: posx_c")
    if int(rec.get("base_posx_c", 0)) != TOOL_POSX:
        raise CheckFailed("R2 after delta: historical base S1 posx")
    if int(rec.get("journal_len_c", 0)) != 1:
        raise CheckFailed("R2 after delta: journal_len_c")
    if not rec.get("user_s1_instance") or not rec.get("user_s1_base"):
        raise CheckFailed("R2 after delta: USER S1 on instance and base")
    user_manifest = manifest(user_root)
    assert_has_s1(user_manifest, window_c_id, class_id)
    assert_has_s1(user_manifest, window_c_base_id, class_id)
    assert_no_s1(user_manifest, WINDOW_A, class_id)
    assert_no_s1(user_manifest, WINDOW_B, class_id)

    run_ok(r1, state_root, "touch-save-workspace")

    rec = run_ok(r2, state_root, "inspect-window-c")
    assert_identity(rec, window_c_id, window_c_base_id)
    if int(rec.get("posx_a", 0)) != MAIN_POSX:
        raise CheckFailed("R2 after R1 rollback: WindowA posx")
    if int(rec.get("width_c", 0)) != TOOL_WIDTH:
        raise CheckFailed("R2 after R1 touch-save: width_c")
    if int(rec.get("posx_c", 0)) != TOOL_POSX_USER:
        raise CheckFailed("R2 after R1 touch-save: posx_c")
    if int(rec.get("base_posx_c", 0)) != TOOL_POSX:
        raise CheckFailed("R2 after R1 rollback: base S1 preserved")
    if not rec.get("user_s1_instance") or not rec.get("user_s1_base"):
        raise CheckFailed("R2 after R1 rollback: USER S1 layers")

    rec = run_ok(r2, state_root, "replay-window-c")
    if int(rec.get("materialized_posx", 0)) != TOOL_POSX_USER:
        raise CheckFailed("replay: materialized posx")
    if int(rec.get("replay_posx", 0)) != TOOL_POSX_USER:
        raise CheckFailed("replay: posx")
    if int(rec.get("replay_posx_twice", 0)) != TOOL_POSX_USER:
        raise CheckFailed("replay: posx idempotent")
    if int(rec.get("materialized_width", 0)) != TOOL_WIDTH:
        raise CheckFailed("replay: materialized width")
    if int(rec.get("replay_width", 0)) != TOOL_WIDTH:
        raise CheckFailed("replay: width")

    print("prefab_late_instance_matrix OK", flush=True)


if __name__ == "__main__":
    try:
        main()
    except CheckFailed as exc:
        print(f"FAIL: {exc}", flush=True)
        raise SystemExit(1) from exc
