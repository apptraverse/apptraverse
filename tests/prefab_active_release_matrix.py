#!/usr/bin/env python3
"""active-release selects state/<R>/supply.bin (iteration 08 fixture)."""

import argparse
import shutil
import subprocess
from pathlib import Path

from application_evolution_matrix import CheckFailed, manifest

MAIN_LABEL_A = "main-title-A"
MAIN_LABEL_B = "main-title-B"
TOOL_LABEL = "tool-title"
MAIN_LABEL_CONST = 60011
TOOL_LABEL_CONST = 60012


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
            import json

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


def assert_manifest_unchanged(before, after):
    if before != after:
        raise CheckFailed("USER manifest changed")


def assert_no_const_in_user(manifest_map, obj_id):
    prefix = f"{obj_id}/"
    for key in manifest_map:
        if key.startswith(prefix):
            raise CheckFailed(f"const layer in USER: {key}")


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
    parser.add_argument("--r2", required=True)
    parser.add_argument("--r1-package-tool", required=True)
    parser.add_argument("--r2-package-tool", required=True)
    parser.add_argument("--work-root", required=True)
    args = parser.parse_args()

    r0 = Path(args.r0)
    r2 = Path(args.r2)
    work_root = Path(args.work_root)
    state_root = work_root / "state"
    user_root = state_root / "user"

    if state_root.exists():
        shutil.rmtree(state_root)
    state_root.mkdir(parents=True)
    user_root.mkdir(parents=True, exist_ok=True)

    run_ok(r0, state_root, "r0-init")
    generate_package(Path(args.r1_package_tool), state_root / "1" / "supply.bin")
    generate_package(Path(args.r2_package_tool), state_root / "2" / "supply.bin")
    manifest_after_init = manifest(user_root)
    posx_after_init = None

    set_active(r0, state_root, 0)
    rec_a = run_ok(r0, state_root, "r0-read-labels")
    if rec_a.get("main_label") != MAIN_LABEL_A:
        raise CheckFailed("scenario A: main label")
    if rec_a.get("tool_label") != TOOL_LABEL:
        raise CheckFailed("scenario A: tool label")
    posx_after_init = int(rec_a.get("posx_a", 0))
    assert_manifest_unchanged(manifest_after_init, manifest(user_root))

    set_active(r0, state_root, 1)
    rec_b = run_ok(r0, state_root, "r0-read-labels")
    if rec_b.get("main_label") != MAIN_LABEL_B:
        raise CheckFailed("scenario B: main label")
    if rec_b.get("tool_label") != TOOL_LABEL:
        raise CheckFailed("scenario B: tool label")
    if int(rec_b.get("posx_a", 0)) != posx_after_init:
        raise CheckFailed("scenario B: dynamic posx changed")
    manifest_b = manifest(user_root)
    assert_manifest_unchanged(manifest_after_init, manifest_b)
    assert_no_const_in_user(manifest_b, MAIN_LABEL_CONST)
    assert_no_const_in_user(manifest_b, TOOL_LABEL_CONST)
    instance_b = int(rec_b.get("window_a_instance_id", 0))

    rec_c = run_ok(r0, state_root, "r0-read-labels")
    if rec_c.get("main_label") != MAIN_LABEL_B:
        raise CheckFailed("scenario C: main label")
    if int(rec_c.get("window_a_instance_id", 0)) != instance_b:
        raise CheckFailed("scenario C: instance id")

    set_active(r0, state_root, 0)
    rec_d = run_ok(r0, state_root, "r0-read-labels")
    if rec_d.get("main_label") != MAIN_LABEL_A:
        raise CheckFailed("scenario D: rollback main label")
    if int(rec_d.get("posx_a", 0)) != posx_after_init:
        raise CheckFailed("scenario D: dynamic state lost")
    assert_manifest_unchanged(manifest_b, manifest(user_root))

    set_active(r0, state_root, 1)
    rec_e = run_ok(r0, state_root, "r0-read-labels")
    if rec_e.get("main_label") != MAIN_LABEL_B:
        raise CheckFailed("scenario E: future R2 package must not apply")
    if not (state_root / "2" / "supply.bin").is_file():
        raise CheckFailed("scenario E: missing R2 package fixture")

    set_active(r2, state_root, 1)
    rec_t13 = run_ok(r2, state_root, "r0-read-labels")
    if rec_t13.get("main_label") != MAIN_LABEL_A:
        raise CheckFailed("T13 fragment: R2 EXE must not downgrade to R1 package")
    if int(rec_t13.get("embedded_release", 0)) != 2:
        raise CheckFailed("T13 fragment: embedded release")
    if int(rec_t13.get("active_release", 0)) != 1:
        raise CheckFailed("T13 fragment: active release")

    print("prefab_active_release_matrix OK", flush=True)


if __name__ == "__main__":
    try:
        main()
    except CheckFailed as exc:
        print(f"FAIL: {exc}", flush=True)
        raise SystemExit(1) from exc
