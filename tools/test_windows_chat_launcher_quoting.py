#!/usr/bin/env python3
"""Prove Host/Client .cmd quoting delivers one --state-dir argv to the real EXE."""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
import tempfile
from pathlib import Path

from windows_chat_launchers import (
    CLIENT_STATE_DIR_CMD,
    HOST_STATE_DIR_CMD,
    start_client_cmd_text,
    start_host_cmd_text,
    write_launchers,
)

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_EXE = (
    ROOT
    / "dist"
    / "chat-demo-host-client-join-fix"
    / "apptraverse_chat.exe"
)


def expanded_localappdata_path(tail: str) -> str:
    local = os.environ["LOCALAPPDATA"]
    return str(Path(local) / Path(tail))


def parse_dump(path: Path) -> dict[str, str]:
    data: dict[str, str] = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        if not line or "=" not in line:
            continue
        key, value = line.split("=", 1)
        data[key] = value
    return data


def run_cmd_argv(argv: list[str], cwd: Path) -> subprocess.CompletedProcess[str]:
    # Pass the batch path and args as separate tokens after /c so cmd.exe
    # does not re-split the spaced --state-dir value inside the .cmd body.
    return subprocess.run(
        ["cmd.exe", "/d", "/c", *argv],
        cwd=str(cwd),
        capture_output=True,
        text=True,
        timeout=60,
        check=False,
    )


def assert_dump(
    dump: dict[str, str],
    *,
    role: str,
    state_dir: str,
    label: str,
) -> None:
    if dump.get("role") != role:
        raise AssertionError(f"{label}: role={dump.get('role')!r} want {role!r}")
    if dump.get("state_dir") != state_dir:
        raise AssertionError(
            f"{label}: state_dir={dump.get('state_dir')!r} want {state_dir!r}"
        )


def test_packaged_launchers(exe: Path, work: Path) -> None:
    if not exe.is_file():
        raise FileNotFoundError(f"missing packaged exe: {exe}")
    pkg = work / "pkg"
    pkg.mkdir(parents=True, exist_ok=True)
    shutil_copy = pkg / "apptraverse_chat.exe"
    shutil_copy.write_bytes(exe.read_bytes())
    write_launchers(pkg)

    host_bytes = (pkg / "start-host.cmd").read_bytes()
    client_bytes = (pkg / "start-client.cmd").read_bytes()
    if start_host_cmd_text().encode("ascii") != host_bytes:
        raise AssertionError("generated start-host.cmd does not match generator")
    if start_client_cmd_text().encode("ascii") != client_bytes:
        raise AssertionError("generated start-client.cmd does not match generator")
    host_text = host_bytes.decode("ascii")
    client_text = client_bytes.decode("ascii")
    if f'--state-dir "{HOST_STATE_DIR_CMD}"' not in host_text:
        raise AssertionError("host launcher missing required quoted --state-dir form")
    if f'--state-dir "{CLIENT_STATE_DIR_CMD}"' not in client_text:
        raise AssertionError("client launcher missing required quoted --state-dir form")

    host_dump = work / "host_dump.txt"
    client_dump = work / "client_dump.txt"
    host_dump.unlink(missing_ok=True)
    client_dump.unlink(missing_ok=True)

    host_rc = run_cmd_argv(
        ["start-host.cmd", "--dump-parsed-launch", str(host_dump)],
        pkg,
    )
    if host_rc.returncode != 0:
        raise AssertionError(
            f"start-host.cmd failed rc={host_rc.returncode} "
            f"stdout={host_rc.stdout!r} stderr={host_rc.stderr!r}"
        )
    client_rc = run_cmd_argv(
        ["start-client.cmd", "--dump-parsed-launch", str(client_dump)],
        pkg,
    )
    if client_rc.returncode != 0:
        raise AssertionError(
            f"start-client.cmd failed rc={client_rc.returncode} "
            f"stdout={client_rc.stdout!r} stderr={client_rc.stderr!r}"
        )

    expected_host = expanded_localappdata_path(r"App Traverse\ChatExample\host")
    expected_client = expanded_localappdata_path(r"App Traverse\ChatExample\client")
    assert_dump(parse_dump(host_dump), role="host", state_dir=expected_host, label="host")
    assert_dump(
        parse_dump(client_dump),
        role="client",
        state_dir=expected_client,
        label="client",
    )
    print(f"PASS packaged launchers host_state_dir={expected_host}")
    print(f"PASS packaged launchers client_state_dir={expected_client}")


def test_multi_space_path(exe: Path, work: Path) -> None:
    multi = Path(r"C:\Temp\App Traverse Test Profile\Host One")
    multi.mkdir(parents=True, exist_ok=True)
    dump = work / "multi_dump.txt"
    dump.unlink(missing_ok=True)

    pkg = work / "multi_pkg"
    pkg.mkdir(parents=True, exist_ok=True)
    (pkg / "apptraverse_chat.exe").write_bytes(exe.read_bytes())
    # Same quoting shape as the packaged launchers: quotes around the whole
    # --state-dir value, which contains several spaces.
    script = pkg / "start-multi-space.cmd"
    script.write_bytes(
        (
            "@echo off\r\n"
            "setlocal EnableExtensions\r\n"
            f'"%~dp0apptraverse_chat.exe" --host --state-dir "{multi}" '
            f'--dump-parsed-launch "{dump}"\r\n'
        ).encode("ascii")
    )
    rc = run_cmd_argv([script.name], pkg)
    if rc.returncode != 0:
        raise AssertionError(
            f"multi-space launch failed rc={rc.returncode} "
            f"stdout={rc.stdout!r} stderr={rc.stderr!r}"
        )
    expected = str(multi)
    assert_dump(parse_dump(dump), role="host", state_dir=expected, label="multi-space")
    print(f"PASS multi-space state_dir={expected}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, default=DEFAULT_EXE)
    args = parser.parse_args()

    with tempfile.TemporaryDirectory(prefix="chat_launcher_quote_") as tmp:
        work = Path(tmp)
        test_packaged_launchers(args.exe.resolve(), work)
        test_multi_space_path(args.exe.resolve(), work)
    print("All Windows chat launcher quoting tests passed!")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:  # noqa: BLE001 - surface failure clearly for CI/local
        print(f"FAIL: {exc}", file=sys.stderr)
        raise SystemExit(1)
