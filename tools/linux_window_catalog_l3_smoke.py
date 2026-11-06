#!/usr/bin/env python3
"""Embedded USER cold-load smoke for linux_window_catalog_demo (WSL Ubuntu + WSLg)."""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

# With X clients started back to back, WSLg 1.0.79 Weston xwm has been seen to
# leave windows unplaced and to abort in weston_wm_handle_map_request
# (`!window->shsurf`); phases are spaced out.
PHASE_GAP_S = 3.0
_phase_started = False


def run_phase(
    exe: Path, state_dir: Path, automation: str, timeout_s: int, env: dict[str, str]
) -> tuple[int, str]:
    global _phase_started
    if _phase_started:
        time.sleep(PHASE_GAP_S)
    _phase_started = True
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


SHUTDOWN_MARKERS = [
    "WINDOW_CATALOG_SHUTDOWN_PERSIST_OK",
    "WINDOW_CATALOG_SHUTDOWN_STOPPED",
]


def run_profile_phases(
    exe: Path,
    prefix: str,
    phases: list[tuple[str, list[str]]],
    timeout_s: int,
    env: dict[str, str],
) -> list[str] | None:
    profile = Path(tempfile.mkdtemp(prefix=prefix))
    outputs: list[str] = []
    try:
        for automation, markers in phases:
            code, out = run_phase(exe, profile, automation, timeout_s, env)
            if code != 0:
                print(f"FAIL: {automation} exit {code}", file=sys.stderr)
                print(out, file=sys.stderr)
                return None
            require_markers(out, markers + SHUTDOWN_MARKERS)
            outputs.append(out)
        return outputs
    finally:
        shutil.rmtree(profile, ignore_errors=True)


def raw_frames(text: str) -> dict[str, tuple[int, ...]]:
    frames: dict[str, tuple[int, ...]] = {}
    for line in text.splitlines():
        parts = line.split()
        if len(parts) == 6 and parts[0] == "WINDOW_CATALOG_LINUX_RAW_FRAME":
            frames[parts[1]] = tuple(int(v) for v in parts[2:])
    return frames


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

    child_close = run_profile_phases(
        exe,
        "apptraverse_wc_linux_child_close_",
        [
            (
                "l3_child_close_save",
                [
                    "WINDOW_CATALOG_LOADED_FROM_EMBEDDED_BACKING",
                    "WINDOW_CATALOG_LINUX_LIST_ROW_IDENTITY_OK",
                    "WINDOW_CATALOG_LINUX_CHILD_CLOSE_REOPEN_OK",
                    "WINDOW_CATALOG_LINUX_CHILD_CLOSE_FRAMES_MODEL_OK",
                    "WINDOW_CATALOG_LINUX_CHILD_CLOSE_SAVE_READY",
                ],
            ),
            (
                "l3_child_close_cold",
                [
                    "WINDOW_CATALOG_LOADED_FROM_PROFILE",
                    "WINDOW_CATALOG_LINUX_CHILD_CLOSE_COLD_OK",
                ],
            ),
        ],
        args.timeout_s,
        env,
    )
    if child_close is None:
        return 1
    saved_frames = raw_frames(child_close[0])
    cold_frames = raw_frames(child_close[1])
    if len(saved_frames) != 4 or saved_frames != cold_frames:
        print("FAIL: native frames differ after cold reload", file=sys.stderr)
        print(f"  saved: {saved_frames}", file=sys.stderr)
        print(f"  cold:  {cold_frames}", file=sys.stderr)
        return 1

    if run_profile_phases(
        exe,
        "apptraverse_wc_linux_l3_",
        [
            (
                "l3_save",
                [
                    "WINDOW_CATALOG_LOADED_FROM_EMBEDDED_BACKING",
                    "WINDOW_CATALOG_LINUX_REOPEN_SAME_NATIVE_OK",
                    "WINDOW_CATALOG_LINUX_LOCALE_TOGGLE_OK",
                    "WINDOW_CATALOG_LINUX_SAVE_READY",
                ],
            ),
            (
                "l3_cold_verify",
                [
                    "WINDOW_CATALOG_LOADED_FROM_PROFILE",
                    "WINDOW_CATALOG_LINUX_COLD_STATE_OK",
                    "WINDOW_CATALOG_LINUX_POST_COMMAND_OK",
                ],
            ),
            ("l3_cycle2_save", ["WINDOW_CATALOG_LINUX_CYCLE2_SAVE_READY"]),
            ("l3_cycle2_cold_verify", ["WINDOW_CATALOG_LINUX_CYCLE2_COLD_OK"]),
        ],
        args.timeout_s,
        env,
    ) is None:
        return 1

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

        print(
            "PASS: linux_window_catalog_l3_smoke "
            "(child close, l3, geom/z, GDK_BACKEND=x11)"
        )
        return 0
    finally:
        shutil.rmtree(profile, ignore_errors=True)


if __name__ == "__main__":
    raise SystemExit(main())
