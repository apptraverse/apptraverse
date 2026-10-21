#!/usr/bin/env python3
"""Real Win32 Surfaces Demo lifecycle: A→B→A→B via WinApp session + Application::Save."""

import argparse
import json
from pathlib import Path
import shutil
import subprocess
import tempfile

from application_evolution_matrix import CheckFailed, digest, manifest

# Fixed expectations — never derived from a prior EXE response.
A1_BOUNDS = (201, 202, 401, 301)
A2_BOUNDS = (311, 322, 411, 311)
B_SUFFIX = " [lifecycle-b1]"
SURFACE1_OBJECT_ID = 3


class LifecycleMatrix:
    def __init__(self, args):
        self.executables = {
            "A": args.a.resolve(strict=True),
            "B": args.b.resolve(strict=True),
        }
        self.root = Path(
            tempfile.mkdtemp(prefix="surfaces-lifecycle-", dir=args.work_root)
        )
        self.state_dir = self.root / "lifecycle user state"
        self.timeout = args.timeout
        self.class_id = None
        self.v1_sha_after_b = None
        print("Run directory: " + str(self.root), flush=True)

    def slot(self, version):
        if self.class_id is None:
            raise CheckFailed("class_id not established")
        return self.state_dir / str(SURFACE1_OBJECT_ID) / str(self.class_id) / str(
            version
        )

    def run_gui(self, revision, automation):
        command = [
            str(self.executables[revision]),
            "--state-dir",
            str(self.state_dir),
            "--session-automation",
            automation,
        ]
        completed = subprocess.run(
            command,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=self.timeout,
        )
        log = self.root / f"gui-{revision}-{automation[:40]}.json"
        log.write_text(
            json.dumps(
                {
                    "command": command,
                    "exit_code": completed.returncode,
                    "stdout": completed.stdout,
                    "stderr": completed.stderr,
                    "manifest": manifest(self.state_dir),
                },
                indent=2,
            ),
            encoding="utf-8",
        )
        return completed

    def inspect(self, revision):
        command = [
            str(self.executables[revision]),
            "--state-dir",
            str(self.state_dir),
            "--session-inspect",
        ]
        completed = subprocess.run(
            command,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=self.timeout,
        )
        record = None
        for line in completed.stdout.splitlines():
            try:
                candidate = json.loads(line)
            except ValueError:
                continue
            if isinstance(candidate, dict) and "surfaces" in candidate:
                record = candidate
        if record is None:
            if completed.returncode != 0:
                return completed.returncode, {
                    "load_ok": False,
                    "load_error": completed.stderr.strip() or f"exit {completed.returncode}",
                    "surfaces": [],
                }
            raise CheckFailed(
                f"{revision} inspect: no JSON (exit={completed.returncode}, "
                f"stderr={completed.stderr!r})"
            )
        if self.class_id is None and record.get("class_id"):
            self.class_id = record["class_id"]
        return completed.returncode, record

    def surface1(self, record):
        for entry in record.get("surfaces", []):
            if entry.get("object_id") == SURFACE1_OBJECT_ID:
                return entry
        raise CheckFailed("Surface1 missing from inspect")

    def expect_bounds(self, record, bounds, label):
        surface = self.surface1(record)
        expected = {
            "desktop_x": bounds[0],
            "desktop_y": bounds[1],
            "desktop_width": bounds[2],
            "desktop_height": bounds[3],
        }
        for key, value in expected.items():
            if surface.get(key) != value:
                raise CheckFailed(f"{label}: {key} expected {value}, got {surface}")

    def run(self):
        shutil.rmtree(self.state_dir, ignore_errors=True)

        # A: bootstrap + bounds via SetWindowPos → QueueCurrentBounds → Event.
        a1 = self.run_gui(
            "A",
            f"set-bounds={A1_BOUNDS[0]},{A1_BOUNDS[1]},{A1_BOUNDS[2]},{A1_BOUNDS[3]};shutdown",
        )
        if a1.returncode != 0:
            raise CheckFailed(f"A1 GUI failed exit={a1.returncode}")
        code, rec = self.inspect("A")
        if code != 0 or not rec.get("load_ok", True):
            raise CheckFailed("A1 inspect after save failed")
        self.expect_bounds(rec, A1_BOUNDS, "A1")
        if self.slot(1).exists():
            raise CheckFailed("A1 must not create v1 layer")

        # B: packaged v1 install + suffix via model proxy Event path.
        b1 = self.run_gui(
            "B",
            f"set-suffix={B_SUFFIX};verify-title-suffix={B_SUFFIX};shutdown",
        )
        if b1.returncode != 0:
            raise CheckFailed(f"B1 GUI failed exit={b1.returncode}")
        code, rec = self.inspect("B")
        if code != 0:
            raise CheckFailed("B1 inspect failed")
        self.expect_bounds(rec, A1_BOUNDS, "B1 bounds from A")
        surface = self.surface1(rec)
        if surface.get("window_title_suffix") != B_SUFFIX:
            raise CheckFailed(f"B1 suffix expected {B_SUFFIX!r}")
        if surface.get("suffix_event_count", 0) < 1:
            raise CheckFailed("B1 journal missing SurfaceWindowTitleSuffixChangedEvent")
        if not self.slot(1).is_file():
            raise CheckFailed("B1 did not persist v1 layer")
        self.v1_sha_after_b = digest(self.slot(1))

        # A: rollback step — may FAIL if unknown Event is not registered on A.
        a2 = self.run_gui(
            "A",
            f"set-bounds={A2_BOUNDS[0]},{A2_BOUNDS[1]},{A2_BOUNDS[2]},{A2_BOUNDS[3]};shutdown",
        )
        a2_ok = a2.returncode == 0
        a2_load_ok = False
        a2_detail = ""
        if a2_ok:
            code, rec = self.inspect("A")
            a2_load_ok = code == 0 and rec.get("load_ok", False)
            if a2_load_ok:
                self.expect_bounds(rec, A2_BOUNDS, "A2")
            else:
                a2_detail = rec.get("load_error", f"inspect exit {code}")
        else:
            a2_detail = f"GUI exit {a2.returncode}"

        if digest(self.slot(1)) != self.v1_sha_after_b:
            raise CheckFailed("A2 changed v1 layer bytes (must preserve unknown v1)")

        if not a2_ok or not a2_load_ok:
            return {
                "result": "FAIL",
                "stage": "A2 rollback",
                "detail": a2_detail,
                "a2_gui_exit": a2.returncode,
                "v1_preserved": True,
            }

        _, rec_after_a2_a = self.inspect("A")
        _, rec_after_a2_b = self.inspect("B")
        if not rec_after_a2_b.get("load_ok", False):
            return {
                "result": "FAIL",
                "stage": "B reload after A2",
                "detail": rec_after_a2_b.get("load_error", "LoadRoot failed"),
                "transitions": {
                    "A1_bootstrap_bounds_save": "PASS",
                    "B1_suffix_event_hwnd_save": "PASS",
                    "A2_bounds_save": "PASS",
                    "A_inspect_after_A2": "PASS"
                    if rec_after_a2_a.get("load_ok")
                    else rec_after_a2_a.get("load_error", "FAIL"),
                    "B_inspect_after_A2": "FAIL",
                    "B2_gui": "NOT_RUN",
                },
                "suffix_event_class_id_note": "see SurfaceWindowTitleSuffixChangedEvent in OBJ_SYS / B1 inspect",
                "v1_sha256": self.v1_sha_after_b,
                "v1_unchanged_after_a2": digest(self.slot(1)) == self.v1_sha_after_b,
            }

        # B: confirm A2 bounds + B suffix restored from persisted v1.
        b2 = self.run_gui(
            "B",
            f"verify-bounds={A2_BOUNDS[0]},{A2_BOUNDS[1]},{A2_BOUNDS[2]},{A2_BOUNDS[3]};"
            f"verify-title-suffix={B_SUFFIX};shutdown",
        )
        if b2.returncode != 0:
            raise CheckFailed(f"B2 GUI failed exit={b2.returncode}")
        code, rec = self.inspect("B")
        if code != 0:
            raise CheckFailed("B2 inspect failed")
        self.expect_bounds(rec, A2_BOUNDS, "B2")
        if self.surface1(rec).get("window_title_suffix") != B_SUFFIX:
            raise CheckFailed("B2 suffix not restored from saved v1")
        if digest(self.slot(1)) != self.v1_sha_after_b:
            raise CheckFailed("B2 overwrote v1 layer unexpectedly")

        return {
            "result": "PASS",
            "detail": "full A→B→A→B lifecycle with Event suffix and Application::Save",
        }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--a", required=True, type=Path)
    parser.add_argument("--b", required=True, type=Path)
    parser.add_argument("--work-root", required=True, type=Path)
    parser.add_argument("--timeout", type=int, default=120)
    args = parser.parse_args()
    args.work_root.mkdir(parents=True, exist_ok=True)
    matrix = LifecycleMatrix(args)
    try:
        outcome = matrix.run()
    except (OSError, ValueError, CheckFailed) as error:
        outcome = {"result": "FAIL", "detail": str(error)}
    out = matrix.root / "lifecycle_result.json"
    out.write_text(json.dumps(outcome, indent=2), encoding="utf-8")
    print(f"{outcome['result']}: {outcome.get('detail', outcome)}", flush=True)
    raise SystemExit(0 if outcome.get("result") == "PASS" else 1)


if __name__ == "__main__":
    main()
