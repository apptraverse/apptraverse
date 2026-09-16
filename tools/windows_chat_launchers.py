#!/usr/bin/env python3
"""Generate Windows Host/Client .cmd launchers with correct --state-dir quoting.

cmd.exe must receive the spaced profile path as one argv item:

  apptraverse_chat.exe --host --state-dir "%LOCALAPPDATA%\\App Traverse\\ChatExample\\host"

Do not quote %LOCALAPPDATA% separately. Do not use an intermediate unquoted set.
"""

from __future__ import annotations

from pathlib import Path

HOST_STATE_DIR_CMD = r"%LOCALAPPDATA%\App Traverse\ChatExample\host"
CLIENT_STATE_DIR_CMD = r"%LOCALAPPDATA%\App Traverse\ChatExample\client"


def start_host_cmd_text() -> str:
    return (
        "@echo off\r\n"
        "setlocal EnableExtensions\r\n"
        f'"%~dp0apptraverse_chat.exe" --host --state-dir "{HOST_STATE_DIR_CMD}" %*\r\n'
        "if errorlevel 1 pause\r\n"
    )


def start_client_cmd_text() -> str:
    return (
        "@echo off\r\n"
        "setlocal EnableExtensions\r\n"
        f'"%~dp0apptraverse_chat.exe" --client --state-dir "{CLIENT_STATE_DIR_CMD}" %*\r\n'
        "if errorlevel 1 pause\r\n"
    )


def write_launchers(out_dir: Path) -> tuple[Path, Path]:
    out_dir.mkdir(parents=True, exist_ok=True)
    host = out_dir / "start-host.cmd"
    client = out_dir / "start-client.cmd"
    host.write_bytes(start_host_cmd_text().encode("ascii"))
    client.write_bytes(start_client_cmd_text().encode("ascii"))
    return host, client


def main() -> int:
    root = Path(__file__).resolve().parents[1]
    write_launchers(root / "examples" / "chat_demo" / "windows")
    print("Wrote examples/chat_demo/windows/start-host.cmd")
    print("Wrote examples/chat_demo/windows/start-client.cmd")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
