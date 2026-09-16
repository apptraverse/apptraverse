#!/usr/bin/env python3
"""Two-process ChatSession live pair over real Aether (Windows/Linux)."""

from __future__ import annotations

import argparse
import hashlib
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path


CHATPROBE = re.compile(r"^CHATPROBE:(.*)$")


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def read_until(proc: subprocess.Popen[str], prefix: str, timeout: float) -> str:
    deadline = time.monotonic() + timeout
    assert proc.stdout is not None
    while time.monotonic() < deadline:
        line = proc.stdout.readline()
        if not line:
            if proc.poll() is not None:
                raise RuntimeError(f"process exited early code={proc.returncode}")
            time.sleep(0.05)
            continue
        line = line.rstrip("\n")
        m = CHATPROBE.match(line)
        if not m:
            continue
        payload = m.group(1)
        if payload.startswith("ERROR:"):
            raise RuntimeError(payload)
        if payload.startswith(prefix):
            return payload
    raise TimeoutError(f"timeout waiting for {prefix}")


def write_cmd(proc: subprocess.Popen[str], cmd: str) -> None:
    assert proc.stdin is not None
    proc.stdin.write(cmd + "\n")
    proc.stdin.flush()


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--probe", type=Path, required=True)
    ap.add_argument("--workdir", type=Path, default=None)
    ap.add_argument("--messages", type=int, default=5)
    args = ap.parse_args()

    probe = args.probe.resolve()
    if not probe.is_file():
        print(f"missing probe: {probe}", file=sys.stderr)
        return 2

    work = args.workdir or Path(tempfile.mkdtemp(prefix="chat_live_pair_"))
    work.mkdir(parents=True, exist_ok=True)
    dir_a = work / "a"
    dir_b = work / "b"
    dir_a.mkdir(exist_ok=True)
    dir_b.mkdir(exist_ok=True)

    exe_hash = sha256_file(probe)
    print(f"LIVE_PROBE path={probe} sha256={exe_hash}", flush=True)

    env = os.environ.copy()
    env["PYTHONUNBUFFERED"] = "1"

    creationflags = 0
    if os.name == "nt":
        creationflags = subprocess.CREATE_NEW_PROCESS_GROUP  # type: ignore[attr-defined]

    procs: list[subprocess.Popen[str]] = []
    try:
        for label, state, role in (("A", dir_a, "--host"), ("B", dir_b, "--client")):
            p = subprocess.Popen(
                [str(probe), role, "--state-dir", str(state)],
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                bufsize=1,
                shell=False,
                env=env,
                creationflags=creationflags,
            )
            procs.append(p)
            print(f"started {label} pid={p.pid}", flush=True)

        a, b = procs
        ready_a = read_until(a, "READY ", 90.0)
        ready_b = read_until(b, "READY ", 90.0)
        uid_a = ready_a.split("uid=", 1)[1].strip()
        uid_b = ready_b.split("uid=", 1)[1].strip()
        print(f"uids a={uid_a} b={uid_b}")
        if uid_a == uid_b:
            raise RuntimeError("peers must have distinct UIDs")

        write_cmd(b, f"JOIN {uid_a}")

        # Wait for ROOM on both.
        room_a = read_until(a, "ROOM ", 90.0)
        room_b = read_until(b, "ROOM ", 90.0)
        print(f"{room_a}\n{room_b}", flush=True)
        # Allow bootstrap/presence to settle before sending.
        time.sleep(5.0)

        n = max(1, args.messages)
        for i in range(n):
            write_cmd(a, f"SEND {i}")
            time.sleep(0.5)
            write_cmd(b, f"SEND {1000 + i}")
            time.sleep(0.5)

        deadline = time.monotonic() + 120.0
        last_sa = ""
        last_sb = ""
        while time.monotonic() < deadline:
            write_cmd(a, "SNAPSHOT 1")
            write_cmd(b, "SNAPSHOT 2")
            sa = read_until(a, "SNAPSHOT ", 30.0)
            sb = read_until(b, "SNAPSHOT ", 30.0)
            last_sa, last_sb = sa, sb

            def msg_count(s: str) -> int:
                m = re.search(r"messages=(\d+)", s)
                return int(m.group(1)) if m else 0

            ca, cb = msg_count(sa), msg_count(sb)
            print(f"counts a={ca} b={cb}", flush=True)
            if ca >= 2 * n and cb >= 2 * n:
                print("message counts OK", flush=True)
                break
            time.sleep(1.0)
        else:
            print(f"last snapshots:\n{last_sa}\n{last_sb}", flush=True)
            raise TimeoutError("messages did not converge")

        write_cmd(a, "CHECKPOINT 11")
        write_cmd(b, "CHECKPOINT 12")
        read_until(a, "CHECKPOINT ", 60.0)
        read_until(b, "CHECKPOINT ", 60.0)

        write_cmd(b, "STOP")
        read_until(b, "STOPPED", 60.0)
        b.wait(timeout=30)

        # Restart B on same profile.
        b = subprocess.Popen(
            [str(probe), "--state-dir", str(dir_b)],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            bufsize=1,
            shell=False,
            creationflags=creationflags,
        )
        procs[1] = b
        ready_b2 = read_until(b, "READY ", 90.0)
        uid_b2 = ready_b2.split("uid=", 1)[1].strip()
        if uid_b2 != uid_b:
            raise RuntimeError(f"B UID changed after restart {uid_b} -> {uid_b2}")
        write_cmd(b, f"OPEN {uid_a}")
        write_cmd(a, f"OPEN {uid_b}")
        read_until(b, "ROOM ", 90.0)

        write_cmd(a, "STOP")
        write_cmd(b, "STOP")
        read_until(a, "STOPPED", 60.0)
        read_until(b, "STOPPED", 60.0)
        a.wait(timeout=30)
        b.wait(timeout=30)
        print("LIVE_PASS")
        return 0
    except Exception as ex:
        print(f"LIVE_FAIL: {ex}", file=sys.stderr)
        print(f"probe_sha256={exe_hash}", file=sys.stderr)
        return 1
    finally:
        for p in procs:
            if p.poll() is None:
                try:
                    if p.stdin:
                        p.stdin.write("STOP\n")
                        p.stdin.flush()
                except Exception:
                    pass
                try:
                    p.terminate()
                except Exception:
                    pass
                try:
                    p.wait(timeout=5)
                except Exception:
                    p.kill()


if __name__ == "__main__":
    sys.exit(main())
