#!/usr/bin/env python3
"""Embedded USER cold-load smoke for linux_window_catalog_demo (WSL Ubuntu + WSLg)."""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


def run_phase(
    exe: Path, state_dir: Path, automation: str, timeout_s: int
) -> tuple[int, str]:
    cmd = [
        str(exe),
        "--state-dir",
        str(state_dir),
        "--session-automation",
        automation,
    ]
    proc = subprocess.run(
        cmd,
        capture_output=True,
        text=True,
        timeout=timeout_s,
        check=False,
    )
    merged = (proc.stdout or "") + (proc.stderr or "")
    return proc.returncode, merged


def require_markers(text: str, markers: list[str]) -> None:
    missing = [m for m in markers if m not in text]
    if missing:
        print("FAIL: missing markers:", ", ".join(missing), file=sys.stderr)
        print("--- output ---", file=sys.stderr)
        print(text, file=sys.stderr)
        sys.exit(1)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("exe", type=Path)
    parser.add_argument("--timeout-s", type=int, default=180)
    args = parser.parse_args()
    exe = args.exe.resolve()
    if not exe.is_file():
        print(f"FAIL: missing executable {exe}", file=sys.stderr)
        return 1

    profile = Path(tempfile.mkdtemp(prefix="apptraverse_wc_linux_"))
    try:
        code, out1 = run_phase(exe, profile, "l3_save", args.timeout_s)
        if code != 0:
            print(f"FAIL: l3_save exit {code}", file=sys.stderr)
            print(out1, file=sys.stderr)
            return 1
        require_markers(
            out1,
            [
                "WINDOW_CATALOG_LOADED_FROM_EMBEDDED_BACKING",
                "WINDOW_CATALOG_LINUX_INITIAL_RENDER_OK",
                "WINDOW_CATALOG_LINUX_REOPEN_SAME_NATIVE_OK",
                "WINDOW_CATALOG_LINUX_LOCALE_TOGGLE_OK",
                "WINDOW_CATALOG_LINUX_SAVE_READY",
                "WINDOW_CATALOG_SHUTDOWN_PERSIST_OK",
                "WINDOW_CATALOG_SHUTDOWN_STOPPED",
            ],
        )

        code, out2 = run_phase(exe, profile, "l3_cold_verify", args.timeout_s)
        if code != 0:
            print(f"FAIL: l3_cold_verify exit {code}", file=sys.stderr)
            print(out2, file=sys.stderr)
            return 1
        require_markers(
            out2,
            [
                "WINDOW_CATALOG_LOADED_FROM_PROFILE",
                "WINDOW_CATALOG_LINUX_COLD_STATE_OK",
                "WINDOW_CATALOG_LINUX_POST_COMMAND_OK",
                "WINDOW_CATALOG_SHUTDOWN_PERSIST_OK",
                "WINDOW_CATALOG_SHUTDOWN_STOPPED",
            ],
        )

        code, out3 = run_phase(exe, profile, "l3_cycle2_save", args.timeout_s)
        if code != 0:
            print(f"FAIL: l3_cycle2_save exit {code}", file=sys.stderr)
            print(out3, file=sys.stderr)
            return 1
        require_markers(
            out3,
            [
                "WINDOW_CATALOG_LOADED_FROM_PROFILE",
                "WINDOW_CATALOG_LINUX_CYCLE2_SAVE_READY",
                "WINDOW_CATALOG_SHUTDOWN_PERSIST_OK",
                "WINDOW_CATALOG_SHUTDOWN_STOPPED",
            ],
        )

        code, out4 = run_phase(exe, profile, "l3_cycle2_cold_verify", args.timeout_s)
        if code != 0:
            print(f"FAIL: l3_cycle2_cold_verify exit {code}", file=sys.stderr)
            print(out4, file=sys.stderr)
            return 1
        require_markers(
            out4,
            [
                "WINDOW_CATALOG_LOADED_FROM_PROFILE",
                "WINDOW_CATALOG_LINUX_CYCLE2_COLD_OK",
                "WINDOW_CATALOG_SHUTDOWN_PERSIST_OK",
                "WINDOW_CATALOG_SHUTDOWN_STOPPED",
            ],
        )

        print("PASS: linux_window_catalog_l3_smoke")
        return 0
    finally:
        shutil.rmtree(profile, ignore_errors=True)


if __name__ == "__main__":
    raise SystemExit(main())
