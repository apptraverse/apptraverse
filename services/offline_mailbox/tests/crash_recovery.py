# SPDX-License-Identifier: Apache-2.0
import pathlib
import subprocess
import sys
import tempfile

with tempfile.TemporaryDirectory(prefix="apptraverse-mailbox-crash-") as root:
    db = str(pathlib.Path(root) / "db")
    subprocess.run([sys.argv[1], "--crash-write", db], check=True, timeout=30)
    subprocess.run([sys.argv[1], "--check-crash", db], check=True, timeout=30)
print("WAL survived abrupt writer process exit")
