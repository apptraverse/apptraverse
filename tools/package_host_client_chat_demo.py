#!/usr/bin/env python3
"""Package apptraverse_chat.exe with regenerated Host/Client .cmd launchers."""

from __future__ import annotations

import argparse
import hashlib
import shutil
import sys
from pathlib import Path

from windows_chat_launchers import write_launchers

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_BUILD = ROOT / "build" / "chat-a01-msvc-debug"
DEFAULT_OUT = ROOT / "dist" / "chat-demo-host-client-join-fix"


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=DEFAULT_BUILD)
    parser.add_argument("--out-dir", type=Path, default=DEFAULT_OUT)
    args = parser.parse_args()

    exe = args.build_dir / "examples" / "chat_demo" / "windows" / "apptraverse_chat.exe"
    if not exe.is_file():
        print(f"ERROR: missing executable: {exe}", file=sys.stderr)
        return 1

    if args.out_dir.exists():
        shutil.rmtree(args.out_dir)
    args.out_dir.mkdir(parents=True, exist_ok=True)

    dest_exe = args.out_dir / "apptraverse_chat.exe"
    shutil.copy2(exe, dest_exe)
    host_cmd, client_cmd = write_launchers(args.out_dir)

    digest = sha256_file(dest_exe)
    (args.out_dir / "PACKAGE_IDENTITY.txt").write_text(
        f"exe={dest_exe.name}\n"
        f"sha256={digest}\n"
        f"bytes={dest_exe.stat().st_size}\n"
        f"start_host={host_cmd.name}\n"
        f"start_client={client_cmd.name}\n",
        encoding="utf-8",
    )
    print(f"Packaged {args.out_dir}")
    print(f"sha256={digest}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
