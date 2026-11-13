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


def require_single_user_file(profile: Path) -> None:
    entries = sorted(path.name for path in profile.iterdir())
    if entries != ["user.bin"] or (profile / "user.bin").stat().st_size == 0:
        print(f"FAIL: expected one non-empty USER file, found {entries}", file=sys.stderr)
        sys.exit(1)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("exe", type=Path)
    parser.add_argument("--fresh-exe", type=Path)
    parser.add_argument("--timeout-s", type=int, default=120)
    args = parser.parse_args()
    exe = args.exe.resolve()
    if not exe.is_file():
        print(f"FAIL: missing executable {exe}", file=sys.stderr)
        return 1

    profile = Path(tempfile.mkdtemp(prefix="apptraverse_l3_linux_"))
    detail_profile = Path(tempfile.mkdtemp(prefix="apptraverse_l3_linux_detail_"))
    fresh_profile = Path(tempfile.mkdtemp(prefix="apptraverse_l3_linux_fresh_"))
    try:
        if args.fresh_exe:
            for _ in range(2):
                code, output = run_phase(args.fresh_exe.resolve(), fresh_profile,
                                         "l3_save", args.timeout_s)
                if code != 0:
                    print(f"FAIL: fresh Debug exit {code}\n{output}", file=sys.stderr)
                    return 1
                require_markers(output, ["MINIMAL_APP_FRESH_DEBUG_GRAPH",
                                         "MINIMAL_APP_L3_SAVE_READY",
                                         "MINIMAL_APP_SHUTDOWN_PERSIST_OK"])
                require_single_user_file(fresh_profile)
        code, out0 = run_phase(exe, detail_profile, "l3_detail_close", args.timeout_s)
        if code != 0:
            print(f"FAIL: l3_detail_close exit {code}", file=sys.stderr)
            print(out0, file=sys.stderr)
            return 1
        require_markers(
            out0,
            [
                "MINIMAL_APP_INITIAL_RENDER_OK",
                "MINIMAL_APP_L3_DETAIL_NATIVE_CLOSE_OK",
                "MINIMAL_APP_SHUTDOWN_STOPPED",
            ],
        )

        code, out1 = run_phase(exe, profile, "l3_save", args.timeout_s)
        if code != 0:
            print(f"FAIL: l3_save exit {code}", file=sys.stderr)
            print(out1, file=sys.stderr)
            return 1
        require_markers(
            out1,
            [
                "MINIMAL_APP_LOADED_FROM_EMBEDDED_BACKING",
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

        code, out3 = run_phase(
            exe, profile, "l3_cycle2_select_row_b", args.timeout_s
        )
        if code != 0:
            print(f"FAIL: l3_cycle2_select_row_b exit {code}", file=sys.stderr)
            print(out3, file=sys.stderr)
            return 1
        require_markers(
            out3,
            [
                "MINIMAL_APP_LOADED_FROM_PROFILE",
                "MINIMAL_APP_INITIAL_RENDER_OK",
                "MINIMAL_APP_L3_CYCLE2_SELECT_OK",
                "MINIMAL_APP_SHUTDOWN_PERSIST_OK",
                "MINIMAL_APP_SHUTDOWN_STOPPED",
            ],
        )

        code, out4 = run_phase(
            exe, profile, "l3_cycle3_cold_select_row_a", args.timeout_s
        )
        if code != 0:
            print(f"FAIL: l3_cycle3_cold_select_row_a exit {code}", file=sys.stderr)
            print(out4, file=sys.stderr)
            return 1
        require_markers(
            out4,
            [
                "MINIMAL_APP_LOADED_FROM_PROFILE",
                "MINIMAL_APP_INITIAL_RENDER_OK",
                "MINIMAL_APP_L3_CYCLE3_COLD_STATE_OK",
                "MINIMAL_APP_L3_CYCLE3_POST_COMMAND_OK",
                "MINIMAL_APP_SHUTDOWN_PERSIST_OK",
                "MINIMAL_APP_SHUTDOWN_STOPPED",
            ],
        )
        require_single_user_file(profile)
        require_single_user_file(detail_profile)
    finally:
        shutil.rmtree(profile, ignore_errors=True)
        shutil.rmtree(detail_profile, ignore_errors=True)
        shutil.rmtree(fresh_profile, ignore_errors=True)

    print("PASS: linux_minimal_app L3 two-cycle embedded USER smoke")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
