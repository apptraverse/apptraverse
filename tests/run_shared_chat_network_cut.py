#!/usr/bin/env python3
"""Actual Aether TCP outage, durable local commit, force-kill and synchronization."""
import argparse
import asyncio
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import time


async def run(args):
    sys.path.insert(0, str(args.fault_tools))
    spec = importlib.util.spec_from_file_location("presence_fault_tools", args.fault_tools / "run_campaign.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    args.out.mkdir(parents=True, exist_ok=False)
    journal = module.Journal(args.out / "socket-faults.jsonl")
    proxy = await module.FaultProxy("Client", journal).start()
    processes, states, readers, errors = {}, {}, [], []
    observed = {s: set() for s in ("Host", "Client")}
    committed = {s: set() for s in ("Host", "Client")}
    checks = []
    started = time.monotonic()
    serial = 0
    result = "FAIL"

    def record(label, **extra):
        row = dict(check=label, elapsed_seconds=time.monotonic() - started, **extra)
        checks.append(row)
        print(json.dumps(row), flush=True)

    async def start(side):
        nonlocal serial
        profile = args.out / side
        profile.mkdir(exist_ok=True)
        env = dict(os.environ)
        env.pop("AE_BENCH_TCP_PROXY_PORT", None)
        if side == "Client":
            env["AE_BENCH_TCP_PROXY_PORT"] = str(proxy.port)
        process = await asyncio.create_subprocess_exec(
            str(args.worker), "--profile", str(profile), "--name", side,
            stdin=asyncio.subprocess.PIPE, stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.STDOUT, env=env,
            creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
        processes[side] = process
        states.pop(side, None)
        serial += 1
        log_path = profile / f"worker-{serial}.log"

        async def read():
            with log_path.open("w", encoding="utf8") as log:
                async for raw in process.stdout:
                    line = raw.decode("utf8", errors="replace")
                    log.write(line)
                    log.flush()
                    if not line.startswith("SNBENCH "):
                        continue
                    event = json.loads(line[8:])
                    if event["event"] == "state":
                        states[side] = event
                    elif event["event"] == "observed":
                        observed[side].add(event["seq"])
                    elif event["event"] == "committed":
                        committed[side].add(event["seq"])
                    elif event["event"] == "error":
                        errors.append((side, event))
        readers.append(asyncio.create_task(read()))

    async def wait(label, condition, seconds=150):
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            if errors:
                raise RuntimeError(str(errors))
            for side, process in processes.items():
                if process.returncode is not None:
                    raise RuntimeError(f"{side} unexpectedly exited {process.returncode}")
            if condition():
                record(label)
                return
            await asyncio.sleep(.1)
        raise RuntimeError("Timeout: " + label)

    async def command(side, text):
        process = processes[side]
        process.stdin.write((text + "\n").encode("utf8"))
        await process.stdin.drain()

    async def send(side, sequence):
        await command(side, f"send {sequence} SNBENCH:{sequence}:network-cut-{side}")
        await wait(f"{side} message {sequence} durably committed", lambda: sequence in committed[side])

    try:
        for side in ("Host", "Client"):
            await start(side)
        await wait("Both native clients Online", lambda: all(
            states.get(s, {}).get("ready") and states[s].get("local_online") for s in processes))
        identity = states["Client"]["uid"]
        await command("Host", "host")
        await wait("Host invitation", lambda: states["Host"].get("invite"))
        await command("Client", "join " + states["Host"]["invite"])
        await wait("Room joined", lambda: all(states[s].get("members") == 2 for s in processes))
        await send("Host", 1)
        await wait("Host to Client", lambda: 1 in observed["Client"])
        await send("Client", 2)
        await wait("Client to Host", lambda: 2 in observed["Host"])
        if not proxy.connections or not all(proxy.bytes.values()):
            raise RuntimeError("No real process traffic passed through the fault proxy")
        proxy.block(True)
        frozen = dict(proxy.bytes)
        await send("Host", 3)
        await send("Client", 4)
        await asyncio.sleep(5)
        if proxy.bytes != frozen or 3 in observed["Client"] or 4 in observed["Host"]:
            raise RuntimeError("Messages bypassed the closed process network gate")
        record("No traffic or delivery through the disabled Client connection", forwarded=frozen)
        process = processes.pop("Client")
        process.kill()
        await process.wait()
        record("Killed only the owned Client after durable commit", pid=process.pid)
        await send("Host", 5)
        proxy.block(False)
        await start("Client")
        await wait("Same identity after forced restart", lambda: states.get("Client", {}).get("uid") == identity)
        await wait("Both offline directions synchronized after restart", lambda:
                   {1, 3, 5}.issubset(observed["Client"]) and {2, 4}.issubset(observed["Host"]), 300)
        await send("Client", 6)
        await wait("Restored client sends", lambda: 6 in observed["Host"])
        for side in processes:
            if not (args.out / side / "user.bin").is_file() or (args.out / side / "root.id").exists():
                raise RuntimeError("Single native USER contract failed")
        record("PASS: durable USER, two-way outage synchronization, force-kill and reconnect")
        result = "PASS"
    finally:
        proxy.block(False, cleanup=True)
        for process in processes.values():
            if process.returncode is None:
                process.stdin.write(b"stop\n")
                await process.stdin.drain()
                process.stdin.close()
                try:
                    await asyncio.wait_for(process.wait(), 20)
                except asyncio.TimeoutError:
                    process.kill()
                    await process.wait()
        await asyncio.gather(*readers, return_exceptions=True)
        await proxy.close()
        journal.file.close()
        (args.out / "summary.json").write_text(json.dumps(dict(result=result, checks=checks), indent=2), encoding="utf8")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--worker", required=True, type=Path)
    parser.add_argument("--fault-tools", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    args = parser.parse_args()
    args.worker, args.fault_tools, args.out = args.worker.resolve(), args.fault_tools.resolve(), args.out.resolve()
    asyncio.run(run(args))


if __name__ == "__main__":
    main()
