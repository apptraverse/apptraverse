"""Explicit real-Aether test: partial kill/resume, executable update and rollback."""
import argparse
import json
import pathlib
import subprocess
import time
import queue
import threading


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--bin", required=True, type=pathlib.Path)
    parser.add_argument("--provider", required=True)
    parser.add_argument("--run", required=True, type=pathlib.Path)
    args = parser.parse_args()
    run = args.run.resolve()
    assert not run.exists(), "Use a fresh directory."
    run.mkdir(parents=True)
    binaries = args.bin.resolve()
    profile, install = run / "profile", run / "installation"
    exe, launcher = binaries / "update_lab_e0.exe", binaries / "update_lab_launcher.exe"
    snapshot = run / "state.json"
    common = ["--headless", "--profile", str(profile), "--provider", args.provider, "--snapshot-file", str(snapshot)]

    def execute(program, arguments, name, expected=0):
        with (run / (name + ".log")).open("w", encoding="utf-8") as log:
            result = subprocess.run([str(program), *arguments], stdout=log, stderr=log, timeout=1200)
        assert result.returncode == expected, (name, result.returncode)
        return json.loads(snapshot.read_text(encoding="utf-8"))

    execute(exe, [*common, "--note", "cloud portable note"], "initial")
    log = (run / "partial.log").open("w", encoding="utf-8")
    observations = queue.Queue()
    process = subprocess.Popen([str(exe), *common, "--release", "2", "--language", "de"],
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, encoding="utf-8", errors="replace")
    def observe():
        for line in process.stdout:
            log.write(line)
            log.flush()
            try:
                observations.put(json.loads(line))
            except json.JSONDecodeError:
                pass
    reader = threading.Thread(target=observe, daemon=True)
    reader.start()
    partial = None
    try:
        deadline = time.monotonic() + 180
        while time.monotonic() < deadline:
            if process.poll() is not None and observations.empty():
                raise AssertionError("Downloader exited before interruption")
            try:
                state = observations.get(timeout=0.2)
                if state["phase"] == "error":
                    raise AssertionError(state)
                if state["phase"] == "progress" and any(f["id"] == "language/2/de" and 0 < f["received"] < f["size"] for f in state["files"]):
                    partial = state
                    break
            except queue.Empty:
                pass
            time.sleep(0.02)
        assert partial and partial["language"] == "en" and partial["release"] == 0
    finally:
        process.kill()
        process.wait(timeout=10)
        reader.join(timeout=10)
        log.close()
    print("PASS real-cloud partial transfer keeps old language; process killed", flush=True)
    resumed = execute(exe, common, "resumed")
    assert resumed["language"] == "de" and resumed["release"] == 2 and resumed["phase"] == "activated"
    assert "resumed=0;" not in resumed["message"] and resumed["note"] == "cloud portable note"
    print("PASS real-cloud DownloadNode resumed and atomically activated language", flush=True)
    launch_common = [*common, "--install", str(install), "--seed", str(exe)]
    upgraded = execute(launcher, [*launch_common, "--release", "3"], "executable")
    assert upgraded["executable"] == 1 and upgraded["release"] == 3
    assert all("S1 / prefab R3" in c for c in upgraded["clones"])
    print("PASS real-cloud executable download, E1 health confirmation and S1", flush=True)
    rollback = execute(launcher, [*launch_common, "--broken"], "rollback", expected=3)
    assert rollback["executable"] == 1 and rollback["phase"] == "rolled-back"
    assert rollback["note"] == upgraded["note"] and rollback["clones"] == upgraded["clones"]
    print("PASS real-cloud bad executable download and state-preserving rollback", flush=True)
    (run / "results.json").write_text(json.dumps({"passed": 4, "partial": partial, "resumed": resumed,
        "upgraded": upgraded, "rollback": rollback}, ensure_ascii=False, indent=2), encoding="utf-8")


if __name__ == "__main__":
    main()
