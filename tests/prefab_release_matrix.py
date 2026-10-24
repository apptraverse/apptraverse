#!/usr/bin/env python3
"""Headless prefab-aware R0→R2 proof (NullPresenter / embedded backing)."""

import argparse
import json
import subprocess
import tempfile
from pathlib import Path

from application_evolution_matrix import CheckFailed, manifest
from event_downgrade_storage_checks import assert_a_refused_without_storage_change


def run(exe, state_dir, op):
    completed = subprocess.run(
        [str(exe), "--state-dir", str(state_dir), "--op", op],
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


def parse_ok(record):
    if not record or not record.get("ok"):
        raise CheckFailed("missing ok record: " + str(record))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--r0", type=Path, required=True)
    parser.add_argument("--r2", type=Path, required=True)
    parser.add_argument("--unknown-old", type=Path, required=True)
    parser.add_argument("--unknown-new", type=Path, required=True)
    parser.add_argument("--work-root", type=Path, required=True)
    args = parser.parse_args()
    args.work_root.mkdir(parents=True, exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix="prefab-release-", dir=args.work_root))
    state = root / "user"
    state.mkdir()
    print("Run directory: " + str(root), flush=True)

    code, rec, _ = run(args.r0, state, "r0-init")
    if code != 0:
        raise CheckFailed("R0 init failed")
    parse_ok(rec)

    code, rec, _ = run(args.r2, state, "r2-verify")
    parse_ok(rec)
    if rec.get("window_b_loaded"):
        raise CheckFailed("lazy window B loaded early")
    if int(rec.get("prefab_origin_a", 0)) != 60001:
        raise CheckFailed("prefab origin mismatch")
    if rec.get("alias_ok") != True:
        raise CheckFailed("preferences alias mismatch")
    if int(rec.get("width_a", 0)) != 640:
        raise CheckFailed("width default mismatch")
    if int(rec.get("posx_a", 0)) != 10:
        raise CheckFailed("posx prefab fallback mismatch")

    code, rec, _ = run(args.r2, state, "r2-set-posx-a")
    parse_ok(rec)
    if rec.get("user_s1_a") != True:
        raise CheckFailed("USER S1 not written after event")

    code, rec, _ = run(args.r2, state, "r2-load-lazy-b")
    parse_ok(rec)
    if int(rec.get("posx_b", 0)) != 10:
        raise CheckFailed("lazy B posx prefab fallback")
    if int(rec.get("lazy_load_count", 0)) < 1:
        raise CheckFailed("lazy load count")

    code, rec, _ = run(args.r2, state, "replay-check")
    parse_ok(rec)
    if rec.get("replay_idempotent") != True:
        raise CheckFailed("replay not idempotent")
    if rec.get("base_prefab_origin_ok") != True:
        raise CheckFailed("base prefab origin gap")

    code, rec, _ = run(args.r2, state, "direct-load-refuse")
    parse_ok(rec)

    unknown_state = root / "unknown"
    unknown_state.mkdir()
    code, b_write, _ = run(args.unknown_new, unknown_state, "write-unknown")
    parse_ok(b_write)
    probe_layer = f"{b_write['object_id']}/{b_write['probe_class_id']}/0"
    before = manifest(unknown_state)
    if probe_layer not in before:
        raise CheckFailed("unknown event layer missing")

    code, a_resave, _ = run(args.unknown_old, unknown_state, "resave")
    after = manifest(unknown_state)
    assert_a_refused_without_storage_change(
        before=before,
        after=after,
        obj_key_prefix=f"{b_write['object_id']}/",
        probe_layer=probe_layer,
        a_code=code,
        a_record=a_resave,
        forbidden_class_ids=set(),
    )

    code, b_load, _ = run(args.unknown_new, unknown_state, "load-verify")
    parse_ok(b_load)
    if int(b_load.get("payload", 0)) != 42:
        raise CheckFailed("unknown event payload mismatch")

    print("prefab_release_matrix OK", flush=True)


if __name__ == "__main__":
    main()
