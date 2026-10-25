#!/usr/bin/env python3
"""R0+R1 BIN vs R1 EXE on one USER (iteration 09A). Requires existing USER from R0."""

import argparse
import json
import shutil
import subprocess
from pathlib import Path

from application_evolution_matrix import CheckFailed, manifest

MAIN_LABEL_B = "main-title-B"
TOOL_LABEL = "tool-title"
MAIN_LABEL_CONST = 60011
TOOL_LABEL_CONST = 60012

SNAPSHOT_KEYS = (
    "main_label",
    "tool_label",
    "width_a",
    "posx_a",
    "window_a_instance_id",
    "window_a_prefab_id",
    "window_b_instance_id",
    "window_b_prefab_id",
    "window_b_loaded",
    "journal_len_a",
    "alias_ok",
    "prefab_base_id",
)


def run(exe, state_root, op, active_release=None):
    cmd = [str(exe), "--state-dir", str(state_root), "--op", op]
    if active_release is not None:
        cmd.extend(["--active-release", str(active_release)])
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


def run_ok(exe, state_root, op, active_release=None):
    code, rec, comp = run(exe, state_root, op, active_release)
    if code != 0:
        raise CheckFailed(
            f"expected exit 0 for {op}, got {code}: stderr={comp.stderr!r} stdout={comp.stdout!r}"
        )
    if not rec or not rec.get("ok"):
        raise CheckFailed(f"missing ok for {op}: {rec}")
    return rec


def set_active(exe, state_root, value):
    run_ok(exe, state_root, "set-active-release", active_release=value)


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


def snapshot_subset(rec):
    return {k: rec.get(k) for k in SNAPSHOT_KEYS}


def assert_snapshots_equal(a, b):
    if a != b:
        raise CheckFailed(f"snapshot mismatch: {a} vs {b}")


def assert_no_const_in_user(manifest_map, obj_id):
    prefix = f"{obj_id}/"
    for key in manifest_map:
        if key.startswith(prefix):
            raise CheckFailed(f"const layer in USER: {key}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--r0", required=True)
    parser.add_argument("--r1", required=True)
    parser.add_argument("--package-tool", required=True)
    parser.add_argument("--work-root", required=True)
    args = parser.parse_args()

    r0 = Path(args.r0)
    r1 = Path(args.r1)
    work_root = Path(args.work_root)
    state_root = work_root / "state"
    user_root = state_root / "user"

    if state_root.exists():
        shutil.rmtree(state_root)
    state_root.mkdir(parents=True)
    user_root.mkdir(parents=True, exist_ok=True)

    run_ok(r0, state_root, "r0-init")
    manifest_after_init = manifest(user_root)

    generate_package(Path(args.package_tool), state_root / "1" / "supply.bin")
    set_active(r0, state_root, 1)

    r0_a = run_ok(r0, state_root, "read-user-equivalence")
    if r0_a.get("main_label") != MAIN_LABEL_B:
        raise CheckFailed("R0 path: main label")
    if r0_a.get("tool_label") != TOOL_LABEL:
        raise CheckFailed("R0 path: tool label")
    if r0_a.get("package_applied") is not True:
        raise CheckFailed("R0 path: expected package applied")

    r1_b = run_ok(r1, state_root, "read-user-equivalence")
    if r1_b.get("main_label") != MAIN_LABEL_B:
        raise CheckFailed("R1 EXE: main label")
    if r1_b.get("tool_label") != TOOL_LABEL:
        raise CheckFailed("R1 EXE: tool label")
    if r1_b.get("package_applied") is True:
        raise CheckFailed("R1 EXE must use embedded supply, not re-apply BIN")
    if int(r1_b.get("embedded_release", 0)) != 1:
        raise CheckFailed("R1 EXE embedded release")

    snap_r0 = snapshot_subset(r0_a)
    snap_r1 = snapshot_subset(r1_b)
    assert_snapshots_equal(snap_r0, snap_r1)

    manifest_mid = manifest(user_root)
    assert_no_const_in_user(manifest_mid, MAIN_LABEL_CONST)
    assert_no_const_in_user(manifest_mid, TOOL_LABEL_CONST)

    r0_c = run_ok(r0, state_root, "read-user-equivalence")
    r1_d = run_ok(r1, state_root, "read-user-equivalence")
    assert_snapshots_equal(snapshot_subset(r0_c), snap_r0)
    assert_snapshots_equal(snapshot_subset(r1_d), snap_r1)

    if manifest(user_root) != manifest_after_init:
        raise CheckFailed("USER manifest changed after read-only ops")

    print("prefab_release_r1_user_equivalence_matrix OK", flush=True)


if __name__ == "__main__":
    try:
        main()
    except CheckFailed as exc:
        print(f"FAIL: {exc}", flush=True)
        raise SystemExit(1) from exc
