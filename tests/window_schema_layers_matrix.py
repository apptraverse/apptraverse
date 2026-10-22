#!/usr/bin/env python3
"""Two native EXEs: one ClassId, independent v0 position/v1 size layers."""

import argparse
import json
from pathlib import Path
import shutil
import subprocess
import tempfile

from application_evolution_matrix import CheckFailed, digest, manifest, unchanged

# Frozen Window layer bytes (Debug/Release identical); A create, B/C package ops.
EXPECTED_WINDOW_LAYER_SHA256 = {
    0: "0d9330506fd1d6da3fbc3a5b83eb6dd05f86bdad7ce710c0fae1d68955f03fb4",
    1: "da75e0116e19d6ec0ac0951d9c53268415e841d95a2591651e3317b7648710c4",
    2: "fb5e512425fc9449316ec95969ebe71e2d576dbab833d61e2a5b9330fd70ee02",
}


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

    def run(self, revision, operation, directory, expect_exit=0,
            backing_dir=None, embedded_backing=False):
        command = [str(self.executables[revision]), "--state-dir", str(directory),
                   "--op", operation]
        if backing_dir is not None:
            command.extend(["--backing-dir", str(backing_dir)])
        if embedded_backing:
            command.append("--embedded-backing")
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
               border_width=None, backing_dir=None, embedded_backing=False):
        record, _ = self.run(revision, "inspect", directory,
                             backing_dir=backing_dir,
                             embedded_backing=embedded_backing)
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
        if digest(self.slot(state, 0)) != EXPECTED_WINDOW_LAYER_SHA256[0]:
            raise CheckFailed("Window v0 golden SHA256 mismatch")
        _, reason = self.run("B", "inspect", state, expect_exit=2)
        if "MISSING_STATE_LAYER" not in reason or "version=1" not in reason:
            raise CheckFailed("B did not reject missing packaged v1 state")
        old_v0_hash = digest(self.slot(state, 0))
        self.run("B", "package", package)
        packaged_layer = self.slot(package, 1)
        if not packaged_layer.is_file() or self.slot(package, 0).exists():
            raise CheckFailed("B package must contain v1, not v0 Window state")
        if digest(packaged_layer) != EXPECTED_WINDOW_LAYER_SHA256[1]:
            raise CheckFailed("Window v1 golden SHA256 mismatch")
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
        if digest(packaged_v2) != EXPECTED_WINDOW_LAYER_SHA256[2]:
            raise CheckFailed("Window v2 golden SHA256 mismatch")
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
        v0_frozen = digest(self.slot(state, 0))
        v2_frozen = digest(self.slot(state, 2))
        v1_before = digest(self.slot(state, 1))
        self.run("C", "resize", state)
        if digest(self.slot(state, 0)) != v0_frozen:
            raise CheckFailed("C resize changed v0 position bytes")
        if digest(self.slot(state, 2)) != v2_frozen:
            raise CheckFailed("C resize changed untouched v2 bytes")
        if digest(self.slot(state, 1)) == v1_before:
            raise CheckFailed("C resize did not update v1")
        self.verify("C", state, 17, 29, 940, 630, 8)
        v0_before = digest(self.slot(state, 0))
        v1_before = digest(self.slot(state, 1))
        v2_frozen = digest(self.slot(state, 2))
        self.run("C", "move-resize", state)
        if digest(self.slot(state, 0)) == v0_before:
            raise CheckFailed("C move-resize did not update v0")
        if digest(self.slot(state, 1)) == v1_before:
            raise CheckFailed("C move-resize did not update v1")
        if digest(self.slot(state, 2)) != v2_frozen:
            raise CheckFailed("C move-resize changed untouched v2 bytes")
        self.verify("C", state, 30, 46, 1040, 680, 8)
        self.verify("A", state, 30, 46)
        self.verify("B", state, 30, 46, 1040, 680)
        self.layers(state, {0, 1, 2})
        if digest(self.slot(package_b, 1)) != package_b_hash:
            raise CheckFailed("immutable packaged v1 source changed")
        if digest(self.slot(package_c, 2)) != package_c_hash:
            raise CheckFailed("immutable packaged v2 source changed")
        return "A/B/C rollback: (30,46), 1040x680, border 8; three Window slots"

    def post_load_reconcile(self, case_dir):
        state, package_b, package_b_hash = self.prepare(case_dir)
        package_c = case_dir / "packaged C initial state"
        self.run("C", "package", package_c)
        packaged_v2 = self.slot(package_c, 2)
        if (not packaged_v2.is_file() or self.slot(package_c, 0).exists() or
                self.slot(package_c, 1).exists()):
            raise CheckFailed("C package must contain v2, not v0/v1 Window state")
        package_c_hash = digest(packaged_v2)
        target_v2 = self.slot(state, 2)
        if target_v2.exists():
            raise CheckFailed("installation would overwrite existing v2")
        shutil.copyfile(packaged_v2, target_v2)
        self.layers(state, {0, 1, 2})
        self.verify("B", state, 10, 20, 640, 480)
        self.verify("C", state, 10, 20, 640, 480, 4)
        v0_frozen, v2_frozen = digest(self.slot(state, 0)), digest(self.slot(state, 2))
        v1_before = digest(self.slot(state, 1))
        self.run("B", "reconcile", state)
        if digest(self.slot(state, 0)) != v0_frozen:
            raise CheckFailed("B reconcile changed v0 position bytes")
        if digest(self.slot(state, 2)) != v2_frozen:
            raise CheckFailed("B reconcile changed v2 border bytes")
        if digest(self.slot(state, 1)) == v1_before:
            raise CheckFailed("B reconcile did not update v1")
        self.verify("C", state, 10, 20, 990, 480, 4)
        v1_frozen = digest(self.slot(state, 1))
        v2_frozen = digest(self.slot(state, 2))
        self.run("A", "move", state)
        if digest(self.slot(state, 1)) != v1_frozen:
            raise CheckFailed("A move changed v1 after reconcile")
        if digest(self.slot(state, 2)) != v2_frozen:
            raise CheckFailed("A move changed v2 after reconcile")
        self.verify("C", state, 17, 29, 990, 480, 4)
        v0_frozen = digest(self.slot(state, 0))
        v2_frozen = digest(self.slot(state, 2))
        v1_before = digest(self.slot(state, 1))
        self.run("C", "reconcile", state)
        if digest(self.slot(state, 0)) != v0_frozen:
            raise CheckFailed("C reconcile changed v0")
        if digest(self.slot(state, 2)) != v2_frozen:
            raise CheckFailed("C reconcile changed v2")
        if digest(self.slot(state, 1)) == v1_before:
            raise CheckFailed("C reconcile did not update v1")
        self.verify("C", state, 17, 29, 983, 480, 4)
        manifest_before = manifest(state)
        self.run("C", "reconcile", state)
        manifest_after = manifest(state)
        if manifest_before != manifest_after:
            raise CheckFailed("second C reconcile changed storage manifest")
        self.verify("A", state, 17, 29)
        self.verify("B", state, 17, 29, 983, 480)
        self.verify("C", state, 17, 29, 983, 480, 4)
        self.layers(state, {0, 1, 2})
        if digest(self.slot(package_b, 1)) != package_b_hash:
            raise CheckFailed("immutable packaged v1 source changed")
        if digest(self.slot(package_c, 2)) != package_c_hash:
            raise CheckFailed("immutable packaged v2 source changed")
        return "post-load reconcile: x+width=1000 via explicit B/C op, not Load"

    def backing_overlay_roundtrip(self, case_dir):
        user = case_dir / "user overlay state"
        package = case_dir / "packaged v1 only"
        self.run("A", "create", user)
        self.layers(user, {0})
        if digest(self.slot(user, 0)) != EXPECTED_WINDOW_LAYER_SHA256[0]:
            raise CheckFailed("Window v0 golden SHA256 mismatch")
        self.run("B", "package", package)
        if digest(self.slot(package, 1)) != EXPECTED_WINDOW_LAYER_SHA256[1]:
            raise CheckFailed("Window v1 golden SHA256 mismatch")
        package_v1_hash = digest(self.slot(package, 1))
        package_manifest = manifest(package)
        self.verify("B", user, 10, 20, 640, 480, backing_dir=package)
        unchanged(package_manifest, manifest(package), "package on B inspect overlay")
        if self.slot(user, 1).exists():
            raise CheckFailed("user v1 present before first overlay write")
        v0_before_resize = digest(self.slot(user, 0))
        self.run("B", "resize", user, backing_dir=package)
        self.verify("B", user, 10, 20, 740, 530, backing_dir=package)
        if digest(self.slot(user, 0)) != v0_before_resize:
            raise CheckFailed("B resize changed user v0 bytes")
        if digest(self.slot(package, 1)) != package_v1_hash:
            raise CheckFailed("packaged v1 changed after overlay resize")
        if not self.slot(user, 1).is_file():
            raise CheckFailed("user v1 missing after overlay resize")
        user_v1_hash = digest(self.slot(user, 1))
        self.run("A", "move", user)
        self.verify("A", user, 17, 29)
        if digest(self.slot(user, 1)) != user_v1_hash:
            raise CheckFailed("A move changed user v1 bytes")
        self.verify("B", user, 17, 29, 740, 530, backing_dir=package)
        if digest(self.slot(package, 1)) != package_v1_hash:
            raise CheckFailed("packaged v1 changed at end of overlay scenario")
        return "Directory overlay: v1 from backing until user resize, then user layers"

    def backing_overlay_three_version(self, case_dir):
        user = case_dir / "user three-layer overlay"
        package_b = case_dir / "package B v1"
        package_c = case_dir / "package C v2"
        backing = case_dir / "combined backing v1 v2"
        self.run("A", "create", user)
        self.layers(user, {0})
        if digest(self.slot(user, 0)) != EXPECTED_WINDOW_LAYER_SHA256[0]:
            raise CheckFailed("Window v0 golden SHA256 mismatch")
        self.run("B", "package", package_b)
        if digest(self.slot(package_b, 1)) != EXPECTED_WINDOW_LAYER_SHA256[1]:
            raise CheckFailed("Window v1 golden SHA256 mismatch")
        self.run("C", "package", package_c)
        if digest(self.slot(package_c, 2)) != EXPECTED_WINDOW_LAYER_SHA256[2]:
            raise CheckFailed("Window v2 golden SHA256 mismatch")
        package_b_manifest = manifest(package_b)
        package_c_manifest = manifest(package_c)
        package_b_v1_hash = digest(self.slot(package_b, 1))
        package_c_v2_hash = digest(self.slot(package_c, 2))
        backing.mkdir()
        backing_v1 = self.slot(backing, 1)
        backing_v1.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(self.slot(package_b, 1), backing_v1)
        backing_v2 = self.slot(backing, 2)
        backing_v2.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(self.slot(package_c, 2), backing_v2)
        backing_manifest = manifest(backing)
        backing_v2_hash = digest(backing_v2)
        user_manifest = manifest(user)
        self.verify("C", user, 10, 20, 640, 480, 4, backing_dir=backing)
        self.layers(user, {0})
        unchanged(user_manifest, manifest(user), "user after C inspect overlay")
        unchanged(backing_manifest, manifest(backing), "backing after C inspect")
        v0_frozen = digest(self.slot(user, 0))
        self.run("B", "resize", user, backing_dir=backing)
        self.verify("C", user, 10, 20, 740, 530, 4, backing_dir=backing)
        if digest(self.slot(user, 0)) != v0_frozen:
            raise CheckFailed("B resize changed user v0 bytes")
        if not self.slot(user, 1).is_file():
            raise CheckFailed("user v1 missing after overlay resize")
        if self.slot(user, 2).exists():
            raise CheckFailed("user v2 present before C change-border")
        unchanged(backing_manifest, manifest(backing), "backing after B resize")
        user_v1_frozen = digest(self.slot(user, 1))
        self.run("A", "move", user)
        self.verify("A", user, 17, 29)
        if digest(self.slot(user, 1)) != user_v1_frozen:
            raise CheckFailed("A move changed user v1 bytes")
        if self.slot(user, 2).exists():
            raise CheckFailed("user v2 present before change-border")
        self.verify("C", user, 17, 29, 740, 530, 4, backing_dir=backing)
        v0_frozen = digest(self.slot(user, 0))
        v1_frozen = digest(self.slot(user, 1))
        self.run("C", "change-border", user, backing_dir=backing)
        self.verify("C", user, 17, 29, 740, 530, 6, backing_dir=backing)
        if digest(self.slot(user, 0)) != v0_frozen:
            raise CheckFailed("change-border changed user v0")
        if digest(self.slot(user, 1)) != v1_frozen:
            raise CheckFailed("change-border changed user v1")
        if digest(backing_v2) != backing_v2_hash:
            raise CheckFailed("backing v2 changed during change-border")
        if not self.slot(user, 2).is_file():
            raise CheckFailed("user v2 missing after change-border")
        user_v0_frozen = digest(self.slot(user, 0))
        user_v2_frozen = digest(self.slot(user, 2))
        self.run("B", "resize", user)
        self.verify("C", user, 17, 29, 840, 580, 6)
        if digest(self.slot(user, 0)) != user_v0_frozen:
            raise CheckFailed("B resize changed user v0")
        if digest(self.slot(user, 2)) != user_v2_frozen:
            raise CheckFailed("B resize changed user v2")
        self.verify("A", user, 17, 29)
        self.verify("B", user, 17, 29, 840, 580)
        self.verify("C", user, 17, 29, 840, 580, 6)
        self.verify("C", user, 17, 29, 840, 580, 6, backing_dir=backing)
        self.layers(user, {0, 1, 2})
        unchanged(package_b_manifest, manifest(package_b), "package B source")
        unchanged(package_c_manifest, manifest(package_c), "package C source")
        unchanged(backing_manifest, manifest(backing), "combined backing")
        if digest(self.slot(package_b, 1)) != package_b_v1_hash:
            raise CheckFailed("package B v1 bytes changed")
        if digest(self.slot(package_c, 2)) != package_c_v2_hash:
            raise CheckFailed("package C v2 bytes changed")
        return "overlay backing v1+v2: user layers grow without mutating packages"

    def backing_overlay_untouched_layer(self, case_dir):
        user = case_dir / "user untouched overlay"
        package_b = case_dir / "package B v1"
        package_c = case_dir / "package C v2"
        backing = case_dir / "combined backing v1 v2"
        self.run("A", "create", user)
        self.layers(user, {0})
        if digest(self.slot(user, 0)) != EXPECTED_WINDOW_LAYER_SHA256[0]:
            raise CheckFailed("Window v0 golden SHA256 mismatch")
        self.run("B", "package", package_b)
        if digest(self.slot(package_b, 1)) != EXPECTED_WINDOW_LAYER_SHA256[1]:
            raise CheckFailed("Window v1 golden SHA256 mismatch")
        self.run("C", "package", package_c)
        if digest(self.slot(package_c, 2)) != EXPECTED_WINDOW_LAYER_SHA256[2]:
            raise CheckFailed("Window v2 golden SHA256 mismatch")
        package_b_manifest = manifest(package_b)
        package_c_manifest = manifest(package_c)
        package_b_v1_hash = digest(self.slot(package_b, 1))
        package_c_v2_hash = digest(self.slot(package_c, 2))
        backing.mkdir()
        backing_v1 = self.slot(backing, 1)
        backing_v1.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(self.slot(package_b, 1), backing_v1)
        backing_v2 = self.slot(backing, 2)
        backing_v2.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(self.slot(package_c, 2), backing_v2)
        backing_manifest = manifest(backing)
        backing_v2_hash = digest(backing_v2)
        user_manifest = manifest(user)
        self.verify("C", user, 10, 20, 640, 480, 4, backing_dir=backing)
        self.layers(user, {0})
        unchanged(user_manifest, manifest(user), "user after C inspect overlay")
        unchanged(backing_manifest, manifest(backing), "backing after C inspect")
        v0_frozen = digest(self.slot(user, 0))
        self.run("C", "change-border", user, backing_dir=backing)
        self.verify("C", user, 10, 20, 640, 480, 6, backing_dir=backing)
        if digest(self.slot(user, 0)) != v0_frozen:
            raise CheckFailed("change-border changed user v0")
        if self.slot(user, 1).exists():
            raise CheckFailed("user v1 materialized without width/height mutation")
        if not self.slot(user, 2).is_file():
            raise CheckFailed("user v2 missing after change-border")
        if digest(backing_v2) != backing_v2_hash:
            raise CheckFailed("backing v2 changed during change-border")
        self.verify("C", user, 10, 20, 640, 480, 6, backing_dir=backing)
        self.layers(user, {0, 2})
        unchanged(package_b_manifest, manifest(package_b), "package B source")
        unchanged(package_c_manifest, manifest(package_c), "package C source")
        unchanged(backing_manifest, manifest(backing), "combined backing")
        if digest(self.slot(package_b, 1)) != package_b_v1_hash:
            raise CheckFailed("package B v1 bytes changed")
        if digest(self.slot(package_c, 2)) != package_c_v2_hash:
            raise CheckFailed("package C v2 bytes changed")
        v1_supply = case_dir / "supply v1 resize scratch"
        self.run("A", "create", v1_supply)
        shutil.copyfile(self.slot(package_b, 1), self.slot(v1_supply, 1))
        self.run("B", "resize", v1_supply)
        self.verify("B", v1_supply, 10, 20, 740, 530)
        supply_v1_hash = digest(self.slot(v1_supply, 1))
        if supply_v1_hash == EXPECTED_WINDOW_LAYER_SHA256[1]:
            raise CheckFailed("supply v1 SHA256 unchanged from golden v1")
        if supply_v1_hash == digest(backing_v1):
            raise CheckFailed("supply v1 matches old backing v1")
        backing_v2_dir = case_dir / "combined backing v1 v2 refresh"
        shutil.copytree(backing, backing_v2_dir)
        shutil.copyfile(self.slot(v1_supply, 1), self.slot(backing_v2_dir, 1))
        if digest(self.slot(backing_v2_dir, 1)) == digest(backing_v1):
            raise CheckFailed("BACKING_V2 v1 unchanged from original backing v1")
        if digest(self.slot(backing_v2_dir, 2)) != backing_v2_hash:
            raise CheckFailed("BACKING_V2 v2 differs from original backing v2")
        backing_v2_layers = {
            p.name for p in self.slot(backing_v2_dir, 1).parent.iterdir() if p.is_file()}
        if backing_v2_layers != {"1", "2"}:
            raise CheckFailed(f"BACKING_V2 version slots: expected {{1, 2}}, got {backing_v2_layers}")
        backing_v2_manifest = manifest(backing_v2_dir)
        user_before_supply_inspect = manifest(user)
        self.verify("C", user, 10, 20, 740, 530, 6, backing_dir=backing_v2_dir)
        unchanged(user_before_supply_inspect, manifest(user), "user after supply inspect")
        unchanged(backing_v2_manifest, manifest(backing_v2_dir), "BACKING_V2 after supply inspect")
        if self.slot(user, 1).exists():
            raise CheckFailed("user v1 appeared after updated backing inspect")
        user_v2_before_supply_border = digest(self.slot(user, 2))
        self.run("C", "change-border", user, backing_dir=backing_v2_dir)
        self.verify("C", user, 10, 20, 740, 530, 8, backing_dir=backing_v2_dir)
        if digest(self.slot(user, 0)) != v0_frozen:
            raise CheckFailed("supply change-border changed user v0")
        if self.slot(user, 1).exists():
            raise CheckFailed("user v1 materialized after supply change-border")
        if digest(self.slot(user, 2)) == user_v2_before_supply_border:
            raise CheckFailed("supply change-border did not update user v2")
        unchanged(backing_v2_manifest, manifest(backing_v2_dir), "BACKING_V2 after supply change-border")
        self.verify("C", user, 10, 20, 740, 530, 8, backing_dir=backing_v2_dir)
        self.layers(user, {0, 2})
        unchanged(package_b_manifest, manifest(package_b), "package B source after supply")
        unchanged(package_c_manifest, manifest(package_c), "package C source after supply")
        unchanged(backing_manifest, manifest(backing), "original combined backing after supply")
        if digest(self.slot(package_b, 1)) != package_b_v1_hash:
            raise CheckFailed("package B v1 bytes changed after supply")
        if digest(self.slot(package_c, 2)) != package_c_v2_hash:
            raise CheckFailed("package C v2 bytes changed after supply")
        return ("untouched v1 overlay: user v2 only; refreshed backing v1 740x530 visible to C")

    def embedded_backing_roundtrip(self, case_dir):
        user = case_dir / "user embedded overlay"
        self.run("A", "create", user)
        self.verify("A", user, 10, 20)
        self.layers(user, {0})
        if digest(self.slot(user, 0)) != EXPECTED_WINDOW_LAYER_SHA256[0]:
            raise CheckFailed("Window v0 golden SHA256 mismatch")
        user_manifest = manifest(user)
        self.verify("C", user, 10, 20, 640, 480, 4, embedded_backing=True)
        self.layers(user, {0})
        unchanged(user_manifest, manifest(user), "user after embedded inspect")
        v0_frozen = digest(self.slot(user, 0))
        self.run("C", "change-border", user, embedded_backing=True)
        self.verify("C", user, 10, 20, 640, 480, 6, embedded_backing=True)
        if digest(self.slot(user, 0)) != v0_frozen:
            raise CheckFailed("embedded change-border changed user v0")
        if self.slot(user, 1).exists():
            raise CheckFailed("user v1 present before embedded resize")
        self.layers(user, {0, 2})
        user_v2_frozen = digest(self.slot(user, 2))
        self.run("C", "resize", user, embedded_backing=True)
        self.verify("C", user, 10, 20, 740, 530, 6, embedded_backing=True)
        self.layers(user, {0, 1, 2})
        if digest(self.slot(user, 0)) != v0_frozen:
            raise CheckFailed("embedded resize changed user v0")
        if digest(self.slot(user, 2)) != user_v2_frozen:
            raise CheckFailed("embedded resize changed user v2")
        user_v1_frozen = digest(self.slot(user, 1))
        self.run("A", "move", user)
        self.verify("A", user, 17, 29)
        if digest(self.slot(user, 1)) != user_v1_frozen:
            raise CheckFailed("A move changed user v1")
        if digest(self.slot(user, 2)) != user_v2_frozen:
            raise CheckFailed("A move changed user v2")
        self.verify("A", user, 17, 29)
        self.verify("B", user, 17, 29, 740, 530)
        self.verify("C", user, 17, 29, 740, 530, 6, embedded_backing=True)
        for path in case_dir.iterdir():
            lowered = path.name.lower()
            if "package" in lowered or "backing" in lowered:
                raise CheckFailed(f"unexpected external supply directory: {path}")
        return "EXE C compiles embedded v1/v2; each process reloads RAM backing"

    def embedded_first_install(self, case_dir):
        user = case_dir / "empty user first install"
        user.mkdir()
        user_manifest = manifest(user)
        self.verify("C", user, 10, 20, 640, 480, 4, embedded_backing=True)
        unchanged(user_manifest, manifest(user), "user after first embedded inspect")
        if self.slot(user, 0).parent.is_dir():
            self.layers(user, set())
        self.run("C", "change-border", user, embedded_backing=True)
        self.verify("C", user, 10, 20, 640, 480, 6, embedded_backing=True)
        self.layers(user, {2})
        if self.slot(user, 0).exists() or self.slot(user, 1).exists():
            raise CheckFailed("user v0/v1 present after first change-border")
        user_v2_frozen = digest(self.slot(user, 2))
        self.run("C", "move", user, embedded_backing=True)
        self.verify("C", user, 23, 37, 640, 480, 6, embedded_backing=True)
        self.layers(user, {0, 2})
        if self.slot(user, 1).exists():
            raise CheckFailed("user v1 present after embedded move")
        if digest(self.slot(user, 2)) != user_v2_frozen:
            raise CheckFailed("embedded move changed user v2")
        user_v0_frozen = digest(self.slot(user, 0))
        self.run("C", "resize", user, embedded_backing=True)
        self.verify("C", user, 23, 37, 740, 530, 6, embedded_backing=True)
        self.layers(user, {0, 1, 2})
        if digest(self.slot(user, 0)) != user_v0_frozen:
            raise CheckFailed("embedded resize changed user v0")
        if digest(self.slot(user, 2)) != user_v2_frozen:
            raise CheckFailed("embedded resize changed user v2")
        user_v1_frozen = digest(self.slot(user, 1))
        self.run("A", "move", user)
        self.verify("A", user, 30, 46)
        if digest(self.slot(user, 1)) != user_v1_frozen:
            raise CheckFailed("A move changed user v1")
        if digest(self.slot(user, 2)) != user_v2_frozen:
            raise CheckFailed("A move changed user v2")
        self.verify("A", user, 30, 46)
        self.verify("B", user, 30, 46, 740, 530)
        self.verify("C", user, 30, 46, 740, 530, 6, embedded_backing=True)
        for path in case_dir.iterdir():
            lowered = path.name.lower()
            if "package" in lowered or "backing" in lowered:
                raise CheckFailed(f"unexpected external supply directory: {path}")
        return "empty USER: embedded v0/v1/v2 until user writes v2 then v0 then v1"

    def embedded_rollback_to_a(self, case_dir):
        user = case_dir / "user rollback to A"
        user.mkdir()
        user_manifest = manifest(user)
        self.verify("C", user, 10, 20, 640, 480, 4, embedded_backing=True)
        unchanged(user_manifest, manifest(user), "user after C embedded inspect")
        if self.slot(user, 0).parent.is_dir():
            self.layers(user, set())
        self.run("C", "change-border", user, embedded_backing=True)
        self.verify("C", user, 10, 20, 640, 480, 6, embedded_backing=True)
        self.layers(user, {2})
        if self.slot(user, 0).exists() or self.slot(user, 1).exists():
            raise CheckFailed("user v0/v1 present before A rollback")
        user_v2_frozen = digest(self.slot(user, 2))
        manifest_before_a_inspect = manifest(user)
        self.verify("A", user, 10, 20, embedded_backing=True)
        unchanged(manifest_before_a_inspect, manifest(user), "user after A embedded inspect")
        if self.slot(user, 0).exists():
            raise CheckFailed("user v0 appeared after A embedded inspect")
        if digest(self.slot(user, 2)) != user_v2_frozen:
            raise CheckFailed("A embedded inspect changed user v2")
        self.run("A", "move", user, embedded_backing=True)
        self.verify("A", user, 17, 29, embedded_backing=True)
        self.layers(user, {0, 2})
        if self.slot(user, 1).exists():
            raise CheckFailed("user v1 present after A embedded move")
        if digest(self.slot(user, 2)) != user_v2_frozen:
            raise CheckFailed("A embedded move changed user v2")
        manifest_before_c_inspect = manifest(user)
        self.verify("C", user, 17, 29, 640, 480, 6, embedded_backing=True)
        unchanged(manifest_before_c_inspect, manifest(user), "user after C embedded inspect")
        manifest_before_a_final = manifest(user)
        self.verify("A", user, 17, 29, embedded_backing=True)
        unchanged(manifest_before_a_final, manifest(user), "user after final A embedded inspect")
        return "C user v2 only; A embedded v0 rollback then C sees merged state"

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
    suite.case("post-load-reconcile", suite.post_load_reconcile)
    suite.case("backing-overlay-roundtrip", suite.backing_overlay_roundtrip)
    suite.case("backing-overlay-three-version", suite.backing_overlay_three_version)
    suite.case("backing-overlay-untouched-layer", suite.backing_overlay_untouched_layer)
    suite.case("embedded-backing-roundtrip", suite.embedded_backing_roundtrip)
    suite.case("embedded-first-install", suite.embedded_first_install)
    suite.case("embedded-rollback-to-a", suite.embedded_rollback_to_a)
    return suite.finish()


if __name__ == "__main__":
    raise SystemExit(main())
