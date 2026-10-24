#!/usr/bin/env python3
"""Run load-only Surfaces consumer from an empty directory (embedded supply only)."""

import argparse
import json
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


def read_release(config_path: Path) -> int:
    data = json.loads(config_path.read_text(encoding="utf-8"))
    value = data["release"]
    if not isinstance(value, int) or isinstance(value, bool) or value < 0:
        raise SystemExit(f"invalid release in {config_path}")
    return value


def parse_kv_output(text: str) -> dict[str, str]:
    out: dict[str, str] = {}
    for line in text.splitlines():
        if not line.strip():
            continue
        key, _, value = line.partition("=")
        out[key.strip()] = value.strip()
    return out


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--consumer", type=Path, required=True)
    parser.add_argument("--config", type=Path, required=True)
    args = parser.parse_args()

    expected_release = read_release(args.config.resolve())
    consumer = args.consumer.resolve(strict=True)

    with tempfile.TemporaryDirectory(prefix="surfaces-distilled-") as tmp:
        root = Path(tmp)
        exe = root / consumer.name
        shutil.copy2(consumer, exe)
        completed = subprocess.run(
            [str(exe)],
            cwd=root,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            check=False,
        )
        if completed.returncode != 0:
            raise SystemExit(
                f"consumer failed: exit={completed.returncode}, "
                f"stderr={completed.stderr!r}"
            )
        kv = parse_kv_output(completed.stdout)
        if int(kv.get("EMBEDDED_RELEASE", "-1")) != expected_release:
            raise SystemExit("EMBEDDED_RELEASE mismatch")
        if kv.get("SURFACE_COUNT") != "1":
            raise SystemExit(f"SURFACE_COUNT expected 1, got {kv.get('SURFACE_COUNT')}")
        if kv.get("SURFACE_NUMBER") != "1":
            raise SystemExit(f"SURFACE_NUMBER expected 1, got {kv.get('SURFACE_NUMBER')}")
        if kv.get("LABEL_ADD_EN") != "Add":
            raise SystemExit(f"LABEL_ADD_EN expected Add, got {kv.get('LABEL_ADD_EN')}")
        supply_count = int(kv.get("SUPPLY_CONSTANT_COUNT", "0"))
        if supply_count < 8:
            raise SystemExit(f"SUPPLY_CONSTANT_COUNT expected >=8, got {supply_count}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
