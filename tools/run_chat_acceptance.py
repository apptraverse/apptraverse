#!/usr/bin/env python3
"""Cross-platform chat acceptance matrix (honest NOT_RUN when tools missing)."""

from __future__ import annotations

import argparse
import json
import os
import platform
import shutil
import subprocess
import sys
from dataclasses import asdict, dataclass, field
from datetime import datetime, timezone
from pathlib import Path
from typing import Literal

ResultStatus = Literal["PASS", "FAIL", "NOT_RUN", "BLOCKED"]

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_WIN_BUILD = ROOT / "build" / "win64-ninja-msvc-debug"


@dataclass
class MatrixRow:
    pair: str
    status: ResultStatus
    detail: str = ""


@dataclass
class AcceptanceReport:
    host: str
    timestamp: str
    rows: list[MatrixRow] = field(default_factory=list)


def _which(name: str) -> str | None:
    return shutil.which(name)


def _has_android() -> bool:
    return bool(os.environ.get("ANDROID_HOME")) and _which("adb") is not None


def _has_emscripten() -> bool:
    return _which("emcc") is not None


def _run_smoke(build_dir: Path) -> tuple[ResultStatus, str]:
    script = ROOT / "tools" / "run_desktop_chat_smoke.py"
    if not script.is_file():
        return "NOT_RUN", "missing run_desktop_chat_smoke.py"
    proc = subprocess.run(
        [sys.executable, str(script), "--build-dir", str(build_dir)],
        cwd=str(ROOT),
        capture_output=True,
        text=True,
    )
    if proc.returncode == 0:
        return "PASS", "desktop smoke runner exit 0"
    tail = (proc.stdout or "") + (proc.stderr or "")
    return "FAIL", tail.strip()[-600:]


def main() -> int:
    parser = argparse.ArgumentParser(description="Chat acceptance matrix")
    parser.add_argument("--build-dir", type=Path, default=DEFAULT_WIN_BUILD)
    parser.add_argument("--json-out", type=Path, default=None)
    args = parser.parse_args()

    report = AcceptanceReport(
        host=platform.platform(),
        timestamp=datetime.now(timezone.utc).isoformat(),
    )

    host = platform.system()
    smoke_status, smoke_detail = _run_smoke(args.build_dir)

    if host == "Windows":
        report.rows.append(
            MatrixRow(
                "Windows<->Windows (fake transport + Win32 smoke)",
                smoke_status,
                smoke_detail,
            )
        )
    else:
        report.rows.append(
            MatrixRow(
                "Windows<->Windows",
                "NOT_RUN",
                f"requires Windows host (current: {host})",
            )
        )

    if host == "Linux" and _which("pkg-config"):
        code = subprocess.run(
            ["pkg-config", "--exists", "gtk+-3.0"],
            capture_output=True,
        ).returncode
        if code == 0:
            report.rows.append(
                MatrixRow(
                    "Linux<->Linux",
                    smoke_status,
                    "GTK host + smoke when built on Linux",
                )
            )
        else:
            report.rows.append(
                MatrixRow("Linux<->Linux", "NOT_RUN", "GTK3 unavailable")
            )
    else:
        report.rows.append(
            MatrixRow("Linux<->Linux", "NOT_RUN", "requires Linux + GTK3 host")
        )

    report.rows.append(
        MatrixRow(
            "Windows<->Linux",
            "NOT_RUN",
            "requires two hosts or CI matrix; not run on this single host",
        )
    )

    if _has_android():
        report.rows.append(
            MatrixRow(
                "desktop<->Android",
                "NOT_RUN",
                "ANDROID_HOME+adb present; APK/interop harness not run here",
            )
        )
    else:
        report.rows.append(
            MatrixRow(
                "desktop<->Android",
                "NOT_RUN",
                "ANDROID_HOME/adb unavailable on this host",
            )
        )

    report.rows.append(
        MatrixRow(
            "Android<->Android",
            "NOT_RUN",
            "requires two devices/emulators",
        )
    )

    report.rows.append(
        MatrixRow(
            "live Aether two-process",
            "NOT_RUN" if host == "Windows" else smoke_status,
            "POSIX-only apptraverse_chat_aether_p2p_test in CMake on Windows",
        )
    )

    print("Chat acceptance matrix")
    for row in report.rows:
        print(f"  [{row.status:7}] {row.pair}: {row.detail}")

    if args.json_out is not None:
        args.json_out.parent.mkdir(parents=True, exist_ok=True)
        args.json_out.write_text(json.dumps(asdict(report), indent=2), encoding="utf-8")

    failed = [r for r in report.rows if r.status == "FAIL"]
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
