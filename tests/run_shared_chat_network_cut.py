#!/usr/bin/env python3
"""Actual Aether outage, durable local commit, force-kill and synchronization."""
import argparse
import asyncio
import importlib.util
import json
import os
import shutil
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
    if args.identity_source:
        for side in ("Host", "Client"):
            target = args.out / side
            target.mkdir()
            shutil.copytree(args.identity_source / side / "sdk", target / "sdk")
    journal = module.Journal(args.out / "socket-faults.jsonl")
    if args.transport == "udp":
        if args.chaos:
            from chat_udp_faults import ChaosUdpProxy
            proxy = await ChaosUdpProxy("Client", journal).start()
        else:
            from udp_fault_proxy import UdpFaultProxy
            proxy = await UdpFaultProxy("Client", journal).start()
    else:
        proxy = await module.FaultProxy("Client", journal).start()
    processes, states, histories, readers, errors = {}, {}, {}, [], []
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
        env.pop("AE_BENCH_UDP_PROXY_PORT", None)
        if side == "Client":
            env["AE_BENCH_UDP_PROXY_PORT" if args.transport == "udp" else "AE_BENCH_TCP_PROXY_PORT"] = str(proxy.port)
        process = await asyncio.create_subprocess_exec(
            str(args.worker), "--profile", str(profile), "--name", side,
            stdin=asyncio.subprocess.PIPE, stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.STDOUT, env=env,
            creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
        processes[side] = process
        states.pop(side, None)
        histories.pop(side, None)
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
                    elif event["event"] == "history":
                        histories[side] = event["messages"]
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
        await wait("Actual P2pStream transport paths on both sides", lambda: all(
            states[s].get(args.transport + "_verified") for s in processes))
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
        await asyncio.sleep(6)
        proxy.block(False)
        await start("Client")
        await wait("Same identity after forced restart", lambda: states.get("Client", {}).get("uid") == identity)
        await wait("Both offline directions synchronized after restart", lambda:
                   {1, 3, 5}.issubset(observed["Client"]) and {2, 4}.issubset(observed["Host"]), 300)
        await send("Client", 6)
        await wait("Restored client sends", lambda: 6 in observed["Host"])
        # Maximum-size chat text; the UDP path requires multiple fragments.
        text = "SNBENCH:7:" + "fragmented-" * 800
        text = text[:8192]
        await command("Host", "send 7 " + text)
        await wait("Maximum-size chat text committed", lambda: 7 in committed["Host"])
        await wait("Maximum-size chat text delivered after reconnect", lambda: 7 in observed["Client"])
        if args.chaos:
            proxy.enable_chaos(True)
            expected = {"Host": {2, 4, 6}, "Client": {1, 3, 5, 7}}
            for sequence in range(8, 24):
                side = "Host" if sequence % 2 else "Client"
                other = "Client" if side == "Host" else "Host"
                payload = f"SNBENCH:{sequence}:" + ("large-" * 1300 if sequence % 4 == 0 else "unordered-UDP")
                await command(side, f"send {sequence} {payload}")
                await wait(f"Lossy burst {sequence} locally committed", lambda s=side, n=sequence: n in committed[s])
                expected[other].add(sequence)
            await wait("Loss, duplicate and reorder burst converges", lambda: all(
                expected[s].issubset(observed[s]) for s in processes), 300)
            counters = dict(proxy.chaos)
            if any(sum(counters[d + "_" + kind] for d in ("in", "out")) == 0
                   for kind in ("dropped", "duplicated", "reordered")):
                raise RuntimeError("Fault scenario did not actually inject all required faults")
            proxy.enable_chaos(False)
            record("Actual UDP fault injections confirmed", counters=counters)
        count = 23 if args.chaos else 7
        await wait("Histories have equal native order and no duplicates", lambda:
            len(histories.get("Host", [])) == count and histories.get("Host") == histories.get("Client")
            and len(set(histories["Host"])) == count, 120)
        await wait("Confirmed business journals collapse", lambda: all(
            states[s].get("retained_events") == 0 and states[s].get("pending_packets") == 0
            for s in processes), 120)
        canonical = list(histories["Host"])
        identities = {s: states[s]["uid"] for s in processes}
        for side in list(processes):
            process = processes.pop(side)
            process.kill()
            await process.wait()
        record("Killed both owned processes after durable convergence")
        await asyncio.sleep(6)
        for side in ("Host", "Client"):
            await start(side)
        await wait("Both identities and complete histories survive cold restart", lambda: all(
            states.get(s, {}).get("uid") == identities[s] and histories.get(s) == canonical for s in processes), 150)
        await wait("Actual transport restored after both processes restarted", lambda: all(
            states[s].get(args.transport + "_verified") for s in processes), 150)
        await send("Client", count + 1)
        await wait("Cold-restored chat continues over P2pStream " + args.transport.upper(),
                   lambda: count + 1 in observed["Host"], 150)
        for side in processes:
            if not (args.out / side / "user.bin").is_file() or (args.out / side / "root.id").exists():
                raise RuntimeError("Single native USER contract failed")
        record("PASS: durable USER, two-way outage synchronization, force-kill and reconnect")
        result = "PASS"
    except Exception as error:
        record("FAIL", error=repr(error))
        raise
    finally:
        proxy.block(False, cleanup=True)
        cleanup_errors = []
        for process in processes.values():
            if process.returncode is None:
                try:
                    process.stdin.write(b"stop\n")
                    await process.stdin.drain()
                    process.stdin.close()
                except (BrokenPipeError, ConnectionResetError) as error:
                    cleanup_errors.append(repr(error))
                try:
                    await asyncio.wait_for(process.wait(), 20)
                except asyncio.TimeoutError:
                    cleanup_errors.append(f"Owned worker {process.pid} did not stop")
                    process.kill()
                    await process.wait()
            if process.returncode != 0:
                cleanup_errors.append(f"Owned worker {process.pid} exited {process.returncode}")
        await asyncio.gather(*readers, return_exceptions=True)
        await proxy.close()
        journal.file.close()
        cleanup_failed = bool(cleanup_errors) and result == "PASS"
        if cleanup_failed:
            result = "FAIL"
            record("FAIL: worker shutdown", errors=cleanup_errors)
        (args.out / "summary.json").write_text(json.dumps(dict(result=result, checks=checks), indent=2), encoding="utf8")
        if cleanup_failed:
            raise RuntimeError("Worker shutdown failed: " + repr(cleanup_errors))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--worker", required=True, type=Path)
    parser.add_argument("--fault-tools", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--identity-source", type=Path, help="Private SDK identity seed; copied into a fresh room")
    parser.add_argument("--transport", choices=("tcp", "udp"), default="tcp")
    parser.add_argument("--chaos", action="store_true", help="UDP loss/duplication/reordering and full cold restart")
    args = parser.parse_args()
    if args.chaos and args.transport != "udp":
        parser.error("--chaos requires --transport udp")
    args.worker, args.fault_tools, args.out = args.worker.resolve(), args.fault_tools.resolve(), args.out.resolve()
    asyncio.run(run(args))


if __name__ == "__main__":
    main()
