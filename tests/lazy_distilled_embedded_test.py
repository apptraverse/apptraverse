#!/usr/bin/env python3
"""Run lazy distilled load consumer from an empty directory."""

import argparse
import shutil
import subprocess
from fixture_process import run_fixture, copy_fixture
import sys
import tempfile
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--consumer", type=Path, required=True)
    args = parser.parse_args()

    consumer = args.consumer.resolve(strict=True)

    with tempfile.TemporaryDirectory(prefix="lazy-distilled-") as tmp:
        root = Path(tmp)
        exe = root / consumer.name
        copy_fixture(consumer, exe)
        completed = run_fixture(
            [str(exe)],
            cwd=root,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            check=False,
        )
        if completed.returncode != 0:
            raise SystemExit(
                f"consumer failed: exit={completed.returncode}, "
                f"stderr={completed.stderr!r}, stdout={completed.stdout!r}"
            )
        if "lazy_distilled_load_consumer OK" not in completed.stdout:
            raise SystemExit(f"unexpected stdout: {completed.stdout!r}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
