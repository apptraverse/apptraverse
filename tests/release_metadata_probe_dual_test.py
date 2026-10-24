#!/usr/bin/env python3
"""Two probe binaries built from different release.json values keep distinct R."""

import argparse
import json
import subprocess
import sys
from pathlib import Path


def read_release(config_path: Path) -> int:
    data = json.loads(config_path.read_text(encoding="utf-8"))
    value = data["release"]
    if not isinstance(value, int) or isinstance(value, bool):
        raise SystemExit(f"'release' must be integer in {config_path}, got {value!r}")
    return value


def run_probe(probe: Path, cwd: Path) -> int:
    completed = subprocess.run(
        [str(probe)],
        cwd=cwd,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        check=False,
    )
    if completed.returncode != 0:
        raise SystemExit(f"probe failed: exit={completed.returncode}")
    return int(completed.stdout.strip())


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--probe0", type=Path, required=True)
    parser.add_argument("--config0", type=Path, required=True)
    parser.add_argument("--probe1", type=Path, required=True)
    parser.add_argument("--config1", type=Path, required=True)
    args = parser.parse_args()

    expected0 = read_release(args.config0.resolve())
    expected1 = read_release(args.config1.resolve())
    if expected0 == expected1:
        raise SystemExit("dual test requires different release values in configs")

    actual0 = run_probe(args.probe0.resolve(strict=True), args.probe0.parent)
    actual1 = run_probe(args.probe1.resolve(strict=True), args.probe1.parent)

    if actual0 != expected0 or actual1 != expected1:
        raise SystemExit(
            f"dual mismatch: probe0={actual0} expected0={expected0}, "
            f"probe1={actual1} expected1={expected1}"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
