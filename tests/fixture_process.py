"""Run native fixtures directly and Emscripten fixtures through Node."""
import os
import shutil
import subprocess
from pathlib import Path


def copy_fixture(source, destination):
    source, destination = Path(source), Path(destination)
    shutil.copy2(source, destination)
    if source.suffix == ".js":
        for suffix in (".wasm", ".worker.js", ".data"):
            companion = source.with_suffix(suffix)
            if companion.exists():
                shutil.copy2(companion, destination.with_suffix(suffix))


def run_fixture(command, **kwargs):
    command = list(command)
    if command and str(command[0]).endswith(".js"):
        node = os.environ.get("APPTRAVERSE_FIXTURE_NODE") or shutil.which("node")
        if not node:
            raise RuntimeError("Node is required to run an Emscripten fixture")
        command.insert(0, node)
    return subprocess.run(command, **kwargs)
