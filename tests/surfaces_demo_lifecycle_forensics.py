#!/usr/bin/env python3
"""Capture storage manifests through Surfaces lifecycle checkpoints."""

import argparse
import json
import shutil
import subprocess
import tempfile
from pathlib import Path

from application_evolution_matrix import manifest

A1 = f"set-bounds=201,202,401,301;shutdown"
B1 = f"set-suffix= [lifecycle-b1];verify-title-suffix= [lifecycle-b1];shutdown"
A2 = f"set-bounds=311,322,411,311;shutdown"

CHECKPOINTS = ("after_A1", "after_B1", "after_A2", "before_B2")


def run_gui(exe, state_dir, automation, timeout):
    return subprocess.run(
        [str(exe), "--state-dir", str(state_dir), "--session-automation", automation],
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        timeout=timeout,
    )


def snapshot(state_dir, label, out_root):
    dest = out_root / label
    if dest.exists():
        shutil.rmtree(dest)
    shutil.copytree(state_dir, dest)
    (out_root / f"{label}_manifest.json").write_text(
        json.dumps(manifest(state_dir), indent=2, sort_keys=True), encoding="utf-8"
    )


def diff_manifests(before, after):
    only_after = {k: after[k] for k in after if k not in before}
    only_before = {k: before[k] for k in before if k not in after}
    changed = {
        k: {"before": before[k], "after": after[k]}
        for k in before
        if k in after and before[k] != after[k]
    }
    return {"added": only_after, "removed": only_before, "changed": changed}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--a", type=Path, required=True)
    parser.add_argument("--b", type=Path, required=True)
    parser.add_argument("--work-root", type=Path, required=True)
    parser.add_argument("--timeout", type=int, default=120)
    args = parser.parse_args()
    args.work_root.mkdir(parents=True, exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix="surfaces-forensics-", dir=args.work_root))
    state = root / "state"
    state.mkdir()
    out = root / "artifacts"
    out.mkdir()

    run_gui(args.a, state, A1, args.timeout)
    snapshot(state, CHECKPOINTS[0], out)
    m_a1 = manifest(state)

    run_gui(args.b, state, B1, args.timeout)
    snapshot(state, CHECKPOINTS[1], out)
    m_b1 = manifest(state)

    run_gui(args.a, state, A2, args.timeout)
    snapshot(state, CHECKPOINTS[2], out)
    m_a2 = manifest(state)

    snapshot(state, CHECKPOINTS[3], out)

    report = {
        "root": str(root),
        "diff_B1_vs_A1": diff_manifests(m_a1, m_b1),
        "diff_A2_vs_B1": diff_manifests(m_b1, m_a2),
    }
    (out / "forensics_report.json").write_text(
        json.dumps(report, indent=2, sort_keys=True), encoding="utf-8"
    )
    print(json.dumps(report, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
