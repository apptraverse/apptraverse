#!/usr/bin/env python3
"""GTK acceptance runner: USER round-trips, native input and geometry on X11."""

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


SHUTDOWN_MARKERS = ["WINDOW_CATALOG_LINUX_USER_FILE_OK"]


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
            user_file = profile / "user.bin"
            if not user_file.is_file() or user_file.stat().st_size == 0:
                print(f"FAIL: shutdown did not write {user_file}", file=sys.stderr)
                return None
            if list(profile.rglob("*")) != [user_file]:
                print(f"FAIL: USER contains files beyond user.bin: {profile}", file=sys.stderr)
                return None
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


def geometry_frames(text: str, phase: str) -> dict[str, tuple[int, ...]]:
    frames: dict[str, tuple[int, ...]] = {}
    for line in text.splitlines():
        parts = line.split()
        if len(parts) == 7 and parts[:2] == ["WINDOW_CATALOG_LINUX_FRAME", phase]:
            frames[parts[2]] = tuple(int(v) for v in parts[3:])
    return frames


def verify_geometry_roundtrip(saved: str, loaded: str) -> bool:
    before = geometry_frames(saved, "geom_saved")
    after = geometry_frames(loaded, "geom_loaded")
    if len(before) != 4 or before != after:
        print(f"FAIL: native geometry round-trip: {before} -> {after}", file=sys.stderr)
        return False
    return True


def verify_fresh_debug(
    fresh_exe: Path, timeout_s: int, env: dict[str, str]
) -> bool:
    parent = Path(tempfile.mkdtemp(prefix="apptraverse_wc_linux_debug_"))
    profiles: list[Path] = []
    snapshots: list[tuple[bytes, int]] = []
    try:
        # Both launches receive the same parent, which already contains a USER
        # after launch one. Each fresh runner must start with zero rows.
        for _ in range(2):
            code, out = run_phase(fresh_exe, parent, "l3_save", timeout_s, env)
            if code != 0:
                print(out, file=sys.stderr)
                return False
            require_markers(out, ["WINDOW_CATALOG_LINUX_SAVE_READY"] + SHUTDOWN_MARKERS)
            marker = "WINDOW_CATALOG_DEBUG_FRESH_PROFILE="
            paths = [Path(line[len(marker):]) for line in out.splitlines() if line.startswith(marker)]
            if len(paths) != 1 or paths[0].parent.parent != parent:
                print("FAIL: missing or unexpected fresh Debug profile", file=sys.stderr)
                return False
            profile = paths[0]
            user_file = profile / "user.bin"
            if not user_file.is_file() or profile in profiles:
                print("FAIL: Debug reused a profile or did not save USER", file=sys.stderr)
                return False
            if not user_file.stat().st_size or list(profile.rglob("*")) != [user_file]:
                print("FAIL: fresh USER must contain exactly one nonempty user.bin", file=sys.stderr)
                return False
            for previous, snapshot in zip(profiles, snapshots):
                previous_user = previous / "user.bin"
                if (previous_user.read_bytes(), previous_user.stat().st_mtime_ns) != snapshot:
                    print("FAIL: Debug modified a previous USER", file=sys.stderr)
                    return False
            profiles.append(profile)
            snapshots.append((user_file.read_bytes(), user_file.stat().st_mtime_ns))
        # Each fresh Debug run distills its own BACKING with new identities.
        # Its USER cannot be paired with the unrelated build-time supply.
        # Cold restoration is tested below using one embedded supply throughout.
        return True
    finally:
        shutil.rmtree(parent, ignore_errors=True)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("exe", type=Path)
    parser.add_argument("--fresh-exe", type=Path,
                        help="same test runner with the normal Debug fresh lifecycle")
    parser.add_argument("--timeout-s", type=int, default=240)
    args = parser.parse_args()
    exe = args.exe.resolve()
    if not exe.is_file():
        print(f"FAIL: missing executable {exe}", file=sys.stderr)
        return 1

    env = os.environ.copy()
    env["GDK_BACKEND"] = "x11"

    if args.fresh_exe is not None:
        if not verify_fresh_debug(args.fresh_exe.resolve(), args.timeout_s, env):
            return 1

    if run_profile_phases(
        exe,
        "apptraverse_wc_linux_native_geometry_",
        [("native_geometry", [
            "WINDOW_CATALOG_LINUX_OS_PLACEMENT_EVENT_OK",
        ])],
        args.timeout_s,
        env,
    ) is None:
        return 1

    child_close = run_profile_phases(
        exe,
        "apptraverse_wc_linux_child_close_",
        [
            (
                "l3_child_close_save",
                [
                    "WINDOW_CATALOG_LINUX_LIST_ROW_IDENTITY_OK",
                    "WINDOW_CATALOG_LINUX_ROW_ACTIVATED_OK",
                    "WINDOW_CATALOG_LINUX_CHILD_CLOSE_REOPEN_OK",
                    "WINDOW_CATALOG_LINUX_CHILD_CLOSE_FRAMES_MODEL_OK",
                    "WINDOW_CATALOG_LINUX_CHILD_CLOSE_SAVE_READY",
                ],
            ),
            (
                "l3_child_close_cold",
                [
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
                    "WINDOW_CATALOG_LINUX_REOPEN_SAME_NATIVE_OK",
                    "WINDOW_CATALOG_LINUX_LOCALE_TOGGLE_OK",
                    "WINDOW_CATALOG_LINUX_SAVE_READY",
                ],
            ),
            (
                "l3_cold_verify",
                [
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

    geometry = run_profile_phases(
        exe,
        "apptraverse_wc_linux_geom_z_",
        [
            ("l3_geom_z_save", ["WINDOW_CATALOG_LINUX_GEOM_Z_MODEL_OK"]),
            (
                "l3_geom_z_cold",
                [
                    "WINDOW_CATALOG_LINUX_GEOM_Z_COLD_OK",
                    "WINDOW_CATALOG_LINUX_GEOM_Z_CYCLE2_SAVE_OK",
                ],
            ),
            (
                "l3_geom_z_cycle2_cold",
                ["WINDOW_CATALOG_LINUX_GEOM_Z_CYCLE2_COLD_OK"],
            ),
        ],
        args.timeout_s,
        env,
    )
    if geometry is None:
        return 1
    if not verify_geometry_roundtrip(geometry[0], geometry[1]):
        return 1
    if not verify_geometry_roundtrip(geometry[1], geometry[2]):
        return 1
    print(
        "PASS: linux_window_catalog_l3_smoke "
        "(child close, l3, geom/z, one user.bin, GDK_BACKEND=x11)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
