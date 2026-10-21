#!/usr/bin/env python3
"""Cross-process fixture matrix; all storage is created in a new run directory.

The guarded matrix exercises a policy implemented ONLY by the test executables.
--native bypasses that policy and diagnoses the native loader's stale-slot
rollback behavior. A data mismatch is FAIL/nonzero, including in diagnostic mode.
Journals and production models remain covered by the existing persistence tests.
"""

import argparse
from dataclasses import dataclass
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile


REVISIONS = "ABC"
STEPS = {"A": 1, "B": 10, "C": 100}
DEFAULT_CHAINS = [("A", "C", "B", "A"), ("A", "B", "C", "B", "A", "C")]
NATIVE_CHAINS = [("A", "B", "A"), ("B", "C", "B")]


class CheckFailed(RuntimeError):
    pass


def schema(mode, revision):
    return (int(revision != "A"), int(revision == "C")) if mode == "evolving" else (0, 0)


@dataclass(frozen=True)
class State:
    value: int
    label: str
    base_schema: int
    derived_schema: int
    added_base: int
    added_derived: int

    @classmethod
    def initial(cls, mode, revision):
        base, derived = schema(mode, revision)
        return cls(11, "initial", base, derived, 1011 if base else 0,
                   2011 if derived else 0)

    def supports(self, mode, revision):
        base, derived = schema(mode, revision)
        return base >= self.base_schema and derived >= self.derived_schema

    def updated(self, mode, revision):
        base, derived = schema(mode, revision)
        value = self.value + STEPS[revision]
        # An old writer must preserve fields it does not understand. Keeping
        # those fields in the oracle also makes native downgrade loss visible.
        return State(value, "updated-" + revision,
                     max(base, self.base_schema), max(derived, self.derived_schema),
                     1000 + value if base else self.added_base,
                     2000 + value if derived else self.added_derived)

    def expected(self, mode, revision):
        base, derived = schema(mode, revision)
        return {
            "exe": revision, "mode": mode,
            "base_schema": base, "derived_schema": derived,
            "value": self.value, "label": self.label,
            "added_base": (self.added_base if self.base_schema else 999) if base else 0,
            "added_derived": (self.added_derived if self.derived_schema else 777) if derived else 0,
            "child_value": 37, "root_id": 91001, "child_id": 91002,
            "alias_child_id": 91002, "child_backref_id": 91001,
            "aliases_same_object": True, "backref_is_root": True,
        }


def digest(path):
    result = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            result.update(block)
    return result.hexdigest()


def manifest(directory):
    result = {}
    if not directory.exists():
        return result
    for path in sorted(directory.rglob("*")):
        relative = path.relative_to(directory).as_posix()
        if path.is_symlink():
            raise CheckFailed("unexpected symlink in storage: " + relative)
        if path.is_dir():
            result[relative] = {"kind": "directory"}
        elif path.is_file():
            result[relative] = {"kind": "file", "size": path.stat().st_size,
                                "sha256": digest(path)}
        else:
            raise CheckFailed("unexpected storage entry: " + relative)
    return result


def unchanged(before, after, context):
    if before != after:
        changed = sorted(key for key in before.keys() | after.keys()
                         if before.get(key) != after.get(key))
        raise CheckFailed(context + ": storage changed: " + ", ".join(changed))


class Matrix:
    def __init__(self, args):
        self.args = args
        self.executables = {
            mode: dict(zip(REVISIONS, map(lambda p: Path(p).resolve(strict=True), paths)))
            for mode, paths in (("stable", args.stable), ("evolving", args.evolving))
        }
        self.executable_hashes = {str(path): digest(path)
                                  for binaries in self.executables.values()
                                  for path in binaries.values()}
        self.root = Path(tempfile.mkdtemp(prefix="apptraverse-evolution-", dir=args.work_root))
        self.class_ids = None
        self.process_count = 0
        self.results = []
        print("Run directory: " + str(self.root), flush=True)

    def invoke(self, mode, revision, operation, directory, native=False):
        self.process_count += 1
        command = [str(self.executables[mode][revision]), "--state-dir", str(directory),
                   "--op", operation]
        if native:
            command.append("--native")
        log = directory.parent / f"process-{self.process_count:03d}-{revision}-{operation}.json"
        before = manifest(directory)
        try:
            completed = subprocess.run(command, capture_output=True, text=True,
                                       encoding="utf-8", errors="replace",
                                       timeout=self.args.timeout, cwd=self.root)
        except subprocess.TimeoutExpired as error:
            log.write_text(json.dumps({"command": command, "timeout": self.args.timeout,
                                       "stdout": str(error.stdout), "stderr": str(error.stderr),
                                       "before": before, "after": manifest(directory)}, indent=2),
                           encoding="utf-8")
            raise CheckFailed(f"{revision} {operation}: process timed out; {log}") from error
        after = manifest(directory)
        log.write_text(json.dumps({"command": command, "exit_code": completed.returncode,
                                   "stdout": completed.stdout, "stderr": completed.stderr,
                                   "before": before, "after": after}, indent=2), encoding="utf-8")
        if operation == "inspect" or completed.returncode != 0:
            unchanged(before, after, f"{mode} {revision} {operation}")
        return completed, before, after

    def verify(self, completed, state, mode, revision):
        if completed.returncode != 0:
            reason = completed.stderr.strip() or completed.stdout.strip() or "no diagnostic"
            raise CheckFailed(f"{mode} {revision}: exit {completed.returncode}: {reason}")
        records = []
        for line in completed.stdout.splitlines():
            try:
                record = json.loads(line)
            except ValueError:
                continue
            if isinstance(record, dict) and "exe" in record and "root_id" in record:
                records.append(record)
        if len(records) != 1:
            raise CheckFailed(f"{mode} {revision}: expected one fixture JSON record, got {len(records)}")
        actual = records[0]
        differences = [f"{key}: expected {value!r}, got {actual.get(key)!r}"
                       for key, value in state.expected(mode, revision).items()
                       if actual.get(key) != value or type(actual.get(key)) is not type(value)]
        if differences:
            raise CheckFailed(f"{mode} {revision}: " + "; ".join(differences))
        class_ids = {key: actual.get(key) for key in
                     ("root_class_id", "base_class_id", "derived_class_id")}
        if any(type(value) is not int or value <= 0 for value in class_ids.values()):
            raise CheckFailed("missing or invalid fixture class IDs")
        if self.class_ids is None:
            self.class_ids = class_ids
        elif self.class_ids != class_ids:
            raise CheckFailed(f"ClassId changed across executables: {class_ids}")
        return actual

    def success(self, mode, revision, operation, directory, state, native=False):
        completed, before, after = self.invoke(mode, revision, operation, directory, native)
        actual = self.verify(completed, state, mode, revision)
        if operation != "inspect" and before == after:
            raise CheckFailed(f"{mode} {revision} {operation}: no state was written")
        return actual

    def rejected(self, mode, revision, operation, directory):
        completed, _, _ = self.invoke(mode, revision, operation, directory)
        reason = completed.stderr.strip()
        if completed.returncode != 2 or "INCOMPATIBLE" not in reason or "class=" not in reason:
            raise CheckFailed(f"expected explicit incompatible exit 2; got {completed.returncode}: {reason}")
        return reason

    def create(self, mode, revision, directory, native=False):
        state = State.initial(mode, revision)
        self.success(mode, revision, "create", directory, state, native)
        self.success(mode, revision, "inspect", directory, state, native)
        return state

    def scenario(self, name, run):
        case_dir = self.root / f"{len(self.results) + 1:02d}-{name}"
        case_dir.mkdir()
        try:
            detail = run(case_dir / "state") or ""
            outcome = {"name": name, "result": "PASS", "detail": detail}
        except (CheckFailed, OSError, ValueError) as error:
            outcome = {"name": name, "result": "FAIL", "detail": str(error)}
        self.results.append(outcome)
        print(f"{outcome['result']} {name}: {outcome['detail']}", flush=True)

    def pair(self, mode, writer, reader, directory):
        state = self.create(mode, writer, directory)
        state = state.updated(mode, writer)
        self.success(mode, writer, "update", directory, state)
        self.success(mode, writer, "inspect", directory, state)
        if state.supports(mode, reader):
            self.success(mode, reader, "inspect", directory, state)
            return "reader restored values/IDs/aliases/cycle; files unchanged"
        reason = self.rejected(mode, reader, "inspect", directory)
        self.rejected(mode, reader, "update", directory)
        self.success(mode, writer, "inspect", directory, state)
        return reason + "; files unchanged; writer recovered last state"

    def data_only(self, directory):
        state = self.create("stable", "A", directory)
        for _ in range(2):
            state = state.updated("stable", "A")
            self.success("stable", "A", "update", directory, state)
            self.success("stable", "A", "inspect", directory, state)
        return "same EXE A: values 11 -> 12 -> 13; schema remains 0/0"

    def chain(self, mode, sequence, directory, native=False):
        state = self.create(mode, sequence[0], directory, native)
        last_writer = sequence[0]
        transitions = []
        for revision in sequence[1:]:
            if not native and not state.supports(mode, revision):
                reason = self.rejected(mode, revision, "inspect", directory)
                self.rejected(mode, revision, "update", directory)
                self.success(mode, last_writer, "inspect", directory, state)
                transitions.append(reason + "; unchanged, recovered with " + last_writer)
                continue
            # Inspect before update: update assigns new-field sentinels, so
            # checking only its output would hide broken migration defaults.
            self.success(mode, revision, "inspect", directory, state, native)
            state = state.updated(mode, revision)
            self.success(mode, revision, "update", directory, state, native)
            self.success(mode, revision, "inspect", directory, state, native)
            recovery = revision if state.supports(mode, revision) else "C"
            self.success(mode, recovery, "inspect", directory, state, native)
            last_writer = recovery
            transitions.append(f"{revision}: value={state.value}, schema={state.base_schema}/{state.derived_schema}")
        self.success(mode, last_writer, "inspect", directory, state, native)
        return "; ".join(transitions) or "initial state verified"

    def future_schema(self, directory):
        state = self.create("evolving", "C", directory)
        before = manifest(directory)
        layer_dir = directory / "91002" / str(self.class_ids["derived_class_id"])
        injected = layer_dir / "2"
        if injected.exists():
            raise CheckFailed("fixture unexpectedly already has schema 2")
        injected.write_bytes((layer_dir / "1").read_bytes())
        reason = self.rejected("evolving", "C", "inspect", directory)
        self.rejected("evolving", "C", "update", directory)
        # Remove only the file this scenario just injected, after proving
        # rejection left it and every original file unchanged.
        injected.unlink()
        unchanged(before, manifest(directory), "remove test-injected future slot")
        self.success("evolving", "C", "inspect", directory, state)
        return reason + "; unchanged; removed test injection and verified original state"

    def finish(self):
        for path, expected_hash in self.executable_hashes.items():
            if digest(Path(path)) != expected_hash:
                self.results.append({"name": "executable-identity", "result": "FAIL",
                                     "detail": "EXE changed during matrix: " + path})
                print("FAIL executable-identity: " + path, flush=True)
        report = {"native": self.args.native, "processes": self.process_count,
                  "executable_sha256": self.executable_hashes, "results": self.results}
        (self.root / "results.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
        failed = sum(item["result"] == "FAIL" for item in self.results)
        print(f"{'FAIL' if failed else 'PASS'}: {len(self.results) - failed} passed, "
              f"{failed} failed, {self.process_count} processes; {self.root / 'results.json'}", flush=True)
        return 1 if failed else 0


def parse_sequence(value):
    sequence = tuple(part.strip().upper() for part in value.split(","))
    if not sequence or any(part not in REVISIONS or len(part) != 1 for part in sequence):
        raise argparse.ArgumentTypeError("sequence must contain A, B or C, e.g. A,C,B,A")
    return sequence


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--stable", nargs=3, required=True, metavar=("A", "B", "C"))
    parser.add_argument("--evolving", nargs=3, required=True, metavar=("A", "B", "C"))
    parser.add_argument("--work-root", type=Path, help="parent for a new run directory; existing profiles are never reused")
    parser.add_argument("--sequence", type=parse_sequence, action="append",
                        help="run this chain instead of the default chains; repeatable")
    parser.add_argument("--native", action="store_true",
                        help="run native rollback diagnostics instead of the guarded matrix; data loss is FAIL/nonzero")
    parser.add_argument("--timeout", type=float, default=30, help="seconds per fixture process (default: 30)")
    args = parser.parse_args()
    if args.timeout <= 0:
        parser.error("--timeout must be positive")
    matrix = Matrix(args)
    if args.native:
        print("Native diagnostic: test admission gate bypassed; stale data is a failure.", flush=True)
        for sequence in args.sequence or NATIVE_CHAINS:
            matrix.scenario("native-" + "-".join(sequence),
                            lambda directory, seq=sequence: matrix.chain("evolving", seq, directory, native=True))
    else:
        print("Guarded matrix: admission policy belongs only to the test fixtures.", flush=True)
        matrix.scenario("stable-data-only", matrix.data_only)
        for mode in ("stable", "evolving"):
            for writer in REVISIONS:
                for reader in REVISIONS:
                    matrix.scenario(f"{mode}-pair-{writer}-{reader}",
                                    lambda directory, m=mode, w=writer, r=reader: matrix.pair(m, w, r, directory))
            for sequence in args.sequence or DEFAULT_CHAINS:
                matrix.scenario(mode + "-chain-" + "-".join(sequence),
                                lambda directory, m=mode, seq=sequence: matrix.chain(m, seq, directory))
        matrix.scenario("future-schema-rejection", matrix.future_schema)
    return matrix.finish()


if __name__ == "__main__":
    raise SystemExit(main())
