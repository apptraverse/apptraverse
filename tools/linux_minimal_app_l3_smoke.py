#!/usr/bin/env python3
"""P3.T3.L3 cold reload smoke for linux_minimal_app (WSL Ubuntu + WSLg)."""

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
    parser.add_argument("--timeout-s", type=int, default=120)
    args = parser.parse_args()
    exe = args.exe.resolve()
    if not exe.is_file():
        print(f"FAIL: missing executable {exe}", file=sys.stderr)
        return 1

    profile = Path(tempfile.mkdtemp(prefix="apptraverse_l3_linux_"))
    try:
        code, out1 = run_phase(exe, profile, "l3_save", args.timeout_s)
        if code != 0:
            print(f"FAIL: l3_save exit {code}", file=sys.stderr)
            print(out1, file=sys.stderr)
            return 1
        require_markers(
            out1,
            [
                "MINIMAL_APP_PROFILE_BOOTSTRAP",
                "MINIMAL_APP_INITIAL_RENDER_OK",
                "MINIMAL_APP_L3_SAVE_READY",
                "MINIMAL_APP_SHUTDOWN_PERSIST_OK",
                "MINIMAL_APP_LINUX_MODEL_THREAD_EXIT",
                "MINIMAL_APP_SHUTDOWN_STOPPED",
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
                "MINIMAL_APP_LOADED_FROM_PROFILE",
                "MINIMAL_APP_INITIAL_RENDER_OK",
                "MINIMAL_APP_L3_COLD_STATE_OK",
                "MINIMAL_APP_L3_POST_COMMAND_OK",
                "MINIMAL_APP_SHUTDOWN_PERSIST_OK",
                "MINIMAL_APP_SHUTDOWN_STOPPED",
            ],
        )
    finally:
        shutil.rmtree(profile, ignore_errors=True)

    print("PASS: linux_minimal_app L3 cold reload smoke")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
