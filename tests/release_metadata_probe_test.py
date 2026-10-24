#!/usr/bin/env python3
"""Compare embedded Release R in probe EXE with release.json (read by runner)."""

import argparse
import json
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


def read_release(config_path: Path) -> int:
    data = json.loads(config_path.read_text(encoding="utf-8"))
    if "release" not in data:
        raise SystemExit(f"missing 'release' in {config_path}")
    value = data["release"]
    if not isinstance(value, int) or isinstance(value, bool):
        raise SystemExit(f"'release' must be integer in {config_path}, got {value!r}")
    if value < 0:
        raise SystemExit(f"'release' must be non-negative in {config_path}")
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
        raise SystemExit(
            f"probe failed: exit={completed.returncode}, stderr={completed.stderr!r}"
        )
    line = completed.stdout.strip().splitlines()
    if len(line) != 1:
        raise SystemExit(f"unexpected probe output: {completed.stdout!r}")
    return int(line[0])


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--probe", type=Path, required=True)
    parser.add_argument("--config", type=Path, required=True)
    parser.add_argument(
        "--isolated-cwd",
        action="store_true",
        help="Run from empty temp dir with decoy state/active-release",
    )
    args = parser.parse_args()

    expected = read_release(args.config.resolve())
    probe = args.probe.resolve(strict=True)

    if args.isolated_cwd:
        with tempfile.TemporaryDirectory(prefix="release-probe-") as tmp:
            root = Path(tmp)
            (root / "state" / "5").mkdir(parents=True)
            (root / "state" / "active-release").write_text("5\n", encoding="utf-8")
            shutil.copy2(probe, root / probe.name)
            actual = run_probe(root / probe.name, root)
    else:
        actual = run_probe(probe, probe.parent)

    if actual != expected:
        raise SystemExit(f"embedded R mismatch: exe={actual}, config={expected}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
