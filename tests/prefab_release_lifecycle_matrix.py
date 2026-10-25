#!/usr/bin/env python3
"""R0 EXE + optional R1 supply.bin (iteration 07 fixture)."""

import argparse
import json
import shutil
import subprocess
from pathlib import Path

from application_evolution_matrix import CheckFailed, manifest

MAIN_LABEL_A = "main-title-A"
MAIN_LABEL_B = "main-title-B"
TOOL_LABEL = "tool-title"
MAIN_LABEL_CONST = 60011
TOOL_LABEL_CONST = 60012


def run(exe, state_dir, op, package=None):
    cmd = [str(exe), "--state-dir", str(state_dir), "--op", op]
    if package is not None:
        cmd.extend(["--supply-package", str(package)])
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


def run_ok(exe, state_dir, op, package=None):
    code, rec, comp = run(exe, state_dir, op, package)
    if code != 0:
        raise CheckFailed(
            f"expected exit 0 for {op}, got {code}: stderr={comp.stderr!r} stdout={comp.stdout!r}"
        )
    if not rec or not rec.get("ok"):
        raise CheckFailed(f"missing ok for {op}: {rec}")
    return rec


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
    parser.add_argument("--package-tool", required=True)
    parser.add_argument("--work-root", required=True)
    args = parser.parse_args()

    r0 = Path(args.r0)
    package_tool = Path(args.package_tool)
    work_root = Path(args.work_root)
    work_root.mkdir(parents=True, exist_ok=True)

    package_path = work_root / "r1" / "supply.bin"
    generate_package(package_tool, package_path)

    state = work_root / "user"
    if state.exists():
        shutil.rmtree(state)
    state.mkdir(parents=True, exist_ok=True)

    run_ok(r0, state, "r0-init")
    manifest_after_init = manifest(state)

    rec = run_ok(r0, state, "r0-read-labels")
    if rec.get("main_label") != MAIN_LABEL_A:
        raise CheckFailed("without package: main label")
    if rec.get("tool_label") != TOOL_LABEL:
        raise CheckFailed("without package: tool label")
    if rec.get("package_applied"):
        raise CheckFailed("package_applied without package")

    rec_pkg = run_ok(r0, state, "r0-read-labels", package=package_path)
    if rec_pkg.get("main_label") != MAIN_LABEL_B:
        raise CheckFailed("with package: main label")
    if rec_pkg.get("tool_label") != TOOL_LABEL:
        raise CheckFailed("with package: tool label")
    manifest_after_pkg_read = manifest(state)
    assert_manifest_unchanged(manifest_after_init, manifest_after_pkg_read)
    assert_no_const_in_user(manifest_after_pkg_read, MAIN_LABEL_CONST)
    assert_no_const_in_user(manifest_after_pkg_read, TOOL_LABEL_CONST)

    rec2 = run_ok(r0, state, "r0-read-labels", package=package_path)
    if rec2.get("main_label") != MAIN_LABEL_B:
        raise CheckFailed("second process with package")
    if int(rec2.get("window_a_instance_id", 0)) != int(
        rec_pkg.get("window_a_instance_id", 0)
    ):
        raise CheckFailed("instance id changed")

    copied_pkg = work_root / "copied" / "supply.bin"
    copied_pkg.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(package_path, copied_pkg)
    rec_copy = run_ok(r0, state, "r0-read-labels", package=copied_pkg)
    if rec_copy.get("main_label") != MAIN_LABEL_B:
        raise CheckFailed("copied package path")

    rec_no_pkg = run_ok(r0, state, "r0-read-labels")
    if rec_no_pkg.get("main_label") != MAIN_LABEL_A:
        raise CheckFailed("revert without package")

    print("prefab_release_lifecycle_matrix OK", flush=True)


if __name__ == "__main__":
    try:
        main()
    except CheckFailed as exc:
        print(f"FAIL: {exc}", flush=True)
        raise SystemExit(1) from exc
