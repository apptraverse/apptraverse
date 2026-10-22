"""Process invocation and runner self-checks for event_version_matrix."""

from __future__ import annotations

import json
import subprocess
from dataclasses import dataclass
from pathlib import Path
from typing import Literal

from application_evolution_matrix import CheckFailed, manifest, unchanged
ExeRevision = Literal["A", "B"]

READ_ONLY_OPS = frozenset(
    {
        "probe-v1-state",
        "inspect",
        "reload",
        "replay-from-base",
        "replay-from-base-fault",
    }
)

TIMEOUT_SEC = 60


@dataclass(frozen=True)
class CompletedInvocation:
    returncode: int
    record: dict | None
    stdout: str
    stderr: str


def extract_ok_record(stdout: str) -> dict | None:
    record = None
    for line in stdout.splitlines():
        try:
            candidate = json.loads(line)
        except ValueError:
            continue
        if isinstance(candidate, dict) and candidate.get("ok"):
            record = candidate
    return record


def validate_invocation(
    completed: CompletedInvocation,
    *,
    context: str,
    expect_exit: int,
    expect_stage: str | None,
    expect_exe: ExeRevision | None,
) -> dict | None:
    if completed.record and completed.record.get("ok") and completed.returncode != 0:
        raise CheckFailed(
            f"{context}: ok JSON with nonzero exit={completed.returncode}"
        )
    if completed.returncode != expect_exit:
        raise CheckFailed(
            f"{context}: exit={completed.returncode}, expected={expect_exit}; "
            f"stderr={completed.stderr!r}"
        )
    if expect_exit != 0:
        return completed.record
    if completed.record is None:
        raise CheckFailed(f"{context}: missing ok JSON; stdout={completed.stdout!r}")
    stage = completed.record.get("stage")
    if expect_stage is not None and stage != expect_stage:
        raise CheckFailed(
            f"{context}: stage={stage!r}, expected={expect_stage!r}"
        )
    exe = completed.record.get("exe")
    if expect_exe is not None and exe != expect_exe:
        raise CheckFailed(f"{context}: exe={exe!r}, expected={expect_exe!r}")
    return completed.record


def invoke_fixture(
    exe: Path,
    exe_rev: ExeRevision,
    state_dir: Path,
    op: str,
    *,
    expect_exit: int = 0,
    expect_stage: str | None = None,
    delta: int | None = None,
    extension_delta: int | None = None,
    check_storage_unchanged: bool | None = None,
) -> dict | None:
    if check_storage_unchanged is None:
        check_storage_unchanged = op in READ_ONLY_OPS
    stage = expect_stage if expect_stage is not None else op
    command = [str(exe), "--state-dir", str(state_dir), "--op", op]
    if delta is not None:
        command.extend(["--delta", str(delta)])
    if extension_delta is not None:
        command.extend(["--extension-delta", str(extension_delta)])

    before = manifest(state_dir) if check_storage_unchanged else None
    try:
        proc = subprocess.run(
            command,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=TIMEOUT_SEC,
        )
    except subprocess.TimeoutExpired as error:
        raise CheckFailed(f"{exe_rev} {op}: timeout after {TIMEOUT_SEC}s") from error

    completed = CompletedInvocation(
        returncode=proc.returncode,
        record=extract_ok_record(proc.stdout),
        stdout=proc.stdout,
        stderr=proc.stderr,
    )
    record = validate_invocation(
        completed,
        context=f"{exe_rev} {op}",
        expect_exit=expect_exit,
        expect_stage=stage,
        expect_exe=exe_rev if expect_exit == 0 else None,
    )
    if check_storage_unchanged:
        after = manifest(state_dir)
        unchanged(before, after, f"{exe_rev} {op} read-only storage")
    return record


def v1_layer_snapshot(
    storage_manifest: dict,
    *,
    doc_id: int,
    base_id: int,
    doc_class_id: int,
    event_class_id: int,
) -> dict[str, str]:
    keys = {
        f"{doc_id}/{doc_class_id}/1",
        f"{base_id}/{doc_class_id}/1",
    }
    suffix = f"/{event_class_id}/1"
    for key in storage_manifest:
        if key.endswith(suffix):
            keys.add(key)
    return {key: storage_manifest[key] for key in keys if key in storage_manifest}


def assert_v1_layers_unchanged(before: dict, after: dict, *, context: str) -> None:
    if before != after:
        changed = sorted(set(before) ^ set(after))
        mismatched = sorted(
            key for key in before.keys() & after.keys() if before[key] != after[key]
        )
        raise CheckFailed(
            f"{context}: v1 layer bytes changed; "
            f"added/removed={changed[:6]} mismatched={mismatched[:6]}"
        )


def assert_baseline_doc_only_v1_replay(
    record: dict,
    *,
    exit_code: int,
    before_manifest: dict,
    after_manifest: dict,
) -> None:
    if exit_code != 0:
        raise CheckFailed(f"baseline replay exit={exit_code}, expected 0")
    unchanged(before_manifest, after_manifest, "baseline replay read-only storage")
    if record.get("stage") != "replay-from-base":
        raise CheckFailed("baseline replay unexpected stage")
    if record.get("exe") != "B":
        raise CheckFailed("baseline replay unexpected exe")
    if record.get("replay_ok") is not True:
        raise CheckFailed(f"baseline replay_ok not true: {record}")
    if int(record.get("tally", -1)) != 17:
        raise CheckFailed(f"baseline tally expected 17, got {record.get('tally')}")
    if int(record.get("extension", -1)) != 5:
        raise CheckFailed(f"baseline extension expected 5, got {record.get('extension')}")
    if int(record.get("journal_len", -1)) != 5:
        raise CheckFailed(
            f"baseline journal_len expected 5, got {record.get('journal_len')}"
        )


def run_runner_selftests() -> None:
    """Negative checks on runner logic (does not replace C++ EXE runs)."""

    def expect_fail(callable_obj, needle: str) -> None:
        try:
            callable_obj()
        except CheckFailed as error:
            if needle not in str(error):
                raise CheckFailed(
                    f"selftest expected {needle!r} in error, got: {error}"
                ) from error
            return
        raise CheckFailed(f"selftest expected CheckFailed containing {needle!r}")

    # ok JSON + nonzero exit
    expect_fail(
        lambda: validate_invocation(
            CompletedInvocation(
                1,
                {"ok": True, "stage": "inspect", "exe": "A"},
                "",
                "",
            ),
            context="selftest",
            expect_exit=0,
            expect_stage="inspect",
            expect_exe="A",
        ),
        "ok JSON with nonzero exit",
    )

    # storage change on read-only
    expect_fail(
        lambda: unchanged({"a/1": "x"}, {"a/1": "y"}, "selftest"),
        "storage changed",
    )

    # wrong replay tally
    from event_version_storage_checks import assert_replay

    expect_fail(
        lambda: assert_replay(
            {
                "ok": True,
                "stage": "replay-from-base",
                "replay_ok": True,
                "tally": 0,
                "journal_len": 5,
                "extension": 645,
                "tally_after_first_replay": 0,
                "tally_after_second_replay": 0,
                "extension_after_first_replay": 645,
                "extension_after_second_replay": 645,
            },
            who="selftest",
            tally=17,
            journal_len=5,
            extension=645,
        ),
        "tally expected 17",
    )

    print(json.dumps({"runner_selftests": "PASS"}), flush=True)
