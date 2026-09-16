#!/usr/bin/env python3
"""Offline checks for chat build receipts and --build-info side effects."""

from __future__ import annotations

import hashlib
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def fail(msg: str) -> int:
    print(f"FAIL: {msg}", file=sys.stderr)
    return 1


def main() -> int:
    build = Path(os.environ.get("APPTRAVERSE_CHAT_BUILD", ROOT / "build" / "chat-a01-msvc-debug"))
    chat = build / "examples" / "chat_demo" / "windows" / "apptraverse_chat.exe"
    probe = build / "tests" / "apptraverse_chat_session_live_probe.exe"
    receipt = build / "receipts" / "apptraverse_chat.json"

    # Missing EXE must fail packaging-style checks.
    missing = build / "does-not-exist.exe"
    if missing.exists():
        return fail("unexpected path exists")
    print("ok: missing exe detected")

    if not chat.is_file():
        print("SKIP: apptraverse_chat.exe not built yet")
        return 0

    if receipt.is_file():
        data = json.loads(receipt.read_text(encoding="utf-8-sig"))
        if data.get("sha256") != sha256(chat):
            return fail("receipt sha256 mismatches executable bytes")
        if data.get("exit_code") != 0:
            return fail("success receipt must have exit_code 0")
        emb = data.get("embedded") or {}
        if not emb.get("binary_source_sha"):
            return fail("receipt missing embedded binary_source_sha")
        print("ok: receipt matches executable")
    else:
        print("WARN: no receipt yet (build with tools/build_chat_demo.ps1)")

    with tempfile.TemporaryDirectory(prefix="chat_build_info_") as tmp:
        out = Path(tmp) / "info.txt"
        spaced = Path(tmp) / "build info out.txt"
        for path in (out, spaced):
            proc = subprocess.run(
                [str(chat), "--build-info-file", str(path)],
                cwd=str(tmp),
                capture_output=True,
                text=True,
                timeout=30,
            )
            if proc.returncode != 0:
                return fail(f"--build-info-file failed rc={proc.returncode} err={proc.stderr!r}")
            if not path.is_file():
                return fail(f"--build-info-file did not create {path}")
            text = path.read_text(encoding="utf-8")
            if "binary_source_sha=" not in text:
                return fail("build-info missing binary_source_sha")
            if "aether_objects_sha=" not in text:
                return fail("build-info missing dependency sha")
        unexpected = [p for p in Path(tmp).iterdir() if p.is_dir()]
        if unexpected:
            return fail(f"build-info created unexpected dirs: {unexpected}")
        print("ok: --build-info-file writes identity without profile dirs")

    if probe.is_file():
        proc = subprocess.run(
            [str(probe), "--build-info"],
            capture_output=True,
            text=True,
            timeout=30,
        )
        if proc.returncode != 0:
            return fail(f"probe --build-info rc={proc.returncode}")
        if "binary_source_sha=" not in proc.stdout:
            return fail("probe --build-info missing sha")
        print("ok: live probe --build-info")

    print("chat build receipt checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
