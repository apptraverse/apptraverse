#!/usr/bin/env python3
"""Cross-process Surfaces Demo EXE A (v0) / EXE B (v1 title suffix layer)."""

import argparse
import json
from pathlib import Path
import shutil
import subprocess
import tempfile

from application_evolution_matrix import CheckFailed, digest, manifest, unchanged


class SurfacesDemoMatrix:
    def __init__(self, args):
        self.executables = {
            "A": args.a.resolve(strict=True),
            "B": args.b.resolve(strict=True),
        }
        self.exe_hashes = {
            rev: digest(path) for rev, path in self.executables.items()
        }
        self.root = Path(
            tempfile.mkdtemp(prefix="surfaces-demo-v0-v1-", dir=args.work_root)
        )
        self.timeout = args.timeout
        self.count = 0
        self.results = []
        self.class_id = None
        self.surface1_id = 3
        print("Run directory: " + str(self.root), flush=True)

    def run(self, revision, operation, directory, expect_exit=0):
        command = [
            str(self.executables[revision]),
            "--state-dir",
            str(directory),
            "--evolution-op",
            operation,
        ]
        before = manifest(directory)
        self.count += 1
        try:
            completed = subprocess.run(
                command,
                capture_output=True,
                text=True,
                encoding="utf-8",
                errors="replace",
                timeout=self.timeout,
                cwd=self.root,
            )
        except subprocess.TimeoutExpired as error:
            raise CheckFailed(f"{revision} {operation}: timeout") from error
        after = manifest(directory)
        log = self.root / f"process-{self.count:03d}-{revision}-{operation}.json"
        log.write_text(
            json.dumps(
                {
                    "command": command,
                    "exit_code": completed.returncode,
                    "stdout": completed.stdout,
                    "stderr": completed.stderr,
                    "before": before,
                    "after": after,
                },
                indent=2,
            ),
            encoding="utf-8",
        )
        if completed.returncode != expect_exit:
            raise CheckFailed(
                f"{revision} {operation}: exit={completed.returncode}, "
                f"expected={expect_exit}, stderr={completed.stderr!r}"
            )
        if operation == "inspect" or completed.returncode != 0:
            unchanged(before, after, f"{revision} {operation}")
        if completed.returncode != 0:
            return None, completed.stderr
        if operation in ("package", "install-bundled"):
            return None, ""
        record = None
        for line in completed.stdout.splitlines():
            try:
                candidate = json.loads(line)
            except ValueError:
                continue
            if isinstance(candidate, dict) and candidate.get("exe") in ("A", "B"):
                record = candidate
        if record is None:
            raise CheckFailed(f"invalid fixture JSON: {completed.stdout!r}")
        if record.get("exe") != revision:
            raise CheckFailed("EXE mismatch: " + str(record))
        if type(record.get("class_id")) is not int or record["class_id"] <= 0:
            raise CheckFailed("invalid ClassId")
        if self.class_id is None:
            self.class_id = record["class_id"]
        elif self.class_id != record["class_id"]:
            raise CheckFailed("ClassId differs between A and B")
        return record, ""

    def surface1(self, record):
        for entry in record.get("surfaces", []):
            if entry.get("object_id") == self.surface1_id:
                return entry
        raise CheckFailed("Surface1 missing from inspect JSON")

    def slot(self, directory, version):
        return directory / str(self.surface1_id) / str(self.class_id) / str(version)

    def layers(self, directory, versions):
        actual = {
            p.name
            for p in self.slot(directory, 0).parent.iterdir()
            if p.is_file()
        }
        if actual != {str(v) for v in versions}:
            raise CheckFailed(
                f"Surface1 version slots: expected {versions}, got {actual}"
            )

    def verify(self, revision, directory, x, y, suffix=None):
        record, _ = self.run(revision, "inspect", directory)
        surface = self.surface1(record)
        if surface["desktop_x"] != x or surface["desktop_y"] != y:
            raise CheckFailed(f"{revision} bounds mismatch: {surface}")
        if revision == "B":
            if surface.get("window_title_suffix") != suffix:
                raise CheckFailed(
                    f"B suffix: expected {suffix!r}, got {surface.get('window_title_suffix')!r}"
                )
        elif "window_title_suffix" in surface:
            raise CheckFailed("v0 EXE unexpectedly reads v1 suffix")

    def prepare(self, case_dir):
        state = case_dir / "user profile with spaces"
        package = case_dir / "packaged B initial state"
        self.run("A", "create", state)
        self.verify("A", state, 120, 120)
        self.layers(state, {0})
        _, reason = self.run("B", "inspect", state, expect_exit=2)
        if "MISSING_STATE_LAYER" not in reason or "version=1" not in reason:
            raise CheckFailed("B did not reject missing packaged v1 state")
        old_v0_hash = digest(self.slot(state, 0))
        self.run("B", "package", package)
        packaged_layer = self.slot(package, 1)
        if not packaged_layer.is_file() or self.slot(package, 0).exists():
            raise CheckFailed("B package must contain v1, not v0 Surface state")
        package_hash = digest(packaged_layer)
        target = self.slot(state, 1)
        if target.exists():
            raise CheckFailed("installation would overwrite existing v1")
        shutil.copyfile(packaged_layer, target)
        if digest(self.slot(state, 0)) != old_v0_hash:
            raise CheckFailed("installing v1 changed the user's v0 layer")
        if digest(target) != package_hash:
            raise CheckFailed("installed v1 bytes differ from packaged state")
        self.layers(state, {0, 1})
        self.verify("A", state, 120, 120)
        self.verify("B", state, 120, 120, " [demo-v1]")
        return state, package, package_hash

    def case(self, name, callback):
        case_dir = self.root / f"{len(self.results) + 1:02d}-{name}"
        case_dir.mkdir()
        try:
            detail = callback(case_dir)
            result = {"name": name, "result": "PASS", "detail": detail}
        except (OSError, ValueError, CheckFailed) as error:
            result = {"name": name, "result": "FAIL", "detail": str(error)}
        self.results.append(result)
        print(f"{result['result']} {name}: {result['detail']}", flush=True)

    def rollback(self, case_dir):
        state, package, package_hash = self.prepare(case_dir)
        x, y = 120, 120
        suffix = " [demo-v1]"
        for revision, operation in (
            ("B", "set-suffix"),
            ("A", "move"),
            ("B", "move"),
            ("A", "move"),
            ("B", "set-suffix"),
            ("A", "move"),
            ("B", "set-suffix"),
            ("B", "move"),
            ("A", "move"),
            ("B", "set-suffix"),
        ):
            v0_before, v1_before = (
                digest(self.slot(state, 0)),
                digest(self.slot(state, 1)),
            )
            record, _ = self.run(revision, operation, state)
            surface = self.surface1(record)
            x, y = surface["desktop_x"], surface["desktop_y"]
            if operation == "set-suffix":
                suffix = surface["window_title_suffix"]
                if digest(self.slot(state, 0)) != v0_before:
                    raise CheckFailed("set-suffix changed v0 layer bytes")
                if digest(self.slot(state, 1)) == v1_before:
                    raise CheckFailed("set-suffix did not update v1")
            else:
                if digest(self.slot(state, 0)) == v0_before:
                    raise CheckFailed("move did not update v0")
                if digest(self.slot(state, 1)) != v1_before:
                    raise CheckFailed("move overwrote v1 suffix layer")
            self.layers(state, {0, 1})
            self.verify("A", state, x, y)
            self.verify("B", state, x, y, suffix)
        if digest(self.slot(package, 1)) != package_hash:
            raise CheckFailed("immutable packaged v1 source changed")
        return f"mixed A/B rollback: ({x},{y}), suffix={suffix!r}"

    def finish(self):
        for revision, expected_hash in self.exe_hashes.items():
            if digest(self.executables[revision]) != expected_hash:
                self.results.append(
                    {
                        "name": "exe-immutability",
                        "result": "FAIL",
                        "detail": f"EXE {revision} changed during test",
                    }
                )
        (self.root / "results.json").write_text(
            json.dumps(
                {
                    "results": self.results,
                    "processes": self.count,
                    "exe_sha256": self.exe_hashes,
                },
                indent=2,
            ),
            encoding="utf-8",
        )
        failures = sum(r["result"] == "FAIL" for r in self.results)
        print(
            f"{len(self.results)-failures} PASS / {failures} FAIL, "
            f"{self.count} processes",
            flush=True,
        )
        return failures == 0


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--a", required=True, type=Path)
    parser.add_argument("--b", required=True, type=Path)
    parser.add_argument("--work-root", required=True, type=Path)
    parser.add_argument("--timeout", type=int, default=60)
    args = parser.parse_args()
    args.work_root.mkdir(parents=True, exist_ok=True)
    matrix = SurfacesDemoMatrix(args)
    def supplied_layer(case_dir):
        matrix.prepare(case_dir)
        return "B restores suffix from a real serialized v1 layer; A sees only v0"

    matrix.case("supplied-v1-layer", supplied_layer)
    matrix.case("rollback", matrix.rollback)
    raise SystemExit(0 if matrix.finish() else 1)


if __name__ == "__main__":
    main()
