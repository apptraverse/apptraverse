#!/usr/bin/env python3
"""Minimal A/B EXE matrix for unknown Event downgrade (no Surfaces / Win32)."""

import argparse
import json
import subprocess
import tempfile
from pathlib import Path

from application_evolution_matrix import CheckFailed, manifest


def run(exe, state_dir, op):
    completed = subprocess.run(
        [str(exe), "--state-dir", str(state_dir), "--op", op],
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        timeout=60,
    )
    record = None
    for line in completed.stdout.splitlines():
        try:
            record = json.loads(line)
        except ValueError:
            continue
    return completed.returncode, record, completed


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--a", type=Path, required=True)
    parser.add_argument("--b", type=Path, required=True)
    parser.add_argument("--work-root", type=Path, required=True)
    args = parser.parse_args()
    args.work_root.mkdir(parents=True, exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix="event-downgrade-", dir=args.work_root))
    state = root / "state"
    state.mkdir()
    print("Run directory: " + str(root), flush=True)

    code, b_write, _ = run(args.b, state, "write-unknown")
    if code != 0 or not b_write or not b_write.get("ok"):
        raise CheckFailed("B write-unknown failed")
    probe_class_id = b_write["probe_class_id"]
    obj_key_prefix = f"{b_write['object_id']}/"

    before = manifest(state)
    probe_layer = f"{obj_key_prefix}{probe_class_id}/0"
    if probe_layer not in before:
        raise CheckFailed("B did not persist probe event layer")

    code, a_load, _ = run(args.a, state, "resave")
    after = manifest(state)
    added_layers = sorted(k for k in after if k not in before and k.startswith(obj_key_prefix))

    if code == 0 and a_load and a_load.get("ok"):
        raise CheckFailed(
            "A must refuse LoadRoot/resave for storage with unknown Event class"
        )
    if a_load and a_load.get("ok"):
        raise CheckFailed("A reported successful load for unknown Event storage")
    if after != before:
        raise CheckFailed(
            "storage must be unchanged after A resave refusal; "
            f"diff keys: {sorted(set(after) ^ set(before))[:8]}"
        )
    if added_layers:
        raise CheckFailed(
            "A added foreign class layers on probe ObjId: " + ", ".join(added_layers)
        )
    if after.get(probe_layer) != before.get(probe_layer):
        raise CheckFailed("A changed serialized bytes of unknown probe layer")

    code, b_inspect, _ = run(args.b, state, "inspect")
    if code != 0 or not b_inspect or not b_inspect.get("ok"):
        raise CheckFailed("B inspect failed after A resave")
    class_ids = set(b_inspect.get("class_ids", []))
    if probe_class_id not in class_ids:
        raise CheckFailed("B lost probe class layer after A resave")
    if len(class_ids) != len(b_inspect.get("class_ids", [])):
        raise CheckFailed("duplicate class ids in storage listing")

    print(json.dumps({"result": "PASS", "probe_class_id": probe_class_id}, indent=2))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except CheckFailed as err:
        print(json.dumps({"result": "FAIL", "detail": str(err)}, indent=2))
        raise SystemExit(1)
