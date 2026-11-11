#!/usr/bin/env python3
"""Run the actual iOS acceptance apps in an already installed simulator runtime."""
import argparse
import json
import pathlib
import subprocess
import time


def run(*args, **kwargs):
    return subprocess.run(args, check=True, text=True, **kwargs)


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
                    import shutil
                    shutil.copy2(file, target / file.name)
            print(f"PASS {label}: {result['checkpoints']} checkpoints", flush=True)
    print(f"iOS simulator acceptance PASS. Evidence: {evidence}", flush=True)


if __name__ == "__main__":
    main()
