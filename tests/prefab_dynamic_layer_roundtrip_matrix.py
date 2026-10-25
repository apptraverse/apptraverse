#!/usr/bin/env python3
"""R0→R1→R2→R1→R2 on one USER: S0 width survives R2 S1; R1 SaveRoot preserves S1 (10A)."""

import argparse
import json
import shutil
import subprocess
from pathlib import Path

from application_evolution_matrix import CheckFailed, manifest, unchanged

MAIN_LABEL_B = "main-title-B"
TOOL_LABEL = "tool-title"
MAIN_LABEL_CONST = 60011
TOOL_LABEL_CONST = 60012

WINDOW_A = 61001
MAIN_PREFAB = 60001


def run(exe, state_root, op, *, active_release=None, width=None):
    cmd = [str(exe), "--state-dir", str(state_root), "--op", op]
    if active_release is not None:
        cmd.extend(["--active-release", str(active_release)])
    if width is not None:
        cmd.extend(["--width", str(width)])
    completed = subprocess.run(
        cmd,
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


def run_ok(exe, state_root, op, **kwargs):
    code, rec, comp = run(exe, state_root, op, **kwargs)
    if code != 0:
        raise CheckFailed(
            f"expected exit 0 for {op}, got {code}: stderr={comp.stderr!r} stdout={comp.stdout!r}"
        )
    if not rec or not rec.get("ok"):
        raise CheckFailed(f"missing ok for {op}: {rec}")
    return rec


def layer_key(obj_id: int, class_id: int, version: int) -> str:
    return f"{obj_id}/{class_id}/{version}"


def assert_no_layer(manifest_map, obj_id: int, class_id: int, version: int):
    key = layer_key(obj_id, class_id, version)
    if key in manifest_map:
        raise CheckFailed(f"unexpected USER layer {key}")


def assert_has_layer(manifest_map, obj_id: int, class_id: int, version: int):
    key = layer_key(obj_id, class_id, version)
    if key not in manifest_map:
        raise CheckFailed(f"missing USER layer {key}")


def assert_no_const_in_user(manifest_map, obj_id):
    prefix = f"{obj_id}/"
    for key in manifest_map:
        if key.startswith(prefix):
            raise CheckFailed(f"const layer in USER: {key}")


def assert_object_identity(rec, *, window_b_loaded=False):
    if int(rec.get("window_a_instance_id", 0)) != WINDOW_A:
        raise CheckFailed("WindowA instance ObjId")
    if int(rec.get("window_a_prefab_id", 0)) != MAIN_PREFAB:
        raise CheckFailed("WindowA prefab ObjId")
    if not rec.get("alias_ok"):
        raise CheckFailed("alias must point to WindowA")
    if bool(rec.get("window_b_loaded")) != window_b_loaded:
        raise CheckFailed("WindowB lazy load state")


def s1_layer_entries(manifest_map, obj_ids, class_id):
    out = {}
    for oid in obj_ids:
        key = layer_key(oid, class_id, 1)
        if key in manifest_map:
            out[key] = manifest_map[key]
    return out


def generate_package(tool: Path, out_path: Path) -> None:
    out_path.parent.mkdir(parents=True, exist_ok=True)
    code = subprocess.run(
        [str(tool), str(out_path)],
        capture_output=True,
        text=True,
        timeout=60,
        check=False,
    ).returncode
    if code != 0:
        raise CheckFailed("package tool failed")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--r0", required=True)
    parser.add_argument("--r1", required=True)
    parser.add_argument("--r2", required=True)
    parser.add_argument("--package-tool", required=True)
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
    generate_package(Path(args.package_tool), state_root / "1" / "supply.bin")
    run_ok(r1, state_root, "set-active-release", active_release=1)

    run_ok(r1, state_root, "set-width-a", width=700)
    rec = run_ok(r1, state_root, "read-user-equivalence")
    assert_object_identity(rec)
    if rec.get("main_label") != MAIN_LABEL_B:
        raise CheckFailed("R1 cold load: main label")
    if rec.get("tool_label") != TOOL_LABEL:
        raise CheckFailed("R1 cold load: tool label")
    if int(rec.get("width_a", 0)) != 700:
        raise CheckFailed("R1 cold load: width")
    assert_no_const_in_user(manifest(user_root), MAIN_LABEL_CONST)
    assert_no_const_in_user(manifest(user_root), TOOL_LABEL_CONST)

    before_readonly = manifest(user_root)
    rec = run_ok(r2, state_root, "d1-r2-readonly-a")
    after_readonly = manifest(user_root)
    unchanged(before_readonly, after_readonly, "R2 readonly load")
    class_id = int(rec["window_class_id"])
    if int(rec.get("width_a", 0)) != 700:
        raise CheckFailed("R2 readonly: width")
    if int(rec.get("posx_a", 0)) != 10:
        raise CheckFailed("R2 readonly: prefab S1 posx")
    if rec.get("user_s1_instance_present"):
        raise CheckFailed("R2 readonly: instance S1 must be absent")
    if rec.get("user_s1_base_present"):
        raise CheckFailed("R2 readonly: base S1 must be absent")
    assert_no_layer(before_readonly, WINDOW_A, class_id, 1)

    run_ok(r2, state_root, "d1-r2-delta-a")
    rec = run_ok(r2, state_root, "d1-r2-inspect-a")
    if int(rec.get("width_a", 0)) != 700:
        raise CheckFailed("after R2 delta: width")
    if int(rec.get("posx_a", 0)) != 15:
        raise CheckFailed("after R2 delta: posx")
    if int(rec.get("base_posx", 0)) != 10:
        raise CheckFailed("after R2 delta: historical base S1")
    inst_base = int(rec["instance_base_id"])
    user_manifest = manifest(user_root)
    assert_has_layer(user_manifest, WINDOW_A, class_id, 1)
    assert_has_layer(user_manifest, inst_base, class_id, 1)
    s1_before_r1 = s1_layer_entries(user_manifest, [WINDOW_A, inst_base], class_id)

    run_ok(r1, state_root, "set-width-a", width=720)
    user_after_r1 = manifest(user_root)
    s1_after_r1 = s1_layer_entries(user_after_r1, [WINDOW_A, inst_base], class_id)
    if s1_before_r1 != s1_after_r1:
        raise CheckFailed("R1 SaveRoot must not alter USER S1 layers byte-for-byte")
    rec = run_ok(r1, state_root, "read-user-equivalence")
    assert_object_identity(rec)
    if int(rec.get("width_a", 0)) != 720:
        raise CheckFailed("R1 after width 720")

    rec = run_ok(r2, state_root, "read-user-equivalence")
    assert_object_identity(rec)
    if int(rec.get("width_a", 0)) != 720:
        raise CheckFailed("R2 final load: width")
    if int(rec.get("posx_a", 0)) != 15:
        raise CheckFailed("R2 final load: posx")

    rec = run_ok(r2, state_root, "d1-r2-replay-a")
    if int(rec.get("materialized_posx", 0)) != 15:
        raise CheckFailed("replay: materialized posx")
    if int(rec.get("replay_posx", 0)) != 15:
        raise CheckFailed("replay: posx")
    if int(rec.get("replay_twice", 0)) != 15:
        raise CheckFailed("replay: posx idempotent")
    if int(rec.get("materialized_width", 0)) != 720:
        raise CheckFailed("replay: materialized width")
    if int(rec.get("replay_width", 0)) != 720:
        raise CheckFailed("replay: width")
    if int(rec.get("replay_width_twice", 0)) != 720:
        raise CheckFailed("replay: width idempotent")

    print("prefab_dynamic_layer_roundtrip_matrix OK", flush=True)


if __name__ == "__main__":
    try:
        main()
    except CheckFailed as exc:
        print(f"FAIL: {exc}", flush=True)
        raise SystemExit(1) from exc
