#!/usr/bin/env python3
"""After B1, failed A2 must not corrupt storage; B must still load B1 graph."""

import argparse
import json
from pathlib import Path
import shutil
import subprocess
import tempfile

from application_evolution_matrix import CheckFailed, digest, manifest, unchanged

A1_BOUNDS = (201, 202, 401, 301)
A2_BOUNDS = (311, 322, 411, 311)
B_SUFFIX = " [lifecycle-b1]"
SURFACE1_OBJECT_ID = 3


def run_gui(exe, state_dir, automation, timeout):
    return subprocess.run(
        [str(exe), "--state-dir", str(state_dir), "--session-automation", automation],
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        timeout=timeout,
    )


def inspect(exe, state_dir, timeout):
    completed = subprocess.run(
        [str(exe), "--state-dir", str(state_dir), "--session-inspect"],
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        timeout=timeout,
    )
    for line in completed.stdout.splitlines():
        try:
            record = json.loads(line)
        except ValueError:
            continue
        if isinstance(record, dict) and "surfaces" in record:
            return completed.returncode, record
    return completed.returncode, {
        "load_ok": False,
        "load_error": completed.stderr.strip() or f"exit {completed.returncode}",
        "surfaces": [],
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--a", type=Path, required=True)
    parser.add_argument("--b", type=Path, required=True)
    parser.add_argument("--work-root", type=Path, required=True)
    parser.add_argument("--timeout", type=int, default=120)
    args = parser.parse_args()
    args.work_root.mkdir(parents=True, exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix="surfaces-corruption-guard-", dir=args.work_root))
    state = root / "state"
    state.mkdir()
    a_exe = args.a.resolve(strict=True)
    b_exe = args.b.resolve(strict=True)

    a1 = run_gui(
        a_exe,
        state,
        f"set-bounds={A1_BOUNDS[0]},{A1_BOUNDS[1]},{A1_BOUNDS[2]},{A1_BOUNDS[3]};shutdown",
        args.timeout,
    )
    if a1.returncode != 0:
        raise CheckFailed(f"A1 failed exit={a1.returncode}")

    b1 = run_gui(
        b_exe,
        state,
        f"set-suffix={B_SUFFIX};verify-title-suffix={B_SUFFIX};shutdown",
        args.timeout,
    )
    if b1.returncode != 0:
        raise CheckFailed(f"B1 failed exit={b1.returncode}")

    code, b1_rec = inspect(b_exe, state, args.timeout)
    if code != 0 or not b1_rec.get("load_ok"):
        raise CheckFailed("B1 inspect failed")
    manifest_after_b1 = manifest(state)
    v1_path = None
    class_id = b1_rec.get("class_id")
    if class_id is not None:
        v1_path = state / str(SURFACE1_OBJECT_ID) / str(class_id) / "1"
    v1_sha = digest(v1_path) if v1_path and v1_path.is_file() else None

    a2 = run_gui(
        a_exe,
        state,
        f"set-bounds={A2_BOUNDS[0]},{A2_BOUNDS[1]},{A2_BOUNDS[2]},{A2_BOUNDS[3]};shutdown",
        args.timeout,
    )
    manifest_after_a2 = manifest(state)
    unchanged(manifest_after_b1, manifest_after_a2, "A2 attempt must not mutate storage")

    if v1_sha is not None and digest(v1_path) != v1_sha:
        raise CheckFailed("A2 changed v1 layer bytes")

    code, b_after = inspect(b_exe, state, args.timeout)
    if code != 0 or not b_after.get("load_ok"):
        raise CheckFailed(
            "B must load graph after A2 refusal: "
            + b_after.get("load_error", "inspect failed")
        )
    surface = next(
        (s for s in b_after.get("surfaces", []) if s.get("object_id") == SURFACE1_OBJECT_ID),
        None,
    )
    if surface is None:
        raise CheckFailed("Surface1 missing after A2")
    if surface.get("window_title_suffix") != B_SUFFIX:
        raise CheckFailed("suffix lost after A2 attempt")
    for key, expected in zip(
        ("desktop_x", "desktop_y", "desktop_width", "desktop_height"), A1_BOUNDS
    ):
        if surface.get(key) != expected:
            raise CheckFailed(f"B1 bounds field {key} changed after A2 attempt")

    result = {
        "result": "PASS",
        "a2_gui_exit": a2.returncode,
        "storage_unchanged": True,
        "b_load_ok_after_a2": True,
    }
    (root / "corruption_guard_result.json").write_text(
        json.dumps(result, indent=2), encoding="utf-8"
    )
    print(json.dumps(result, indent=2))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except CheckFailed as err:
        print(json.dumps({"result": "FAIL", "detail": str(err)}, indent=2))
        raise SystemExit(1)
