#!/usr/bin/env python3
"""R1 must not compact S1 journal effects it cannot promote into historical base (11B.1).

11B.2 guard matrix (R1 must not compact when target/base have unknown stored versions).
"""

import argparse
import json
import shutil
import subprocess
from pathlib import Path

from application_evolution_matrix import CheckFailed, manifest

WINDOW_A = 61001
WINDOW_B = 61002
MAIN_LABEL_CONST = 60011
TOOL_LABEL_CONST = 60012

TOOL_WIDTH = 480
TOOL_POSX = 20
TOOL_POSX_USER = 27
MAIN_POSX = 10


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


def assert_has_s1(manifest_map, obj_id: int, class_id: int):
    key = layer_key(obj_id, class_id, 1)
    if key not in manifest_map:
        raise CheckFailed(f"missing USER S1 layer {key}")


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

    run_ok(r2, state_root, "delta-posx-c")
    rec = run_ok(r2, state_root, "inspect-window-c")
    class_id = int(rec["window_class_id"])
    if int(rec.get("posx_c", 0)) != TOOL_POSX_USER:
        raise CheckFailed("pre-R1 compact: posx_c")
    if int(rec.get("journal_len_c", -1)) != 1:
        raise CheckFailed("pre-R1 compact: journal_len")
    if int(rec.get("base_posx_c", 0)) != TOOL_POSX:
        raise CheckFailed("pre-R1 compact: base_posx")

    compact_r1 = run_ok(r1, state_root, "compact-window-c")
    if int(compact_r1.get("journal_len_c", -1)) != 1:
        raise CheckFailed(
            "R1 compact must not collapse S1 journal (journal_len after="
            + str(compact_r1.get("journal_len_c"))
            + ")"
        )

    rec = run_ok(r2, state_root, "inspect-window-c")
    if int(rec.get("window_c_instance_id", 0)) != window_c_id:
        raise CheckFailed("instance ObjId changed")
    if int(rec.get("instance_base_id", 0)) != window_c_base_id:
        raise CheckFailed("base ObjId changed")
    if int(rec.get("width_c", 0)) != TOOL_WIDTH:
        raise CheckFailed("width_c")
    if int(rec.get("posx_a", 0)) != MAIN_POSX:
        raise CheckFailed("WindowA posx")
    if rec.get("window_b_loaded"):
        raise CheckFailed("WindowB must stay lazy")
    if int(rec.get("journal_len_c", -1)) != 1:
        raise CheckFailed("R2 inspect: journal_len must stay 1")
    if int(rec.get("base_posx_c", 0)) != TOOL_POSX:
        raise CheckFailed("R2 inspect: historical base S1 must stay 20")
    if int(rec.get("posx_c", 0)) != TOOL_POSX_USER:
        raise CheckFailed("R2 inspect: instance posx must stay 27")
    if not rec.get("user_s1_instance"):
        raise CheckFailed("R2 inspect: USER S1 on instance required")
    if not rec.get("user_s1_base"):
        raise CheckFailed("R2 inspect: USER S1 on historical base required")

    user_manifest = manifest(user_root)
    assert_has_s1(user_manifest, window_c_id, class_id)
    assert_has_s1(user_manifest, window_c_base_id, class_id)

    for _ in range(2):
        replay = run_ok(r2, state_root, "replay-window-c")
        if int(replay.get("replay_posx", 0)) != TOOL_POSX_USER:
            raise CheckFailed("replay: posx")
        if int(replay.get("replay_posx_twice", 0)) != TOOL_POSX_USER:
            raise CheckFailed("replay: idempotent posx")

    print("prefab_old_exe_compaction_guard_matrix OK", flush=True)


if __name__ == "__main__":
    try:
        main()
    except CheckFailed as exc:
        print(f"FAIL: {exc}", flush=True)
        raise SystemExit(1) from exc
