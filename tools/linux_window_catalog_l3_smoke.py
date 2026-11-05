#!/usr/bin/env python3
"""Embedded USER cold-load smoke for linux_window_catalog_demo (WSL Ubuntu + WSLg)."""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


def run_phase(
    exe: Path, state_dir: Path, automation: str, timeout_s: int, env: dict[str, str]
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
        env=env,
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
    parser.add_argument("--timeout-s", type=int, default=240)
    args = parser.parse_args()
    exe = args.exe.resolve()
    if not exe.is_file():
        print(f"FAIL: missing executable {exe}", file=sys.stderr)
        return 1

    env = os.environ.copy()
    env["GDK_BACKEND"] = "x11"

    profile = Path(tempfile.mkdtemp(prefix="apptraverse_wc_linux_geom_z_"))
    try:
        code, out1 = run_phase(exe, profile, "l3_geom_z_save", args.timeout_s, env)
        if code != 0:
            print(f"FAIL: l3_geom_z_save exit {code}", file=sys.stderr)
            print(out1, file=sys.stderr)
            return 1
        require_markers(
            out1,
            [
                "WINDOW_CATALOG_LOADED_FROM_EMBEDDED_BACKING",
                "WINDOW_CATALOG_LINUX_INITIAL_RENDER_OK",
                "WINDOW_CATALOG_LINUX_GEOM_Z_MODEL_OK",
                "WINDOW_CATALOG_SHUTDOWN_PERSIST_OK",
                "WINDOW_CATALOG_SHUTDOWN_STOPPED",
            ],
        )

        code, out2 = run_phase(exe, profile, "l3_geom_z_cold", args.timeout_s, env)
        if code != 0:
            print(f"FAIL: l3_geom_z_cold exit {code}", file=sys.stderr)
            print(out2, file=sys.stderr)
            return 1
        require_markers(
            out2,
            [
                "WINDOW_CATALOG_LOADED_FROM_PROFILE",
                "WINDOW_CATALOG_LINUX_GEOM_Z_COLD_OK",
                "WINDOW_CATALOG_LINUX_GEOM_Z_CYCLE2_SAVE_OK",
                "WINDOW_CATALOG_SHUTDOWN_PERSIST_OK",
                "WINDOW_CATALOG_SHUTDOWN_STOPPED",
            ],
        )

        code, out3 = run_phase(
            exe, profile, "l3_geom_z_cycle2_cold", args.timeout_s, env
        )
        if code != 0:
            print(f"FAIL: l3_geom_z_cycle2_cold exit {code}", file=sys.stderr)
            print(out3, file=sys.stderr)
            return 1
        require_markers(
            out3,
            [
                "WINDOW_CATALOG_LOADED_FROM_PROFILE",
                "WINDOW_CATALOG_LINUX_GEOM_Z_CYCLE2_COLD_OK",
                "WINDOW_CATALOG_SHUTDOWN_PERSIST_OK",
                "WINDOW_CATALOG_SHUTDOWN_STOPPED",
            ],
        )

        print("PASS: linux_window_catalog_l3_smoke (geom/z, GDK_BACKEND=x11)")
        return 0
    finally:
        shutil.rmtree(profile, ignore_errors=True)


if __name__ == "__main__":
    raise SystemExit(main())
