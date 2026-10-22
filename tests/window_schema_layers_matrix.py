#!/usr/bin/env python3
"""Two native EXEs: one ClassId, independent v0 position/v1 size layers."""

import argparse
import json
from pathlib import Path
import shutil
import subprocess
import tempfile

from application_evolution_matrix import CheckFailed, digest, manifest, unchanged


class WindowMatrix:
    def __init__(self, args):
        self.executables = {"A": args.a.resolve(strict=True),
                            "B": args.b.resolve(strict=True),
                            "C": args.c.resolve(strict=True)}
        self.exe_hashes = {rev: digest(path) for rev, path in self.executables.items()}
        self.root = Path(tempfile.mkdtemp(prefix="window-v0-v1-", dir=args.work_root))
        self.timeout = args.timeout
        self.count = 0
        self.results = []
        self.class_id = None
        print("Run directory: " + str(self.root), flush=True)

    def run(self, revision, operation, directory, expect_exit=0):
        command = [str(self.executables[revision]), "--state-dir", str(directory),
                   "--op", operation]
        before = manifest(directory)
        self.count += 1
        try:
            completed = subprocess.run(command, capture_output=True, text=True,
                                       encoding="utf-8", errors="replace",
                                       timeout=self.timeout, cwd=self.root)
        except subprocess.TimeoutExpired as error:
            raise CheckFailed(f"{revision} {operation}: timeout") from error
        after = manifest(directory)
        log = self.root / f"process-{self.count:03d}-{revision}-{operation}.json"
        log.write_text(json.dumps({"command": command, "exit_code": completed.returncode,
                                   "stdout": completed.stdout, "stderr": completed.stderr,
                                   "before": before, "after": after}, indent=2), encoding="utf-8")
        if completed.returncode != expect_exit:
            raise CheckFailed(f"{revision} {operation}: exit={completed.returncode}, "
                              f"expected={expect_exit}, stderr={completed.stderr!r}")
        if operation == "inspect" or completed.returncode != 0:
            unchanged(before, after, f"{revision} {operation}")
        if completed.returncode != 0:
            return None, completed.stderr
        record = None
        for line in completed.stdout.splitlines():
            try:
                candidate = json.loads(line)
            except ValueError:
                continue
            if isinstance(candidate, dict) and candidate.get("exe") in ("A", "B", "C"):
                record = candidate
        if record is None:
            raise CheckFailed(f"invalid fixture JSON: {completed.stdout!r}")
        if record.get("exe") != revision or record.get("object_id") != 92001:
            raise CheckFailed("EXE/ObjId mismatch: " + str(record))
        if type(record.get("class_id")) is not int or record["class_id"] <= 0:
            raise CheckFailed("invalid ClassId")
        if self.class_id is None:
            self.class_id = record["class_id"]
        elif self.class_id != record["class_id"]:
            raise CheckFailed("ClassId differs between EXEs")
        return record, ""

    def verify(self, revision, directory, x, y, width=None, height=None,
               border_width=None):
        record, _ = self.run(revision, "inspect", directory)
        expected = {"x": x, "y": y}
        if revision == "B":
            expected.update(width=width, height=height)
            if "border_width" in record:
                raise CheckFailed("v1 EXE unexpectedly reads v2 fields")
        elif revision == "C":
            expected.update(width=width, height=height, border_width=border_width)
        elif "width" in record or "height" in record:
            raise CheckFailed("v0 EXE unexpectedly reads v1 fields")
        if revision == "A" and "border_width" in record:
            raise CheckFailed("v0 EXE unexpectedly reads v2 fields")
        for key, value in expected.items():
            if type(record.get(key)) is not int or record[key] != value:
                raise CheckFailed(f"{revision} {key}: expected {value}, got {record.get(key)}")

    def slot(self, directory, version):
        return directory / "92001" / str(self.class_id) / str(version)

    def layers(self, directory, versions):
        actual = {p.name for p in self.slot(directory, 0).parent.iterdir() if p.is_file()}
        if actual != {str(v) for v in versions}:
            raise CheckFailed(f"Window version slots: expected {versions}, got {actual}")

    def prepare(self, case_dir):
        state = case_dir / "user profile with spaces"
        package = case_dir / "packaged B initial state"
        self.run("A", "create", state)
        self.verify("A", state, 10, 20)
        self.layers(state, {0})
        _, reason = self.run("B", "inspect", state, expect_exit=2)
        if "MISSING_STATE_LAYER" not in reason or "version=1" not in reason:
            raise CheckFailed("B did not reject missing packaged v1 state")
        old_v0_hash = digest(self.slot(state, 0))
        self.run("B", "package", package)
        packaged_layer = self.slot(package, 1)
        if not packaged_layer.is_file() or self.slot(package, 0).exists():
            raise CheckFailed("B package must contain v1, not v0 Window state")
        package_hash = digest(packaged_layer)
        target = self.slot(state, 1)
        if target.exists():
            raise CheckFailed("installation would overwrite existing v1")
        shutil.copyfile(packaged_layer, target)
        if digest(self.slot(state, 0)) != old_v0_hash:
            raise CheckFailed("installing v1 changed the user's v0 position")
        if digest(target) != package_hash:
            raise CheckFailed("installed v1 bytes differ from packaged state")
        self.layers(state, {0, 1})
        self.verify("B", state, 10, 20, 640, 480)
        self.verify("A", state, 10, 20)
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

    def supplied_layer(self, case_dir):
        state, package, original_hash = self.prepare(case_dir)
        if digest(self.slot(package, 1)) != original_hash:
            raise CheckFailed("bundled v1 state changed")
        return "B restores dimensions from a real serialized v1 layer; A sees only v0"

    def rollback(self, case_dir):
        state, package, package_hash = self.prepare(case_dir)
        x, y, width, height = 10, 20, 640, 480
        for revision, operation in (("B", "resize"), ("A", "move"),
                                    ("B", "move"), ("A", "move"),
                                    ("B", "resize"), ("A", "move"),
                                    ("B", "resize"), ("B", "move"),
                                    ("A", "move"), ("B", "resize")):
            v0_before, v1_before = digest(self.slot(state, 0)), digest(self.slot(state, 1))
            record, _ = self.run(revision, operation, state)
            if operation == "resize":
                width += 100
                height += 50
                if digest(self.slot(state, 0)) != v0_before:
                    raise CheckFailed("resize changed v0 position bytes")
                if digest(self.slot(state, 1)) == v1_before:
                    raise CheckFailed("resize did not update v1")
            else:
                dx, dy = (7, 9) if revision == "A" else (13, 17)
                x += dx
                y += dy
                if digest(self.slot(state, 0)) == v0_before:
                    raise CheckFailed("move did not update v0")
                if digest(self.slot(state, 1)) != v1_before:
                    raise CheckFailed("move overwrote untouched v1 size bytes")
            expected = {"x": x, "y": y}
            if revision == "B":
                expected.update(width=width, height=height)
            for key, value in expected.items():
                if record[key] != value:
                    raise CheckFailed(f"{revision} {operation} {key}: {record[key]} != {value}")
            self.layers(state, {0, 1})
            self.verify("A", state, x, y)
            self.verify("B", state, x, y, width, height)
        if digest(self.slot(package, 1)) != package_hash:
            raise CheckFailed("immutable packaged v1 source changed")
        return f"mixed A/B rollback: ({x},{y}), {width}x{height}, separate v0/v1 slots"

    def save_root_dual_layer(self, case_dir):
        state, package, package_hash = self.prepare(case_dir)
        v0_before, v1_before = digest(self.slot(state, 0)), digest(self.slot(state, 1))
        self.run("B", "move-resize", state)
        v0_after_resize = digest(self.slot(state, 0))
        v1_after_resize = digest(self.slot(state, 1))
        if v0_after_resize == v0_before:
            raise CheckFailed("move-resize did not update v0 position bytes")
        if v1_after_resize == v1_before:
            raise CheckFailed("move-resize did not update v1 size bytes")
        self.verify("B", state, 23, 37, 740, 530)
        self.verify("A", state, 23, 37)
        v1_frozen = digest(self.slot(state, 1))
        self.run("A", "move", state)
        if digest(self.slot(state, 1)) != v1_frozen:
            raise CheckFailed("A move changed untouched v1 bytes after SaveRoot")
        if digest(self.slot(state, 0)) == v0_after_resize:
            raise CheckFailed("A move did not update v0")
        self.verify("A", state, 30, 46)
        self.verify("B", state, 30, 46, 740, 530)
        if digest(self.slot(package, 1)) != package_hash:
            raise CheckFailed("immutable packaged v1 source changed")
        return "SaveRoot dual-layer: B move-resize then A move preserves v1 bytes"

    def three_version_rollback(self, case_dir):
        state, package_b, package_b_hash = self.prepare(case_dir)
        package_c = case_dir / "packaged C initial state"
        self.run("B", "resize", state)
        self.verify("B", state, 10, 20, 740, 530)
        self.verify("A", state, 10, 20)
        _, reason = self.run("C", "inspect", state, expect_exit=2)
        if "MISSING_STATE_LAYER" not in reason or "version=2" not in reason:
            raise CheckFailed("C did not reject missing packaged v2 state")
        self.run("C", "package", package_c)
        packaged_v2 = self.slot(package_c, 2)
        if (not packaged_v2.is_file() or self.slot(package_c, 0).exists() or
                self.slot(package_c, 1).exists()):
            raise CheckFailed("C package must contain v2, not v0/v1 Window state")
        package_c_hash = digest(packaged_v2)
        v0_frozen, v1_frozen = digest(self.slot(state, 0)), digest(self.slot(state, 1))
        target_v2 = self.slot(state, 2)
        if target_v2.exists():
            raise CheckFailed("installation would overwrite existing v2")
        shutil.copyfile(packaged_v2, target_v2)
        if digest(self.slot(state, 0)) != v0_frozen:
            raise CheckFailed("installing v2 changed the user's v0 position")
        if digest(self.slot(state, 1)) != v1_frozen:
            raise CheckFailed("installing v2 changed the user's v1 size")
        if digest(target_v2) != package_c_hash:
            raise CheckFailed("installed v2 bytes differ from packaged state")
        self.layers(state, {0, 1, 2})
        self.verify("C", state, 10, 20, 740, 530, 4)
        v2_before = digest(self.slot(state, 2))
        self.run("C", "change-border", state)
        if digest(self.slot(state, 0)) != v0_frozen:
            raise CheckFailed("change-border changed v0 position bytes")
        if digest(self.slot(state, 1)) != v1_frozen:
            raise CheckFailed("change-border changed v1 size bytes")
        if digest(self.slot(state, 2)) == v2_before:
            raise CheckFailed("change-border did not update v2")
        self.verify("C", state, 10, 20, 740, 530, 6)
        v1_frozen = digest(self.slot(state, 1))
        v2_frozen = digest(self.slot(state, 2))
        self.run("A", "move", state)
        if digest(self.slot(state, 1)) != v1_frozen:
            raise CheckFailed("A move changed untouched v1 bytes")
        if digest(self.slot(state, 2)) != v2_frozen:
            raise CheckFailed("A move changed untouched v2 bytes")
        self.verify("A", state, 17, 29)
        v0_frozen = digest(self.slot(state, 0))
        v2_frozen = digest(self.slot(state, 2))
        self.run("B", "resize", state)
        if digest(self.slot(state, 0)) != v0_frozen:
            raise CheckFailed("B resize changed v0 position bytes")
        if digest(self.slot(state, 2)) != v2_frozen:
            raise CheckFailed("B resize changed untouched v2 bytes")
        self.verify("B", state, 17, 29, 840, 580)
        v0_frozen = digest(self.slot(state, 0))
        v1_frozen = digest(self.slot(state, 1))
        self.run("C", "change-border", state)
        if digest(self.slot(state, 0)) != v0_frozen:
            raise CheckFailed("final change-border changed v0")
        if digest(self.slot(state, 1)) != v1_frozen:
            raise CheckFailed("final change-border changed v1")
        self.verify("C", state, 17, 29, 840, 580, 8)
        self.verify("A", state, 17, 29)
        self.verify("B", state, 17, 29, 840, 580)
        self.layers(state, {0, 1, 2})
        if digest(self.slot(package_b, 1)) != package_b_hash:
            raise CheckFailed("immutable packaged v1 source changed")
        if digest(self.slot(package_c, 2)) != package_c_hash:
            raise CheckFailed("immutable packaged v2 source changed")
        return "A/B/C rollback: (17,29), 840x580, border 8; three Window slots"

    def finish(self):
        for revision, expected_hash in self.exe_hashes.items():
            if digest(self.executables[revision]) != expected_hash:
                self.results.append({"name": "exe-immutability", "result": "FAIL",
                                     "detail": f"EXE {revision} changed during test"})
        (self.root / "results.json").write_text(json.dumps({
            "results": self.results, "processes": self.count,
            "exe_sha256": self.exe_hashes}, indent=2), encoding="utf-8")
        failures = sum(r["result"] == "FAIL" for r in self.results)
        print(f"{len(self.results)-failures} PASS / {failures} FAIL, "
              f"{self.count} processes", flush=True)
        return 1 if failures else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--a", type=Path, required=True)
    parser.add_argument("--b", type=Path, required=True)
    parser.add_argument("--c", type=Path, required=True)
    parser.add_argument("--work-root", type=Path, default=None)
    parser.add_argument("--timeout", type=float, default=30)
    args = parser.parse_args()
    if args.timeout <= 0:
        parser.error("--timeout must be positive")
    if args.work_root is not None and not args.work_root.is_dir():
        parser.error("--work-root must be an existing directory")
    suite = WindowMatrix(args)
    suite.case("new-exe-supplies-v1-state", suite.supplied_layer)
    suite.case("mixed-upgrade-rollback", suite.rollback)
    suite.case("save-root-dual-layer", suite.save_root_dual_layer)
    suite.case("three-version-rollback", suite.three_version_rollback)
    return suite.finish()


if __name__ == "__main__":
    raise SystemExit(main())
