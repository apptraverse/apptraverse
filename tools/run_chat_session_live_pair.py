#!/usr/bin/env python3
"""Two-process ChatSession live pair over real Aether (Windows/Linux)."""

from __future__ import annotations

import argparse
import hashlib
import os
import queue
import re
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path


CHATPROBE = re.compile(r"^CHATPROBE:(.*)$")


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


class ProbeReader:
    """Dedicated stdout reader so timeouts never block forever in readline."""

    def __init__(self, proc: subprocess.Popen[str], label: str) -> None:
        self.proc = proc
        self.label = label
        self.q: queue.Queue[str | None] = queue.Queue()
        self._thread = threading.Thread(target=self._run, name=f"probe-{label}", daemon=True)
        self._thread.start()

    def _run(self) -> None:
        assert self.proc.stdout is not None
        try:
            for line in self.proc.stdout:
                self.q.put(line.rstrip("\n"))
        finally:
            self.q.put(None)

    def read_until(self, prefix: str, timeout: float) -> str:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            remaining = max(0.05, deadline - time.monotonic())
            try:
                line = self.q.get(timeout=min(0.5, remaining))
            except queue.Empty:
                if self.proc.poll() is not None:
                    raise RuntimeError(
                        f"{self.label} exited early code={self.proc.returncode}"
                    )
                continue
            if line is None:
                raise RuntimeError(
                    f"{self.label} stdout closed code={self.proc.returncode}"
                )
            m = CHATPROBE.match(line)
            if not m:
                # Preserve unrelated output for later diagnosis.
                print(f"{self.label}|{line}", flush=True)
                continue
            payload = m.group(1)
            if payload.startswith("ERROR:"):
                raise RuntimeError(f"{self.label}:{payload}")
            if payload.startswith(prefix):
                return payload
            print(f"{self.label}|CHATPROBE:{payload}", flush=True)
        raise TimeoutError(f"{self.label}: timeout waiting for {prefix}")


def write_cmd(proc: subprocess.Popen[str], cmd: str) -> None:
    assert proc.stdin is not None
    proc.stdin.write(cmd + "\n")
    proc.stdin.flush()


def start_probe(
    probe: Path,
    role: str,
    state: Path,
    env: dict[str, str],
    creationflags: int,
) -> subprocess.Popen[str]:
    return subprocess.Popen(
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
    readers: dict[str, ProbeReader] = {}
    try:
        a = start_probe(probe, "--host", dir_a, env, creationflags)
        b = start_probe(probe, "--client", dir_b, env, creationflags)
        procs.extend([a, b])
        readers["A"] = ProbeReader(a, "A")
        readers["B"] = ProbeReader(b, "B")
        print(f"started A(host) pid={a.pid}", flush=True)
        print(f"started B(client) pid={b.pid}", flush=True)

        ready_a = readers["A"].read_until("READY ", 90.0)
        ready_b = readers["B"].read_until("READY ", 90.0)
        uid_a = ready_a.split("uid=", 1)[1].strip()
        uid_b = ready_b.split("uid=", 1)[1].strip()
        print(f"uids a={uid_a} b={uid_b}", flush=True)
        if uid_a == uid_b:
            raise RuntimeError("peers must have distinct UIDs")

        # Client-only Join. No Host OpenPeer / OPEN command.
        write_cmd(b, f"JOIN {uid_a}")

        room_a = readers["A"].read_until("ROOM ", 90.0)
        room_b = readers["B"].read_until("ROOM ", 90.0)
        print(f"{room_a}\n{room_b}", flush=True)
        id_a = re.search(r"id=(\d+)", room_a)
        id_b = re.search(r"id=(\d+)", room_b)
        if not id_a or not id_b or id_a.group(1) != id_b.group(1):
            raise RuntimeError(f"room ids differ: {room_a} vs {room_b}")

        n = max(1, args.messages)
        expected_texts: list[tuple[str, str]] = []
        for i in range(n):
            write_cmd(a, f"SEND {i}")
            time.sleep(0.2)
            write_cmd(b, f"SEND {1000 + i}")
            time.sleep(0.2)

        deadline = time.monotonic() + 120.0
        last_sa = ""
        last_sb = ""
        while time.monotonic() < deadline:
            write_cmd(a, "SNAPSHOT 1")
            write_cmd(b, "SNAPSHOT 2")
            sa = readers["A"].read_until("SNAPSHOT ", 30.0)
            sb = readers["B"].read_until("SNAPSHOT ", 30.0)
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
        readers["A"].read_until("CHECKPOINT ", 60.0)
        readers["B"].read_until("CHECKPOINT ", 60.0)

        write_cmd(b, "STOP")
        readers["B"].read_until("STOPPED", 60.0)
        b.wait(timeout=30)

        # Restart Client WITH --client; Join only (no OPEN on either side).
        b = start_probe(probe, "--client", dir_b, env, creationflags)
        procs[1] = b
        readers["B"] = ProbeReader(b, "B")
        ready_b2 = readers["B"].read_until("READY ", 90.0)
        uid_b2 = ready_b2.split("uid=", 1)[1].strip()
        if uid_b2 != uid_b:
            raise RuntimeError(f"B UID changed after restart {uid_b} -> {uid_b2}")
        write_cmd(b, f"JOIN {uid_a}")
        readers["B"].read_until("ROOM ", 90.0)

        write_cmd(a, "STOP")
        write_cmd(b, "STOP")
        readers["A"].read_until("STOPPED", 60.0)
        readers["B"].read_until("STOPPED", 60.0)
        a.wait(timeout=30)
        b.wait(timeout=30)
        if a.returncode != 0 or b.returncode != 0:
            raise RuntimeError(f"exit codes a={a.returncode} b={b.returncode}")
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
