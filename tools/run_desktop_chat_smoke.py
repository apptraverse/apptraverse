#!/usr/bin/env python3
"""Shared desktop chat smoke runner (protocol + native GUI rows).

Uses stdlib only. On Linux requires GTK3 + optional xvfb-run for headless GUI.
On Windows runs protocol row always; GUI row when the Win32 smoke binary exists.
"""

from __future__ import annotations

import argparse
import json
import os
import platform
import shutil
import subprocess
import sys
import time
from dataclasses import asdict, dataclass, field
from datetime import datetime, timezone
from pathlib import Path
from typing import Literal

ResultStatus = Literal["PASS", "FAIL", "NOT_RUN", "BLOCKED"]

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_BUILD = ROOT / "build" / "linux-x64-ninja-gcc-debug"
DEFAULT_WIN_BUILD = ROOT / "build" / "win64-ninja-msvc-debug"


@dataclass
class SmokeRow:
    name: str
    status: ResultStatus
    detail: str = ""
    duration_sec: float = 0.0


@dataclass
class SmokeReport:
    host: str
    timestamp: str
    rows: list[SmokeRow] = field(default_factory=list)

    def add(self, row: SmokeRow) -> None:
        self.rows.append(row)


def _which(name: str) -> str | None:
    return shutil.which(name)


def _run(cmd: list[str], *, cwd: Path, timeout_sec: int, env: dict[str, str] | None = None) -> tuple[int, str]:
    proc = subprocess.run(
        cmd,
        cwd=str(cwd),
        env=env,
        capture_output=True,
        text=True,
        timeout=timeout_sec,
    )
    out = (proc.stdout or "") + (proc.stderr or "")
    return proc.returncode, out


def _probe_gtk() -> bool:
    if _which("pkg-config") is None:
        return False
    code, _ = _run(["pkg-config", "--exists", "gtk+-3.0"], cwd=ROOT, timeout_sec=10)
    return code == 0


def _probe_xvfb() -> bool:
    return _which("xvfb-run") is not None


def protocol_row(build_dir: Path, timeout_sec: int) -> SmokeRow:
    name = "protocol_shared_sync"
    exe = build_dir / "tests" / "apptraverse_shared_sync_protocol_test"
    if platform.system() == "Windows":
        exe = build_dir / "tests" / "apptraverse_shared_sync_protocol_test.exe"
    if not exe.is_file():
        return SmokeRow(name, "NOT_RUN", f"missing {exe}")
    start = time.monotonic()
    try:
        code, out = _run([str(exe)], cwd=build_dir, timeout_sec=timeout_sec)
    except subprocess.TimeoutExpired:
        return SmokeRow(name, "FAIL", "timeout", time.monotonic() - start)
    dur = time.monotonic() - start
    if code == 0:
        return SmokeRow(name, "PASS", "exit 0", dur)
    tail = out.strip()[-800:]
    return SmokeRow(name, "FAIL", f"exit {code}: {tail}", dur)


def session_integration_row(build_dir: Path, timeout_sec: int) -> SmokeRow:
    name = "protocol_chat_session_integration"
    exe = build_dir / "tests" / "apptraverse_chat_session_integration_test"
    if platform.system() == "Windows":
        exe = build_dir / "tests" / "apptraverse_chat_session_integration_test.exe"
    if not exe.is_file():
        return SmokeRow(name, "NOT_RUN", f"missing {exe}")
    start = time.monotonic()
    try:
        code, out = _run([str(exe)], cwd=build_dir, timeout_sec=timeout_sec)
    except subprocess.TimeoutExpired:
        return SmokeRow(name, "FAIL", "timeout", time.monotonic() - start)
    dur = time.monotonic() - start
    if code == 0:
        return SmokeRow(name, "PASS", "exit 0", dur)
    tail = out.strip()[-800:]
    return SmokeRow(name, "FAIL", f"exit {code}: {tail}", dur)


def linux_gui_row(build_dir: Path, timeout_sec: int) -> SmokeRow:
    name = "gui_linux_gtk_smoke"
    if platform.system() != "Linux":
        return SmokeRow(name, "NOT_RUN", "requires Linux host")
    if not _probe_gtk():
        return SmokeRow(name, "NOT_RUN", "GTK3 pkg-config unavailable")
    exe = build_dir / "tests" / "apptraverse_chat_linux_smoke_test"
    if not exe.is_file():
        return SmokeRow(name, "NOT_RUN", f"missing {exe}")
    cmd = [str(exe)]
    env = os.environ.copy()
    display = env.get("DISPLAY", "")
    if not display:
        if not _probe_xvfb():
            return SmokeRow(name, "NOT_RUN", "no DISPLAY and no xvfb-run")
        cmd = ["xvfb-run", "-a", "-s", "-screen 0 1280x720x24"] + cmd
    start = time.monotonic()
    try:
        code, out = _run(cmd, cwd=build_dir, timeout_sec=timeout_sec, env=env)
    except subprocess.TimeoutExpired:
        return SmokeRow(name, "FAIL", "timeout", time.monotonic() - start)
    dur = time.monotonic() - start
    if code == 0:
        return SmokeRow(name, "PASS", "exit 0", dur)
    tail = out.strip()[-1200:]
    return SmokeRow(name, "FAIL", f"exit {code}: {tail}", dur)


def windows_gui_row(build_dir: Path, timeout_sec: int) -> SmokeRow:
    name = "gui_win32_smoke"
    if platform.system() != "Windows":
        return SmokeRow(name, "NOT_RUN", "requires Windows host")
    exe = build_dir / "tests" / "apptraverse_chat_windows_smoke_test.exe"
    if not exe.is_file():
        return SmokeRow(name, "NOT_RUN", f"missing {exe}")
    start = time.monotonic()
    try:
        code, out = _run([str(exe)], cwd=build_dir, timeout_sec=timeout_sec)
    except subprocess.TimeoutExpired:
        return SmokeRow(name, "FAIL", "timeout", time.monotonic() - start)
    dur = time.monotonic() - start
    if code == 0:
        return SmokeRow(name, "PASS", "exit 0", dur)
    tail = out.strip()[-1200:]
    return SmokeRow(name, "FAIL", f"exit {code}: {tail}", dur)


def main() -> int:
    parser = argparse.ArgumentParser(description="Desktop chat smoke runner")
    parser.add_argument("--build-dir", type=Path, default=None)
    parser.add_argument("--timeout-sec", type=int, default=120)
    parser.add_argument("--json-out", type=Path, default=None)
    args = parser.parse_args()

    if args.build_dir is not None:
        build_dir = args.build_dir
    elif platform.system() == "Windows":
        build_dir = DEFAULT_WIN_BUILD
    else:
        build_dir = DEFAULT_BUILD

    report = SmokeReport(
        host=platform.platform(),
        timestamp=datetime.now(timezone.utc).isoformat(),
    )

    report.add(protocol_row(build_dir, args.timeout_sec))
    report.add(session_integration_row(build_dir, args.timeout_sec))
    report.add(linux_gui_row(build_dir, args.timeout_sec))
    report.add(windows_gui_row(build_dir, args.timeout_sec))

    print("Desktop chat smoke report")
    print(f"  build_dir={build_dir}")
    for row in report.rows:
        print(f"  [{row.status:7}] {row.name}: {row.detail} ({row.duration_sec:.1f}s)")

    if args.json_out is not None:
        args.json_out.parent.mkdir(parents=True, exist_ok=True)
        args.json_out.write_text(json.dumps(asdict(report), indent=2), encoding="utf-8")

    failed = [r for r in report.rows if r.status == "FAIL"]
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
