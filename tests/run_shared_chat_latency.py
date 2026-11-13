#!/usr/bin/env python3
"""Measure two production ChatSessions through Aether without opening GUI windows.

Worker QPC timestamps measure Send -> remote ChatView observation and Send -> an
event-driven headless display consuming that view. Neither metric
claims physical GUI paint latency. Run explicitly; this creates cloud identities.
"""
from __future__ import annotations

import argparse
import csv
import json
import math
import os
from pathlib import Path
import queue
import random
import statistics
import subprocess
import sys
import threading
import time


PREFIX = "SNBENCH "
SIDES = ("Host", "Client")
CSV_FIELDS = (
    "phase", "seq", "source", "receiver", "payload_bytes", "frequency",
    "sent_ticks", "observed_ticks", "presented_ticks", "observed_ms", "presented_ms",
)


def distribution(values):
    ordered = sorted(values)
    if not ordered:
        return {"count": 0}
    return {
        "count": len(ordered), "min_ms": ordered[0],
        "mean_ms": statistics.fmean(ordered),
        "p50_ms": ordered[math.ceil(len(ordered) * 0.50) - 1],
        "p95_ms": ordered[math.ceil(len(ordered) * 0.95) - 1],
        "max_ms": ordered[-1],
    }


class Campaign:
    def __init__(self, args):
        self.args = args
        self.deadline = time.monotonic() + args.deadline
        self.started = time.monotonic()
        self.events = queue.Queue()
        self.processes = {}
        self.readers = []
        self.states = {}
        self.clocks = {}
        self.publications = {}
        self.idle_notifications = None
        self.stopped = set()
        self.timestamps = {}
        self.expected = {}
        self.samples = []
        self.sequence = 0
        self.timeouts = 0
        self.random = random.Random(args.seed)
        self.controller_log = (args.out / "controller.jsonl").open("w", encoding="utf-8")
        self.csv_file = (args.out / "samples.csv").open("w", newline="", encoding="utf-8")
        self.csv = csv.DictWriter(self.csv_file, CSV_FIELDS)
        self.csv.writeheader()
        self.csv_file.flush()

    def record(self, event, **fields):
        self.controller_log.write(json.dumps({"event": event, **fields}) + "\n")
        self.controller_log.flush()

    def start(self, side):
        profile = self.args.out / side
        profile.mkdir()
        flags = subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0
        process = subprocess.Popen(
            [str(self.args.exe), "--profile", str(profile), "--name", side],
            cwd=profile, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT, text=True, encoding="utf-8", errors="replace",
            bufsize=1, creationflags=flags,
        )
        self.processes[side] = process

        def read():
            try:
                with (profile / "worker.log").open("w", encoding="utf-8") as log:
                    for line in process.stdout:
                        log.write(line)
                        log.flush()
                        if line.startswith(PREFIX):
                            try:
                                event = json.loads(line[len(PREFIX):])
                                if not isinstance(event, dict):
                                    raise ValueError("event is not an object")
                            except (ValueError, TypeError) as error:
                                event = {"event": "error", "message": f"invalid worker JSON: {error}"}
                            self.events.put((side, event))
            except (OSError, ValueError) as error:
                self.events.put((side, {"event": "error", "message": f"reader failed: {error}"}))
            finally:
                self.events.put((side, {"event": "worker_eof"}))

        reader = threading.Thread(target=read, name=f"latency-{side}", daemon=True)
        reader.start()
        self.readers.append(reader)
        self.record("start", side=side, pid=process.pid)
        print(f"Starting {side} (PID {process.pid})", flush=True)

    def command(self, side, command):
        process = self.processes[side]
        if process.poll() is not None:
            raise RuntimeError(f"{side} exited {process.returncode}")
        if process.stdin.closed:
            raise RuntimeError(f"{side} input is already closed")
        process.stdin.write(command + "\n")
        process.stdin.flush()

    def consume(self, side, value):
        self.record("worker", side=side, value=value)
        event = value.get("event")
        if event == "state":
            self.states[side] = value
        elif event == "publication":
            count = value.get("count")
            if not isinstance(count, int) or count <= self.publications.get(side, 0):
                raise RuntimeError(f"{side}: nonmonotonic publication count")
            self.publications[side] = count
        elif event == "clock":
            frequency = value.get("frequency")
            if not isinstance(frequency, int) or frequency <= 0:
                raise RuntimeError(f"{side}: invalid QPC frequency")
            if side in self.clocks and self.clocks[side] != frequency:
                raise RuntimeError(f"{side}: QPC frequency changed")
            self.clocks[side] = frequency
        elif event in ("sent", "observed", "presented"):
            seq, ticks = value.get("seq"), value.get("ticks")
            if not isinstance(seq, int) or not isinstance(ticks, int) or ticks < 0:
                raise RuntimeError(f"{side}: invalid {event} timestamp")
            expected = self.expected.get(seq)
            if expected is None:
                raise RuntimeError(f"{side}: unsolicited sequence {seq}")
            source, receiver, message = expected
            if side != (source if event == "sent" else receiver):
                raise RuntimeError(f"{side}: {event} came from the wrong participant")
            if event == "observed" and value.get("message") != message:
                raise RuntimeError(f"{side}: received text differs for sequence {seq}")
            key = (side, event, seq)
            if key in self.timestamps:
                raise RuntimeError(f"{side}: duplicate {event} event for sequence {seq}")
            self.timestamps[key] = ticks
        elif event == "stopped":
            self.stopped.add(side)
        elif event == "error":
            raise RuntimeError(f"{side}: {value.get('message', 'worker error')}")
        elif event == "worker_eof":
            if side not in self.stopped:
                raise RuntimeError(f"{side}: worker output ended before graceful stop")
        else:
            raise RuntimeError(f"{side}: unknown worker event {event!r}")

    def wait(self, description, predicate, seconds):
        end = min(self.deadline, time.monotonic() + seconds)
        while True:
            # Drain ready records before testing the predicate so an error already
            # queued behind a successful observation cannot silently become PASS.
            while True:
                try:
                    self.consume(*self.events.get_nowait())
                except queue.Empty:
                    break
            result = predicate()
            if result:
                return result
            remaining = end - time.monotonic()
            if remaining <= 0:
                self.timeouts += 1
                raise RuntimeError(f"timeout: {description}")
            try:
                self.consume(*self.events.get(timeout=min(remaining, 0.1)))
            except queue.Empty:
                pass

    def pause(self):
        duration = self.random.uniform(0.020, 0.180)
        if time.monotonic() + duration > self.deadline:
            self.timeouts += 1
            raise RuntimeError("timeout: campaign deadline during between-message pause")
        # Outside the measured interval. Do not round this jitter through the
        # 100ms controller queue poll and accidentally couple it to the UI timer.
        time.sleep(duration)

    def sample(self, phase, source):
        receiver = "Client" if source == "Host" else "Host"
        self.sequence += 1
        seq = self.sequence
        prefix = f"SNBENCH:{seq}:"
        payload = prefix + "x" * (self.args.payload_bytes - len(prefix))
        if len(payload.encode("ascii")) != self.args.payload_bytes:
            raise RuntimeError("payload size is smaller than its sequence marker")
        self.expected[seq] = (source, receiver, payload)
        self.command(source, f"send {seq} {payload}")
        keys = ((source, "sent", seq), (receiver, "observed", seq), (receiver, "presented", seq))
        self.wait(f"{phase} {source}->{receiver} sequence {seq}",
                  lambda: all(key in self.timestamps for key in keys), self.args.message_timeout)
        sent, observed, presented = (self.timestamps[key] for key in keys)
        if not sent <= observed <= presented:
            raise RuntimeError(f"invalid clock order for sequence {seq}: {sent}, {observed}, {presented}")
        frequency = self.clocks[source]
        row = {
            "phase": phase, "seq": seq, "source": source, "receiver": receiver,
            "payload_bytes": len(payload), "frequency": frequency,
            "sent_ticks": sent, "observed_ticks": observed, "presented_ticks": presented,
            "observed_ms": (observed - sent) * 1000 / frequency,
            "presented_ms": (presented - sent) * 1000 / frequency,
        }
        self.samples.append(row)
        self.csv.writerow(row)
        self.csv_file.flush()
        self.record("sample", **row)

    def run(self):
        for side in SIDES:
            self.start(side)
        self.wait("both production ChatSessions ready", lambda:
                  len(self.clocks) == 2 and all(self.states.get(s, {}).get("ready") for s in SIDES), 150)
        if len(set(self.clocks.values())) != 1:
            raise RuntimeError("worker QPC frequencies differ")
        if len({self.states[s].get("uid") for s in SIDES}) != 2:
            raise RuntimeError("workers did not select independent identities")
        self.command("Host", "host")
        self.wait("host room invitation", lambda:
                  self.states["Host"].get("in_room") and self.states["Host"].get("invite"), 30)
        self.command("Client", "join " + self.states["Host"]["invite"])
        self.wait("host plus one client admitted", lambda: all(
            self.states[s].get("in_room") and self.states[s].get("members") == 2 for s in SIDES), 90)
        print("Host + client ready; measuring first messages", flush=True)
        for side in SIDES:
            self.sample("first", side)
            self.pause()
        for phase, count in (("warmup", self.args.warmup), ("measured", self.args.samples)):
            for index in range(count):
                self.sample(phase, SIDES[index % 2])
                if (index + 1) % 10 == 0 or index + 1 == count:
                    print(f"{phase}: {index + 1}/{count}", flush=True)
                self.pause()
        # Let the last message's consumer notifications drain, then verify
        # transport ACKs/retries do not cause idle publication or UI polling.
        end = time.monotonic() + 0.5
        self.wait("last notifications drained", lambda: time.monotonic() >= end, 2)
        before = self.publications.copy()
        end = time.monotonic() + 3
        self.wait("idle notification observation", lambda: time.monotonic() >= end, 5)
        self.idle_notifications = {s: self.publications[s] - before[s] for s in SIDES}
        if any(self.idle_notifications.values()):
            raise RuntimeError(f"idle chat kept publishing: {self.idle_notifications}")
        print("Idle: zero publications for 3 seconds on both peers", flush=True)
        for side in SIDES:
            profile = self.args.out / side
            if (profile / "native").exists() or (profile / "root.id").exists():
                raise RuntimeError(f"{side}: chat state was written to disk")
        self.stop_workers()

    def stop_workers(self):
        for side in SIDES:
            self.command(side, "stop")
            self.processes[side].stdin.close()
        self.wait("workers stopped gracefully", lambda: len(self.stopped) == 2, 30)
        for side, process in self.processes.items():
            process.wait(timeout=10)
            if process.returncode != 0:
                raise RuntimeError(f"{side}: graceful stop exited {process.returncode}")

    def summary(self, result, reason=None):
        phases = {}
        for phase in ("first", "warmup", "measured"):
            groups = {}
            for source in (None, *SIDES):
                rows = [s for s in self.samples if s["phase"] == phase and
                        (source is None or s["source"] == source)]
                direction = "all" if source is None else f"{source}->{('Client' if source == 'Host' else 'Host')}"
                groups[direction] = {
                    "observed": distribution([s["observed_ms"] for s in rows]),
                    "presented": distribution([s["presented_ms"] for s in rows]),
                }
            phases[phase] = groups
        return {
            "result": result, "reason": reason, "elapsed_seconds": time.monotonic() - self.started,
            "boundary": {
                "start": "worker QPC immediately before production ChatSession.Send",
                "observed": "remote ChatView read after its change notification",
                "presented": "event-driven headless display text built from that notification",
                "excluded": "physical input, sender edit/UTF8 extraction, actual Win32 control paint",
            },
            "environment": "same-host separate processes, real Aether cloud, existing Debug build",
            "chat_storage": "native USER graph atomically persisted to user.bin; SDK identity profile is separate",
            "idle_notifications": self.idle_notifications,
            "percentile_method": "nearest-rank", "timeouts": self.timeouts,
            "payload_bytes": self.args.payload_bytes, "seed": self.args.seed,
            "between_messages_jitter_ms": [20, 180],
            "requested_measured_samples": self.args.samples, "requested_warmup_samples": self.args.warmup,
            "completed_samples": len(self.samples), "qpc_frequencies": self.clocks,
            "final_states": self.states, "by_phase": phases,
        }

    def close(self):
        # Graceful cleanup first; terminate only the child PIDs owned here.
        for side, process in self.processes.items():
            if process.poll() is None and side not in self.stopped:
                try:
                    self.command(side, "stop")
                except (OSError, RuntimeError):
                    pass
                if not process.stdin.closed:
                    try:
                        process.stdin.close()
                    except OSError:
                        pass
        for process in self.processes.values():
            if process.poll() is None:
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.terminate()
                    try:
                        process.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait(timeout=5)
            if process.stdin:
                try:
                    process.stdin.close()
                except OSError:
                    pass
        for reader in self.readers:
            reader.join(timeout=5)
        for process in self.processes.values():
            if process.stdout:
                try:
                    process.stdout.close()
                except OSError:
                    pass
        self.csv_file.close()
        self.controller_log.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--samples", type=int, default=100, help="total alternating measured messages")
    parser.add_argument("--warmup", type=int, default=10, help="total alternating warmup messages")
    parser.add_argument("--payload-bytes", type=int, default=128, help="ASCII text bytes, including sequence marker")
    parser.add_argument("--seed", type=int, default=20261112)
    parser.add_argument("--deadline", type=float, default=600)
    parser.add_argument("--message-timeout", type=float, default=30)
    args = parser.parse_args()
    args.exe, args.out = args.exe.resolve(), args.out.resolve()
    if not args.exe.is_file():
        parser.error("worker executable does not exist")
    if args.samples < 2 or args.warmup < 0 or not 32 <= args.payload_bytes <= 8192:
        parser.error("need samples >= 2, warmup >= 0 and 32 <= payload-bytes <= 8192")
    if args.deadline <= 0 or args.message_timeout <= 0:
        parser.error("timeouts must be positive")
    if args.out.exists() and any(args.out.iterdir()):
        parser.error("use a fresh output directory; profiles must not be reused from another campaign")
    args.out.mkdir(parents=True, exist_ok=True)
    campaign = Campaign(args)
    code, reason = 0, None
    try:
        campaign.run()
    except (RuntimeError, OSError, ValueError, subprocess.SubprocessError) as error:
        code, reason = 1, str(error)
        print(f"FAIL: {reason}", file=sys.stderr, flush=True)
    finally:
        campaign.close()
    summary = campaign.summary("PASS" if code == 0 else "FAIL", reason)
    (args.out / "summary.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"result": summary["result"], "measured": summary["by_phase"]["measured"]}, indent=2), flush=True)
    return code


if __name__ == "__main__":
    sys.exit(main())
