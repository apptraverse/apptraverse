#!/usr/bin/env python3
"""Same ClassId Event v0/v1: materialized save/load plus TryReplayFromBase."""

import argparse
import json
import shutil
import subprocess
import tempfile
from pathlib import Path

from application_evolution_matrix import CheckFailed, digest, manifest, unchanged
from event_version_storage_checks import (
    assert_base_v1_probe,
    assert_class_ids_match,
    assert_extension,
    assert_journal_len,
    assert_no_extension_field,
    assert_replay,
    assert_tally,
    doc_version_slots,
    event_versions,
    last_event,
    parse_fixture_record,
)

BASE_ID = 94002
PACKAGED_EXTENSION = 640


def run(exe: Path, state_dir: Path, op: str, *, delta=None, extension_delta=None):
    command = [str(exe), "--state-dir", str(state_dir), "--op", op]
    if delta is not None:
        command.extend(["--delta", str(delta)])
    if extension_delta is not None:
        command.extend(["--extension-delta", str(extension_delta)])
    completed = subprocess.run(
        command,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        timeout=60,
    )
    record = None
    for line in completed.stdout.splitlines():
        try:
            candidate = json.loads(line)
        except ValueError:
            continue
        if isinstance(candidate, dict) and candidate.get("ok"):
            record = candidate
    return completed.returncode, record, completed


def install_v1_layer(state: Path, package: Path, obj_id: int, class_id: int):
    rel = Path(str(obj_id)) / str(class_id) / "1"
    source = package / rel
    target = state / rel
    if not source.is_file():
        raise CheckFailed(f"package missing {rel}")
    if target.exists():
        raise CheckFailed(f"refusing to overwrite existing {rel}")
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, target)


def run_commit_sequence(a_exe: Path, b_exe: Path, state: Path, event_class_id: int):
    code, b1, _ = run(b_exe, state, "commit-v1", delta=1, extension_delta=5)
    if code != 0:
        raise CheckFailed("B commit-v1 failed")
    code, a1, _ = run(a_exe, state, "commit-v0", delta=2)
    if code != 0:
        raise CheckFailed("A commit-v0 failed")
    run(b_exe, state, "commit-v0", delta=4)
    run(b_exe, state, "commit-v1", delta=0, extension_delta=0)
    code, a2, _ = run(a_exe, state, "commit-v0", delta=10)
    if code != 0:
        raise CheckFailed("A final commit-v0 failed")
    assert_tally(a2, 17, who="A final")
    return b1


def record_baseline_replay_doc_only_v1(a_exe: Path, b_exe: Path, work: Path):
    """Replay with document v1 only (base snapshot missing v1) — expect wrong extension."""
    state = work / "baseline-state"
    package = work / "baseline-package"
    state.mkdir()
    package.mkdir()
    code, a0, _ = run(a_exe, state, "create")
    if code != 0 or not a0:
        raise CheckFailed("baseline A create failed")
    doc_id = int(a0["document_id"])
    doc_class_id = int(a0["document_class_id"])
    run(b_exe, package, "export-doc-v1-package")
    install_v1_layer(state, package, doc_id, doc_class_id)
    after_install = manifest(state)
    base_v1_at_install = f"{BASE_ID}/{doc_class_id}/1" in after_install
    if base_v1_at_install:
        raise CheckFailed("baseline doc-only install must not add base v1 layer")
    run_commit_sequence(a_exe, b_exe, state, int(a0["event_class_id"]))
    before = manifest(state)
    code, replay, _ = run(b_exe, state, "replay-from-base")
    after = manifest(state)
    unchanged(before, after, "baseline replay-from-base must not write storage")
    baseline = {
        "replay_exit_code": code,
        "replay_ok": replay.get("replay_ok") if replay else None,
        "tally": replay.get("tally") if replay else None,
        "extension": replay.get("extension") if replay else None,
        "journal_len": replay.get("journal_len") if replay else None,
        "expected_extension": 645,
        "expected_tally": 17,
        "base_v1_at_install": base_v1_at_install,
    }
    print(json.dumps({"baseline_replay_doc_only_v1": baseline}), flush=True)
    return baseline


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--a", type=Path, required=True)
    parser.add_argument("--b", type=Path, required=True)
    parser.add_argument("--work-root", type=Path, required=True)
    args = parser.parse_args()
    args.work_root.mkdir(parents=True, exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix="event-version-", dir=args.work_root))
    baseline_work = root / "baseline"
    baseline_work.mkdir()
    print("Run directory: " + str(root), flush=True)

    baseline = record_baseline_replay_doc_only_v1(args.a, args.b, baseline_work)
    if baseline.get("extension") == 645:
        raise CheckFailed(
            "baseline replay unexpectedly matched extension=645 "
            "(doc-only v1: base snapshot lacks packaged extension)"
        )

    state = root / "state"
    package = root / "packaged-v1"
    state.mkdir()
    package.mkdir()

    code, a0, _ = run(args.a, state, "create")
    if code != 0 or not a0:
        raise CheckFailed("A create failed")

    code, b_pkg, _ = run(args.b, package, "export-v1-package")
    if code != 0 or not b_pkg:
        raise CheckFailed("B export-v1-package failed")
    assert_class_ids_match(a0, b_pkg)

    doc_id = int(a0["document_id"])
    doc_class_id = int(a0["document_class_id"])
    event_class_id = int(a0["event_class_id"])

    v0_path = state / str(doc_id) / str(doc_class_id) / "0"
    base_v0_path = state / str(BASE_ID) / str(doc_class_id) / "0"
    v0_hash = digest(v0_path)
    base_v0_hash = digest(base_v0_path)
    install_v1_layer(state, package, doc_id, doc_class_id)
    install_v1_layer(state, package, BASE_ID, doc_class_id)
    if digest(v0_path) != v0_hash or digest(base_v0_path) != base_v0_hash:
        raise CheckFailed("installing v1 layers changed v0 bytes")
    if doc_version_slots(manifest(state), doc_id, doc_class_id) != {0, 1}:
        raise CheckFailed("document missing v0/v1 slots")
    if doc_version_slots(manifest(state), BASE_ID, doc_class_id) != {0, 1}:
        raise CheckFailed("base snapshot missing v0/v1 slots")

    code, probe, _ = run(args.b, state, "probe-v1-state")
    if code != 0:
        raise CheckFailed("B probe-v1-state failed")
    assert_base_v1_probe(probe, who="B probe after install", base_id=BASE_ID)
    before_probe = manifest(state)
    unchanged(before_probe, before_probe, "probe-v1-state")

    code, a_inst, _ = run(args.a, state, "inspect")
    assert_tally(a_inst, 0, who="A after v1 install")
    assert_no_extension_field(a_inst, who="A")

    run_commit_sequence(args.a, args.b, state, event_class_id)
    doc_v1_hash = manifest(state)[f"{doc_id}/{doc_class_id}/1"]

    before_replay = manifest(state)
    code, b_replay, _ = run(args.b, state, "replay-from-base")
    unchanged(before_replay, manifest(state), "B replay-from-base read-only")
    assert_replay(
        b_replay,
        who="B replay after sequence",
        tally=17,
        extension=645,
        journal_len=5,
    )

    code, b_fault, _ = run(args.b, state, "replay-from-base-fault")
    assert_replay(
        b_fault,
        who="B fault replay",
        tally=17,
        extension=645,
        journal_len=5,
    )

    code, a_replay, _ = run(args.a, state, "replay-from-base")
    assert_replay(
        a_replay,
        who="A replay",
        tally=17,
        journal_len=5,
        extension=None,
    )
    assert_no_extension_field(a_replay, who="A replay output")

    before_a_save = manifest(state)
    code, a_save, _ = run(args.a, state, "replay-from-base-save")
    after_a_save = manifest(state)
    if after_a_save.get(f"{doc_id}/{doc_class_id}/1") != doc_v1_hash:
        raise CheckFailed("A replay-save changed document v1 layer bytes")
    assert_replay(
        a_save,
        who="A replay-save",
        tally=17,
        journal_len=5,
        extension=None,
    )

    code, b_after_a_save, _ = run(args.b, state, "replay-from-base")
    assert_replay(
        b_after_a_save,
        who="B replay after A replay-save",
        tally=17,
        extension=645,
        journal_len=5,
    )

    code, b_load, _ = run(args.b, state, "reload")
    assert_tally(b_load, 17, who="B reload (materialized load)")
    assert_extension(b_load, 645, who="B reload")
    assert_journal_len(b_load, 5, who="B reload")

    first_event_id = int(b_load["events"][0]["event_id"])
    if event_versions(manifest(state), first_event_id, event_class_id) != {0, 1}:
        raise CheckFailed("first v1 event layers must remain after replay roundtrip")

    print(
        json.dumps(
            {
                "result": "PASS",
                "baseline_replay_doc_only_v1": baseline,
                "document_class_id": doc_class_id,
                "event_class_id": event_class_id,
                "save_load": "PASS",
                "replay": "PASS",
                "final_tally": 17,
                "final_extension": 645,
                "journal_len": 5,
            }
        ),
        flush=True,
    )


if __name__ == "__main__":
    try:
        main()
    except CheckFailed as error:
        print(json.dumps({"result": "FAIL", "detail": str(error)}), flush=True)
        raise SystemExit(1) from error
