#!/usr/bin/env python3
"""Run the actual iOS acceptance apps in an already installed simulator runtime."""
import argparse
import hashlib
import json
import pathlib
import shutil
import subprocess
import time


def run(*args, **kwargs):
    return subprocess.run(args, check=True, text=True, **kwargs)


def wait_for(predicate, message, timeout=120):
    deadline = time.monotonic() + timeout
    while not predicate():
        if time.monotonic() >= deadline:
            raise RuntimeError(message)
        time.sleep(0.1)


def check_os_background(device, evidence, stamp):
    """Use UIKit's actual app transition, then kill and cold-load its USER."""
    bundle = "org.apptraverse.WindowCatalog.ios.acceptance.loadonly"
    run_id = stamp + "-os"
    container = pathlib.Path(run("xcrun", "simctl", "get_app_container", device,
        bundle, "data", capture_output=True, timeout=60).stdout.strip())
    source = container / "Documents/WindowCatalogAcceptance" / run_id
    print("Running embedded OS-deactivation seed", flush=True)
    launched = run("xcrun", "simctl", "launch", device, bundle,
        "--run-id", run_id, "--os-background", capture_output=True, timeout=300).stdout
    print(launched.strip(), flush=True)
    seed_pid = int(launched.strip().rsplit(":", 1)[1])
    wait_for(lambda: (source / "ready-os-background").exists(),
             "OS fixture never reached the active seeded state")
    profile = source / "profile"
    assert not list(profile.rglob("user.bin")), "OS fixture saved before deactivation"
    run("xcrun", "simctl", "launch", device, "com.apple.Preferences", timeout=300)
    wait_for(lambda: (profile / "user.bin").exists(),
             "Native OS deactivation did not save USER")
    user = profile / "user.bin"
    saved = user.read_bytes()
    assert saved, "OS deactivation produced an empty USER"
    time.sleep(0.25)
    assert saved == user.read_bytes(), "OS save did not finish"
    assert list(profile.rglob("*")) == [user], "OS profile is not one USER file"
    print("PASS actual OS deactivation: one USER; live process retained", flush=True)
    activated = run("xcrun", "simctl", "launch", device, bundle,
        capture_output=True, timeout=300).stdout
    print(activated.strip(), flush=True)
    assert int(activated.strip().rsplit(":", 1)[1]) == seed_pid, "OS activation replaced the process"
    wait_for(lambda: (source / "ready-os-second-background").exists(),
             "OS activation did not retain presenters and accept further work")
    resume = json.loads((source / "result-os-resume.json").read_text())
    assert resume["status"] == "PASS" and resume["same_live_session"] and resume["pid"] == seed_pid, resume
    assert user.read_bytes() == saved, "Foreground command performed unexpected disk save"
    run("xcrun", "simctl", "launch", device, "com.apple.Preferences", timeout=300)
    wait_for(lambda: user.read_bytes() != saved,
             "Second OS deactivation did not save the later accepted command")
    second_saved = user.read_bytes()
    assert second_saved, "Second OS save produced an empty USER"
    time.sleep(0.25)
    assert user.read_bytes() == second_saved, "Second OS save did not finish"
    assert list(profile.rglob("*")) == [user], "Repeated OS saves created additional profile files"
    run("xcrun", "simctl", "terminate", device, bundle, timeout=60)
    print("PASS actual OS activation: same process, bridge and presenters; later work saved", flush=True)
    target = evidence / "os-lifecycle"
    target.mkdir()
    (target / "saved-user-sha256.txt").write_text(hashlib.sha256(second_saved).hexdigest() + "\n")
    with (target / "cold.log").open("w") as output:
        run("xcrun", "simctl", "launch", "--console", device, bundle,
            "--run-id", run_id, "--cold", stdout=output,
            stderr=subprocess.STDOUT, timeout=300)
    result = json.loads((source / "result-cold.json").read_text())
    assert result["status"] == "PASS", result
    for file in source.iterdir():
        if file.is_file():
            shutil.copy2(file, target / file.name)
    print(f"PASS OS-deactivation cold restore: {result['checkpoints']} checkpoints", flush=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build", required=True, type=pathlib.Path)
    parser.add_argument("--device", required=True)
    parser.add_argument("--artifacts", required=True, type=pathlib.Path)
    options = parser.parse_args()
    stamp = time.strftime("%Y%m%d-%H%M%S")
    evidence = options.artifacts / stamp
    evidence.mkdir(parents=True)
    apps = options.build / "examples/surfaces_demo"
    # Acceptance targets are defined by an include in this directory.
    definitions = [
        ("debug", "org.apptraverse.WindowCatalog.ios.acceptance.debug"),
        ("load_only", "org.apptraverse.WindowCatalog.ios.acceptance.loadonly"),
    ]
    for kind, bundle in definitions:
        app = apps / f"apptraverse_ios_window_catalog_acceptance_{kind}.app"
        print(f"Installing {bundle}", flush=True)
        run("xcrun", "simctl", "install", options.device, str(app), timeout=300)
        phases = (False,) if kind == "debug" else (False, True)
        for cold in phases:
            label = kind + ("-cold" if cold else "-seed")
            print(f"Running {label}", flush=True)
            arguments = ["xcrun", "simctl", "launch", "--console", options.device,
                         bundle, "--run-id", stamp]
            if cold:
                arguments.append("--cold")
            with (evidence / f"{label}.log").open("w") as output:
                run(*arguments, stdout=output, stderr=subprocess.STDOUT, timeout=300)
            container = pathlib.Path(run(
                "xcrun", "simctl", "get_app_container", options.device, bundle, "data",
                capture_output=True, timeout=60).stdout.strip())
            source = container / "Documents/WindowCatalogAcceptance" / stamp
            result = json.loads((source / ("result-cold.json" if cold else "result-seed.json")).read_text())
            if result["status"] != "PASS":
                raise RuntimeError(f"{label} failed: {result}")
            target = evidence / kind
            target.mkdir(exist_ok=True)
            for file in source.iterdir():
                if file.is_file():
                    shutil.copy2(file, target / file.name)
            print(f"PASS {label}: {result['checkpoints']} checkpoints", flush=True)
    check_os_background(options.device, evidence, stamp)
    print(f"iOS simulator acceptance PASS. Evidence: {evidence}", flush=True)


if __name__ == "__main__":
    main()
