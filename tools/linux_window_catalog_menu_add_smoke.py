#!/usr/bin/env python3
"""Two native GTK processes verify menu/Add and cold LOCAL restoration."""
import pathlib
import subprocess
import sys
import tempfile

with tempfile.TemporaryDirectory(prefix="apptraverse-gtk-menu-add-") as directory:
    profile = pathlib.Path(directory)
    for suffix in ([], ["--restore"]):
        result = subprocess.run([sys.argv[1], str(profile), *sys.argv[2:], *suffix], timeout=40)
        if result.returncode:
            sys.exit(result.returncode)
