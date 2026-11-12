#!/usr/bin/env python3
"""Three owned headless processes, real Aether cloud, private persisted identities."""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import time
import uuid


def read_status(path: Path):
    if os.name != "nt":
        return json.loads(path.read_text(encoding="utf-8"))
    # Let the worker atomically replace status.json while it is being read.
    # The Windows CRT's ordinary open does not grant FILE_SHARE_DELETE.
    import ctypes
    from ctypes import wintypes
    import msvcrt
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    create = kernel.CreateFileW
    create.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD,
                       wintypes.LPVOID, wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE]
    create.restype = wintypes.HANDLE
    handle = create(str(path), 0x80000000, 7, None, 3, 0x80, None)
    if handle == ctypes.c_void_p(-1).value:
        raise ctypes.WinError(ctypes.get_last_error())
    try:
        descriptor = msvcrt.open_osfhandle(handle, os.O_RDONLY | os.O_BINARY)
    except OSError:
        kernel.CloseHandle.argtypes = [wintypes.HANDLE]
        kernel.CloseHandle(handle)
        raise
    with os.fdopen(descriptor, encoding="utf-8") as stream:
        return json.load(stream)


class Campaign:
    def __init__(self, exe: Path, out: Path, deadline: float):
        self.exe, self.out, self.deadline = exe, out, time.monotonic() + deadline
        self.processes: dict[str, subprocess.Popen] = {}
        self.logs = []
        self.serial = 0
        self.run_id = uuid.uuid4().hex
        self.events = []

    def record(self, event, **fields):
        value = dict(time=time.time(), event=event, **fields)
        self.events.append(value)
        with (self.out / "controller.jsonl").open("a", encoding="utf-8") as stream:
            stream.write(json.dumps(value) + "\n")
        print(event, json.dumps(fields), flush=True)

    def start(self, side):
        profile = self.out / side
        profile.mkdir(exist_ok=True)
        (profile / "commands").mkdir(exist_ok=True)
        (profile / "acks").mkdir(exist_ok=True)
        (profile / "status.json").unlink(missing_ok=True)
        log = (profile / f"worker-{self.serial:04d}.log").open("wb")
        self.logs.append(log)
        flags = subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0
        process = subprocess.Popen(
            [str(self.exe), "--name", f"shared-node-{self.run_id}-{side}"],
            cwd=profile, stdout=log, stderr=subprocess.STDOUT, creationflags=flags,
        )
        self.processes[side] = process
        self.serial += 1
        self.record("start", side=side, pid=process.pid)

    def status(self, side):
        process = self.processes[side]
        if process.poll() is not None:
            raise RuntimeError(f"{side} exited {process.returncode}; inspect its private worker log")
        try:
            state = read_status(self.out / side / "status.json")
        except (FileNotFoundError, json.JSONDecodeError, PermissionError):
            return {}
        if state.get("error") or state.get("failed"):
            raise RuntimeError(f"{side}: {state.get('error') or 'runtime failed'}")
        for uid, peer in state.get("peers", {}).items():
            if (peer.get("event_accepted_retries", 0) > 0 and
                    peer.get("min_event_accepted_retry_gap_us", 0) < 1_000_000):
                raise RuntimeError(f"{side}->{uid}: accepted same-Event retry preceded its ACK timeout")
        return state

    def wait(self, description, predicate, seconds=75):
        end = min(self.deadline, time.monotonic() + seconds)
        while time.monotonic() < end:
            result = predicate()
            if result:
                self.record("pass", check=description)
                return result
            time.sleep(0.1)
        snapshots = {}
        for side in self.processes:
            try:
                snapshots[side] = self.status(side)
            except RuntimeError as error:
                snapshots[side] = {"error": str(error)}
        (self.out / "failure-status.json").write_text(json.dumps(snapshots, indent=2), encoding="utf-8")
        raise RuntimeError(f"timeout: {description}")

    def command(self, side, command):
        self.serial += 1
        name = f"{self.serial:05d}"
        directory = self.out / side
        temporary = directory / "commands" / f"{name}.tmp"
        temporary.write_text(command + "\n", encoding="ascii")
        temporary.replace(temporary.with_suffix(".cmd"))

        def acknowledged():
            path = directory / "acks" / f"{name}.json"
            try:
                result = json.loads(path.read_text(encoding="utf-8"))
            except (FileNotFoundError, json.JSONDecodeError, PermissionError):
                self.status(side)
                return False
            if not result.get("ok"):
                raise RuntimeError(f"{side}: command rejected: {command.split()[0]}")
            return True

        self.wait(f"{side} {command.split()[0]}", acknowledged, seconds=30)

    def stop(self, side):
        process = self.processes[side]
        self.command(side, "stop")
        process.wait(timeout=25)
        if process.returncode != 0:
            raise RuntimeError(f"{side} graceful stop exited {process.returncode}")
        self.record("stop", side=side, pid=process.pid)
        del self.processes[side]

    def stable(self, sides, tail, members, journal=None, checkpoint=None):
        states = {side: self.status(side) for side in sides}
        if not all(s.get("tail") == tail and s.get("participants") == members
                   and s.get("pending") == 0 for s in states.values()):
            return False
        if journal is not None and not all(s.get("journal") == journal for s in states.values()):
            return False
        if checkpoint is not None and not all(s.get("checkpoint") == checkpoint for s in states.values()):
            return False
        if len({s.get("digest") for s in states.values()}) != 1:
            raise RuntimeError("equal-looking tails have different full native model digests")
        if any(s.get("foreign_relay") != 0 for s in states.values()):
            raise RuntimeError("a participant relayed another author's Event")
        return states

    def run(self):
        for side in "HAB":
            self.start(side)
        selected = self.wait(
            "three private Aether identities selected",
            lambda: all(self.status(side).get("selected") for side in "HAB"), 130,
        )
        del selected
        uids = {side: self.status(side)["uid"] for side in "HAB"}
        if len(set(uids.values())) != 3:
            raise RuntimeError("clients are not independent identities")
        for side in "HAB":
            for other in "HAB":
                if other != side:
                    self.command(side, "watch " + uids[other])
        self.command("H", "boot " + uids["A"])
        room_id = self.wait("host root persisted", lambda: self.status("H").get("room_id"))
        self.command("A", f"expect {room_id} {uids['H']}")
        self.command("B", f"expect {room_id} {uids['H']}")
        self.command("H", "seed")
        self.wait("native 16KiB initial state over Aether", lambda: self.stable("HA", "|H10", 2, 1))
        if self.status("A")["max_received"] <= 16384:
            raise RuntimeError("initial state did not exercise a large native message")
        self.command("A", "append 20 |A20")
        self.wait("two direct replicas synchronized", lambda: self.stable("HA", "|H10|A20", 2, 2))
        self.command("H", f"admit {uids['B']} 30")
        self.wait("third participant admitted through host", lambda: self.stable("HAB", "|H10|A20", 3, 3))
        for side, timestamp in (("A", 40), ("B", 50), ("H", 60)):
            self.command(side, f"append {timestamp} |{side}{timestamp}")
        tail = "|H10|A20|A40|B50|H60"
        states = self.wait("all pairs deliver directly", lambda: self.stable("HAB", tail, 3, 6))
        for side in "HAB":
            if states[side]["presence_queries"] != 0:
                raise RuntimeError("presence queries ran in ACK/retry-only mode")
            for other in "HAB":
                if other != side and states[side]["peers"][uids[other]]["events_received"] == 0:
                    raise RuntimeError(f"no direct Event evidence for {other}->{side}")
                if other != side and states[side]["peers"][uids[other]]["availability"] != "unknown":
                    raise RuntimeError("availability was inferred while presence is disabled")
        if any(states[s]["peers"][uids["B"]]["old_event_attempts"] for s in "HA"):
            raise RuntimeError("initial history was resent incrementally to the joining peer")
        self.record("live-convergence", uids=uids, room_id=room_id,
                    max_native_received={s: states[s]["max_received"] for s in "HAB"})
        self.stop("B")
        self.command("A", "append 70 |A70")
        tail += "|A70"
        self.wait("H receives A70 while stopped B retains pending delivery", lambda:
                  self.status("H").get("tail") == tail and
                  self.status("A").get("peers", {}).get(uids["B"], {}).get("pending", 0) > 0)
        # An absent ACK retains the obligation. Local queue refusals may be
        # rechecked every Service turn; they are not accepted transmissions.
        initial_peer = self.status("A")["peers"][uids["B"]]
        before = initial_peer["event_attempts"]
        accepted_before = initial_peer["event_accepted"]
        retries_before = initial_peer["event_accepted_retries"]
        observation_end = time.monotonic() + 3
        attempts = before
        while time.monotonic() < observation_end:
            state = self.status("A")
            peer = state["peers"][uids["B"]]
            attempts = peer["event_attempts"]
            if peer["pending"] == 0 or peer["availability"] != "unknown" or state["presence_queries"]:
                raise RuntimeError("unacknowledged delivery was cleared or gated by presence")
            time.sleep(0.1)
        if attempts < before + 2:
            raise RuntimeError("missing ACK did not cause local retry/admission checks")
        # status() checks exact logical Service timestamps for accepted retries
        # of the same Event. A busy SDK may keep one frame in flight throughout
        # this interval, so multiple accepted retries are not required here.
        self.record("missing-ack-retry", local_admission_checks=attempts - before,
                    accepted_events=peer["event_accepted"] - accepted_before,
                    accepted_retries=peer["event_accepted_retries"] - retries_before,
                    min_accepted_retry_gap_us=peer["min_event_accepted_retry_gap_us"])
        self.start("B")
        self.wait("B restores the same identity", lambda: self.status("B").get("uid") == uids["B"], 130)
        self.wait("B catches up from durable pending", lambda: self.stable("HAB", tail, 3, 7), 90)
        for side in "HAB":
            self.command(side, "progress 1000")
        self.wait("progress packets confirmed", lambda: all(self.status(s).get("pending") == 0 for s in "HAB"))
        for side in "HAB":
            self.command(side, "compact")
        self.wait("all replicas checkpointed", lambda: self.stable("HAB", tail, 3, 0, 1000))
        for side in list("HAB"):
            self.stop(side)
        # Let the cloud forget the old connection before preserved-state startup.
        time.sleep(6)
        for side in "HAB":
            self.start(side)
        self.wait("cold checkpoint and stable UIDs restored", lambda:
                  all(self.status(s).get("uid") == uids[s] for s in "HAB") and
                  self.stable("HAB", tail, 3, 0, 1000), 130)
        old_index = self.status("A")["index"]
        self.command("A", "append 1100 |A1100")
        tail += "|A1100"
        final = self.wait("post-restart author index and delivery continue", lambda: self.stable("HAB", tail, 3, 1, 1000))
        if final["A"]["index"] != old_index + 1:
            raise RuntimeError("author index was reset across checkpoint/restart")
        summary = {"result": "PASS", "room_id": room_id, "uids": uids,
                   "final": final, "checks": self.events}
        (self.out / "summary.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")
        for side in list("HAB"):
            self.stop(side)
        return summary

    def cleanup(self):
        # Only child PIDs this controller created are owned here.
        for side, process in list(self.processes.items()):
            if process.poll() is None:
                process.terminate()
        for process in self.processes.values():
            try:
                process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=10)
        self.processes.clear()
        for log in self.logs:
            log.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--deadline", type=float, default=480)
    args = parser.parse_args()
    exe = args.exe.resolve()
    out = args.out.resolve()
    if not exe.is_file():
        parser.error("worker executable does not exist")
    if out.exists() and any(out.iterdir()):
        parser.error("use a fresh output directory; identities must not be reused from another campaign")
    out.mkdir(parents=True, exist_ok=True)
    campaign = Campaign(exe, out, args.deadline)
    try:
        campaign.run()
        print("SharedNode real Aether three-process test PASS", flush=True)
        return 0
    except (RuntimeError, OSError, subprocess.SubprocessError) as error:
        campaign.record("FAIL", reason=str(error))
        (out / "summary.json").write_text(json.dumps({"result": "FAIL", "reason": str(error)}, indent=2), encoding="utf-8")
        return 1
    finally:
        campaign.cleanup()


if __name__ == "__main__":
    sys.exit(main())
