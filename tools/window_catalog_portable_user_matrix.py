"""Run USER interchange against separately compiled platform registries/supplies.

Each runner has a command argv list and optional path_style="wsl". Android uses
kind="adb", adb argv, executable and remote_root. Every input is a private copy;
this tool never reads/writes the application's ordinary profile.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import time


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


class Runner:
    def __init__(self, name: str, config: dict, root: Path):
        self.name, self.config, self.root = name, config, root

    def path(self, path: Path) -> str:
        if self.config.get("kind") == "adb":
            return self.config["remote_root"].rstrip("/") + "/" + path.name
        value = str(path.resolve()).replace("\\", "/")
        if self.config.get("path_style") == "wsl" and value[1:3] == ":/":
            value = "/mnt/" + value[0].lower() + value[2:]
        return value

    def call(self, mode: str, source: Path, output: Path | None = None) -> dict:
        config = self.config
        stem = f"{self.name}-{mode}-{source.name}"
        log = self.root / (stem + ".log")
        input_sha256 = sha(source / "user.bin") if mode == "verify" else None
        if config.get("kind") == "adb":
            adb = config["adb"]
            subprocess.run(adb + ["shell", "mkdir", "-p", config["remote_root"]], check=True)
            if mode != "seed":
                subprocess.run(adb + ["shell", "mkdir", "-p", self.path(source)], check=True)
                for file in ("user.bin", "manifest.txt"):
                    subprocess.run(adb + ["push", str(source / file), self.path(source) + "/" + file],
                                   check=True, capture_output=True)
            command = adb + ["shell", config["executable"], mode, self.path(source)]
        else:
            command = config["command"] + [mode, self.path(source)]
        if output is not None:
            command.append(self.path(output))
        started = time.monotonic()
        try:
            result = subprocess.run(command, capture_output=True, timeout=180)
            log.write_bytes(result.stdout + result.stderr)
            code = result.returncode
        except subprocess.TimeoutExpired as error:
            log.write_bytes((error.stdout or b"") + (error.stderr or b""))
            code = "timeout"
        if code == 0 and config.get("kind") == "adb" and mode != "verify":
            produced = output if output is not None else source
            produced.mkdir(parents=True, exist_ok=True)
            for file in ("user.bin", "manifest.txt"):
                subprocess.run(config["adb"] + ["pull", self.path(produced) + "/" + file,
                                               str(produced / file)], check=True, capture_output=True)
        remote_input_sha256 = None
        if code == 0 and config.get("kind") == "adb" and mode == "verify":
            remote_bytes = subprocess.run(config["adb"] + ["exec-out", "cat",
                self.path(source) + "/user.bin"], check=True, capture_output=True).stdout
            remote_input_sha256 = hashlib.sha256(remote_bytes).hexdigest()
        input_unchanged = (mode != "verify" or (
            sha(source / "user.bin") == input_sha256 and
            (remote_input_sha256 is None or remote_input_sha256 == input_sha256)))
        return {"runner": self.name, "operation": mode, "input": source.name,
                "output": output.name if output else None, "exit": code,
                "seconds": round(time.monotonic() - started, 3), "log": str(log),
                "remote_input_sha256": remote_input_sha256,
                "input_sha256": input_sha256, "input_unchanged": input_unchanged,
                "abi": next((line for line in log.read_text(encoding="utf-8", errors="replace").splitlines()
                             if line.startswith("PORTABLE_USER_ABI ")), None)}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--config", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--verify-only", action="store_true", help="Skip mutation and return-transfer cases")
    args = parser.parse_args()
    root = args.output.resolve()
    if root.exists():
        raise SystemExit("Output must be a new isolated directory")
    root.mkdir(parents=True)
    configs = json.loads(args.config.read_text(encoding="utf-8"))
    runners = {name: Runner(name, config, root) for name, config in configs.items()}
    results = []
    seeds = {}
    for name, runner in runners.items():
        profile = root / (name + "-seed")
        result = runner.call("seed", profile)
        results.append(result)
        if result["exit"] == 0:
            seeds[name] = profile
    for source_name, source in seeds.items():
        for target_name, runner in runners.items():
            copied = root / (source_name + "-to-" + target_name)
            shutil.copytree(source, copied)
            result = runner.call("verify", copied)
            results.append(result)
            if result["exit"] == 0 and not args.verify_only:
                changed = root / (source_name + "-via-" + target_name)
                result = runner.call("continue", copied, changed)
                results.append(result)
                if result["exit"] == 0:
                    results.append(runners[source_name].call("verify", changed))
    report = {"platforms": list(runners), "results": results,
              "passed": all(item["exit"] == 0 and item.get("input_unchanged", True) for item in results)}
    (root / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    for result in results:
        print(f"{result['runner']:10} {result['operation']:8} {result['input']:28} {result['exit']}")
    print("PORTABLE_USER_MATRIX", "PASS" if report["passed"] else "FAIL", len(results))
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
