#!/usr/bin/env python3
"""Native GTK processes verify menu/Add, fullscreen exit and cold LOCAL geometry."""
import pathlib
import os
import subprocess
import sys
import tempfile

with tempfile.TemporaryDirectory(prefix="apptraverse-gtk-menu-add-") as directory:
    profile = pathlib.Path(directory)
    runs = [[], ["--restore"]]
    if os.environ.get("GDK_BACKEND") == "wayland" and "--monitor-output" not in sys.argv[2:]:
        runs.append(["--restore-windowed"])
    for suffix in runs:
        result = subprocess.run([sys.argv[1], str(profile), *sys.argv[2:], *suffix], timeout=40)
        if result.returncode:
            sys.exit(result.returncode)
