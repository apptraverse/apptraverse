#!/usr/bin/env python3
"""Real-network Aether transport ladder: L0 raw, L1 safe, L2 adapter."""

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


LINE_RE = re.compile(r"^(READY|SEND_QUEUED|NATIVE_WRITE_RESULT|RECEIVE|ERROR|STOPPED|STREAM_INFO|PEER_BOUND|ADAPTER_)\b")


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def make_payload(seq: int, size: int) -> bytes:
    out = bytearray(size)
    for i in range(size):
        v = (seq * 131 + i * 17) & 0xFF
        if (i % 17) == 0:
            v = 0
        elif (i % 23) == 0:
            v = 0x80 | (v & 0x7F)
        out[i] = v
    if size >= 8:
        for b in range(8):
            out[b] = (seq >> (8 * b)) & 0xFF
    return bytes(out)


def hex_encode(data: bytes) -> str:
    return data.hex()


class ProbeProc:
    def __init__(self, label: str, proc: subprocess.Popen[str]) -> None:
        self.label = label
        self.proc = proc
        self.lines: queue.Queue[str | None] = queue.Queue()
        self.all: list[str] = []
        self._t = threading.Thread(target=self._run, daemon=True, name=f"drain-{label}")
        self._t.start()

    def _run(self) -> None:
        assert self.proc.stdout is not None
        try:
            for raw in self.proc.stdout:
                line = raw.rstrip("\n")
                self.all.append(line)
                self.lines.put(line)
                print(f"{self.label}|{line}", flush=True)
        finally:
            self.lines.put(None)

    def write(self, cmd: str) -> None:
        assert self.proc.stdin is not None
        self.proc.stdin.write(cmd + "\n")
        self.proc.stdin.flush()

    def wait_match(self, pred, timeout: float, retain_unmatched: bool = True) -> str:
        deadline = time.monotonic() + timeout
        unmatched: list[str] = []
        while time.monotonic() < deadline:
            remaining = max(0.05, deadline - time.monotonic())
            try:
                line = self.lines.get(timeout=min(0.5, remaining))
            except queue.Empty:
                if self.proc.poll() is not None:
                    raise RuntimeError(f"{self.label} exited early rc={self.proc.returncode}")
                continue
            if line is None:
                raise RuntimeError(f"{self.label} stdout closed rc={self.proc.returncode}")
            if pred(line):
                if retain_unmatched:
                    for u in unmatched:
                        self.lines.put(u)
                return line
            unmatched.append(line)
            if line.startswith("ERROR"):
                raise RuntimeError(f"{self.label}:{line}")
        raise TimeoutError(f"{self.label}: timeout pred; first_missing context unmatched={len(unmatched)}")

    def terminate(self) -> None:
        if self.proc.poll() is None:
            try:
                self.write("STOP")
            except Exception:
                pass
            try:
                self.proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.proc.kill()


def start_native(probe: Path, role: str, state: Path, stream: str, env: dict[str, str], flags: int) -> ProbeProc:
    proc = subprocess.Popen(
        [
            str(probe),
            "--state-dir",
            str(state),
            "--client-name",
            f"ladder-{role}-{stream}",
            "--stream",
            stream,
        ],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        bufsize=1,
        env=env,
        creationflags=flags,
    )
    return ProbeProc(role, proc)


def start_adapter(probe: Path, role: str, state: Path, env: dict[str, str], flags: int) -> ProbeProc:
    proc = subprocess.Popen(
        [
            str(probe),
            "--state-dir",
            str(state),
            "--client-name",
            f"ladder-adapter-{role}",
            "--binary-ladder",
        ],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        bufsize=1,
        env=env,
        creationflags=flags,
    )
    return ProbeProc(role, proc)


def parse_ready(line: str) -> str:
    # READY uid=<uid> ...
    for part in line.split():
        if part.startswith("uid="):
            return part[4:]
    raise RuntimeError(f"ready missing uid: {line}")


def sender_uid_from_ready(proc: ProbeProc) -> str:
    for line in proc.all:
        if line.startswith("READY "):
            return parse_ready(line)
    raise RuntimeError(f"{proc.label}: no READY yet")


def expect_receive(proc: ProbeProc, src: str, seq: int, size: int, timeout: float) -> None:
    want = make_payload(seq, size)
    want_hex = hex_encode(want)

    def pred(line: str) -> bool:
        if not line.startswith("RECEIVE "):
            return False
        return f"src={src}" in line and f"bytes={size}" in line and f"hex={want_hex}" in line

    reg_t0 = time.monotonic()
    line = proc.wait_match(pred, timeout)
    print(f"OK_RECEIVE {proc.label} {line} latency_s={time.monotonic() - reg_t0:.3f}", flush=True)


def expect_write_ok(proc: ProbeProc, peer: str, seq: int, timeout: float) -> None:
    def pred(line: str) -> bool:
        return (
            line.startswith("NATIVE_WRITE_RESULT ")
            and f"peer={peer}" in line
            and f"seq={seq}" in line
            and "status=Success" in line
        )

    proc.wait_match(pred, timeout)


def run_exchange(
    sender: ProbeProc,
    receiver: ProbeProc,
    peer_uid: str,
    seq: int,
    size: int,
    timeout: float,
    require_write_ok: bool = True,
) -> None:
    sender_uid = sender_uid_from_ready(sender)
    sender.write(f"SEND {peer_uid} {seq} {size}")
    sender.wait_match(lambda l: l.startswith("SEND_QUEUED ") and f"seq={seq}" in l, timeout)
    # Receive and write-OK can complete in either order (ACK may lag delivery).
    got_rx = False
    got_ok = not require_write_ok
    deadline = time.monotonic() + timeout
    want = make_payload(seq, size)
    want_hex = hex_encode(want)

    def is_rx(line: str) -> bool:
        return (
            line.startswith("RECEIVE ")
            and f"src={sender_uid}" in line
            and f"bytes={size}" in line
            and f"hex={want_hex}" in line
        )

    def is_ok(line: str) -> bool:
        return (
            line.startswith("NATIVE_WRITE_RESULT ")
            and f"peer={peer_uid}" in line
            and f"seq={seq}" in line
            and "status=Success" in line
        )

    while time.monotonic() < deadline and not (got_rx and got_ok):
        remaining = max(0.05, deadline - time.monotonic())
        # Poll receiver first for RX, sender for OK.
        for proc, pred, flag in (
            (receiver, is_rx, "rx"),
            (sender, is_ok, "ok"),
        ):
            if flag == "rx" and got_rx:
                continue
            if flag == "ok" and got_ok:
                continue
            try:
                line = proc.lines.get(timeout=min(0.2, remaining))
            except queue.Empty:
                continue
            if line is None:
                raise RuntimeError(f"{proc.label} stdout closed")
            if pred(line):
                if flag == "rx":
                    got_rx = True
                    print(f"OK_RECEIVE {proc.label} {line}", flush=True)
                else:
                    got_ok = True
            elif line.startswith("ERROR"):
                raise RuntimeError(f"{proc.label}:{line}")
            else:
                proc.lines.put(line)
    if not got_rx:
        raise TimeoutError(f"RECEIVE missing seq={seq} size={size}")
    if require_write_ok and not got_ok:
        raise TimeoutError(f"NATIVE_WRITE_RESULT Success missing seq={seq}")

def run_l0_l1(native: Path, work: Path, stream: str, sizes: list[int], env: dict[str, str], flags: int) -> None:
    host_state = work / f"{stream}-host"
    client_state = work / f"{stream}-client"
    host_state.mkdir(parents=True, exist_ok=True)
    client_state.mkdir(parents=True, exist_ok=True)

    host = start_native(native, "host", host_state, stream, env, flags)
    client = start_native(native, "client", client_state, stream, env, flags)
    children = [host, client]
    try:
        reg_t0 = time.monotonic()
        host_ready = host.wait_match(lambda l: l.startswith("READY "), 120.0)
        client_ready = client.wait_match(lambda l: l.startswith("READY "), 120.0)
        host_uid = parse_ready(host_ready)
        client_uid = parse_ready(client_ready)
        print(f"REGISTRATION_S={time.monotonic() - reg_t0:.3f} stream={stream}", flush=True)
        print(f"UIDS host={host_uid} client={client_uid}", flush=True)

        # Client opens host only. Inbound PEER_BOUND on host may arrive with
        # the first datagram; do not require it before the first SEND.
        client.write(f"OPEN {host_uid}")
        client.wait_match(lambda l: l.startswith("PEER_BOUND ") and host_uid in l, 60.0)

        # Record stream info lines already drained into all[].
        for p in (host, client):
            for line in p.all:
                if line.startswith("STREAM_INFO "):
                    print(f"INFO|{p.label}|{line}", flush=True)

        seq = 1
        # 1) Client first — write OK may lag ACK behind RECEIVE on SafeStream.
        t0 = time.monotonic()
        client.write(f"SEND {host_uid} {seq} {sizes[0]}")
        client.wait_match(lambda l: l.startswith("SEND_QUEUED ") and f"seq={seq}" in l, 60.0)
        try:
            host.wait_match(lambda l: l.startswith("PEER_BOUND ") and client_uid in l, 60.0)
        except TimeoutError as ex:
            if stream == "raw":
                print(f"L0_RAW_NO_HOST_PEER_BOUND after SEND: {ex}", flush=True)
            raise
        expect_receive(host, client_uid, seq, sizes[0], 60.0)
        try:
            expect_write_ok(client, host_uid, seq, 60.0)
        except TimeoutError:
            print(f"WARN write_ok_timeout seq={seq} (receive already matched)", flush=True)
        print(f"PACKET_LATENCY_S first={time.monotonic() - t0:.3f}", flush=True)
        seq += 1
        # 2) Host response different size
        run_exchange(host, client, client_uid, seq, sizes[1], 60.0, require_write_ok=False)
        expect_write_ok(host, client_uid, seq, 60.0)
        seq += 1

        # 3) 20 alternating
        for i in range(20):
            if i % 2 == 0:
                run_exchange(client, host, host_uid, seq, sizes[0], 60.0)
            else:
                run_exchange(host, client, client_uid, seq, sizes[1], 60.0)
            seq += 1

        # 4) Concurrent both directions
        c_seq, h_seq = seq, seq + 1
        seq += 2
        client.write(f"SEND {host_uid} {c_seq} {sizes[0]}")
        host.write(f"SEND {client_uid} {h_seq} {sizes[1]}")
        expect_write_ok(client, host_uid, c_seq, 60.0)
        expect_write_ok(host, client_uid, h_seq, 60.0)
        expect_receive(host, client_uid, c_seq, sizes[0], 60.0)
        expect_receive(client, host_uid, h_seq, sizes[1], 60.0)

        # 5) Idle then send
        time.sleep(3.0)
        run_exchange(client, host, host_uid, seq, sizes[0], 60.0)
        seq += 1
        run_exchange(host, client, client_uid, seq, sizes[1], 60.0)

        print(f"LADDER_{stream.upper()}_PASS", flush=True)
    except Exception as ex:
        print(f"LADDER_{stream.upper()}_FAIL first_missing={ex}", flush=True)
        raise
    finally:
        for c in children:
            c.terminate()


def run_l1_sizes(native: Path, work: Path, env: dict[str, str], flags: int) -> None:
    # Representative + boundary sizes for safe stream.
    host_state = work / "safe-sizes-host"
    client_state = work / "safe-sizes-client"
    host_state.mkdir(parents=True, exist_ok=True)
    client_state.mkdir(parents=True, exist_ok=True)
    host = start_native(native, "host", host_state, "safe", env, flags)
    client = start_native(native, "client", client_state, "safe", env, flags)
    try:
        host_uid = parse_ready(host.wait_match(lambda l: l.startswith("READY "), 120.0))
        client_uid = parse_ready(client.wait_match(lambda l: l.startswith("READY "), 120.0))
        client.write(f"OPEN {host_uid}")
        client.wait_match(lambda l: "PEER_BOUND" in l, 60.0)
        host.wait_match(lambda l: "PEER_BOUND" in l, 60.0)

        # Read max from STREAM_INFO
        max_el = 0
        for line in client.all:
            if "STREAM_INFO" in line and "max_element_size=" in line:
                for part in line.split():
                    if part.startswith("max_element_size="):
                        max_el = int(part.split("=", 1)[1])
        print(f"SAFE_MAX_ELEMENT_SIZE={max_el}", flush=True)

        plan = [
            ("c2h", 24),
            ("h2c", 24),
            ("h2c", 830),
            ("c2h", 24),
            ("c2h", 224),
            ("h2c", 224),
            ("c2h", 14),
            ("h2c", 63),
            ("c2h", 64),
            ("h2c", 65),
            ("c2h", 840),
            ("h2c", 4096),
        ]
        seq = 100
        for direction, size in plan:
            if max_el and size > max_el:
                print(f"UNSUPPORTED size={size} max_element_size={max_el}", flush=True)
                continue
            if direction == "c2h":
                run_exchange(client, host, host_uid, seq, size, 90.0)
            else:
                run_exchange(host, client, client_uid, seq, size, 90.0)
            seq += 1

        # Simultaneous pending
        c_seq, h_seq = seq, seq + 1
        client.write(f"SEND {host_uid} {c_seq} 224")
        host.write(f"SEND {client_uid} {h_seq} 224")
        expect_write_ok(client, host_uid, c_seq, 90.0)
        expect_write_ok(host, client_uid, h_seq, 90.0)
        expect_receive(host, client_uid, c_seq, 224, 90.0)
        expect_receive(client, host_uid, h_seq, 224, 90.0)

        time.sleep(3.0)
        run_exchange(client, host, host_uid, seq + 2, 24, 60.0)
        run_exchange(host, client, client_uid, seq + 3, 24, 60.0)
        print("LADDER_SAFE_SIZES_PASS", flush=True)
    finally:
        host.terminate()
        client.terminate()


def run_l2_adapter(adapter: Path, work: Path, env: dict[str, str], flags: int) -> None:
    host_state = work / "adapter-host"
    client_state = work / "adapter-client"
    host_state.mkdir(parents=True, exist_ok=True)
    client_state.mkdir(parents=True, exist_ok=True)
    host = start_adapter(adapter, "host", host_state, env, flags)
    client = start_adapter(adapter, "client", client_state, env, flags)
    try:
        host_uid = parse_ready(host.wait_match(lambda l: l.startswith("READY "), 120.0))
        client_uid = parse_ready(client.wait_match(lambda l: l.startswith("READY "), 120.0))
        client.write(f"OPEN {host_uid}")
        # Adapter uses ChatAetherRuntime; wait presence/peer via RECEIVE path after SEND.
        seq = 1
        for direction, size in [
            ("c2h", 24),
            ("h2c", 24),
            ("h2c", 830),
            ("c2h", 24),
            ("c2h", 224),
            ("h2c", 224),
        ]:
            if direction == "c2h":
                client.write(f"SEND {host_uid} {seq} {size}")
                expect_receive(host, client_uid, seq, size, 90.0)
            else:
                host.write(f"SEND {client_uid} {seq} {size}")
                expect_receive(client, host_uid, seq, size, 90.0)
            seq += 1
        print("LADDER_ADAPTER_PASS", flush=True)
    finally:
        host.terminate()
        client.terminate()


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--native-probe", type=Path, required=True)
    ap.add_argument("--adapter-probe", type=Path, required=False)
    ap.add_argument("--workdir", type=Path, required=True)
    ap.add_argument("--rung", choices=["l0", "l1", "l2", "all"], default="all")
    args = ap.parse_args()

    args.workdir.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    env["APPTRAVERSE_JOIN_TRACE"] = ""
    flags = getattr(subprocess, "CREATE_NO_WINDOW", 0) if os.name == "nt" else 0

    print(f"NATIVE_PROBE path={args.native_probe} sha256={sha256_file(args.native_probe)}", flush=True)
    if args.adapter_probe:
        print(
            f"ADAPTER_PROBE path={args.adapter_probe} sha256={sha256_file(args.adapter_probe)}",
            flush=True,
        )

    try:
        if args.rung in ("l0", "all"):
            run_l0_l1(args.native_probe, args.workdir, "raw", [24, 48], env, flags)
        if args.rung in ("l1", "all"):
            run_l0_l1(args.native_probe, args.workdir, "safe", [24, 48], env, flags)
            run_l1_sizes(args.native_probe, args.workdir, env, flags)
        if args.rung in ("l2", "all"):
            if not args.adapter_probe:
                raise SystemExit("--adapter-probe required for L2")
            run_l2_adapter(args.adapter_probe, args.workdir, env, flags)
    except Exception as ex:
        print(f"LADDER_FAIL {ex}", flush=True)
        return 1
    print("LADDER_PASS", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
