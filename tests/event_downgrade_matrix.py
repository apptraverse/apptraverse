#!/usr/bin/env python3
"""Minimal A/B EXE matrix for unknown Event downgrade (no Surfaces / Win32)."""

import argparse
import json
import subprocess
import tempfile
from pathlib import Path

from application_evolution_matrix import CheckFailed, manifest
from event_downgrade_storage_checks import (
    assert_a_refused_without_storage_change,
    assert_b_inspect_unchanged,
)


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

    code, b_before_inspect, _ = run(args.b, state, "inspect")
    if code != 0 or not b_before_inspect or not b_before_inspect.get("ok"):
        raise CheckFailed("B inspect after write failed")
    before_class_ids = list(b_before_inspect.get("class_ids", []))

    code, a_load, _ = run(args.a, state, "resave")
    after = manifest(state)
    forbidden = set()
    if a_load and "other_event_class_id" in a_load:
        forbidden.add(int(a_load["other_event_class_id"]))
    if a_load and "other_event_alt_class_id" in a_load:
        forbidden.add(int(a_load["other_event_alt_class_id"]))

    assert_a_refused_without_storage_change(
        before=before,
        after=after,
        obj_key_prefix=obj_key_prefix,
        probe_layer=probe_layer,
        a_code=code,
        a_record=a_load,
        forbidden_class_ids=forbidden,
    )

    code, b_inspect, _ = run(args.b, state, "inspect")
    assert_b_inspect_unchanged(
        b_code=code,
        b_inspect=b_inspect,
        probe_class_id=probe_class_id,
        before_class_ids=before_class_ids,
    )

    print(json.dumps({"result": "PASS", "probe_class_id": probe_class_id}, indent=2))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except CheckFailed as err:
        print(json.dumps({"result": "FAIL", "detail": str(err)}, indent=2))
        raise SystemExit(1)
