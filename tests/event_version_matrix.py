#!/usr/bin/env python3
"""Same ClassId Event v0/v1: materialized save/load plus TryReplayFromBase."""

import argparse
import json
import shutil
import tempfile
from pathlib import Path

from application_evolution_matrix import CheckFailed, digest, manifest, unchanged
from event_version_runner import (
    assert_baseline_doc_only_v1_replay,
    assert_v1_layers_unchanged,
    invoke_fixture,
    run_runner_selftests,
    v1_layer_snapshot,
)
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
)

BASE_ID = 94002


def run_overlay_version_roundtrip(a_exe: Path, b_exe: Path, work: Path) -> None:
    state = work / "user"
    package = work / "package"
    state.mkdir()
    package.mkdir()

    a_create = invoke_fixture(
        a_exe, "A", state, "create", expect_stage="create"
    )
    doc_id = int(a_create["document_id"])
    doc_class_id = int(a_create["document_class_id"])
    event_class_id = int(a_create["event_class_id"])
    assert_tally(a_create, 0, who="overlay A create")
    assert_journal_len(a_create, 0, who="overlay A create")
    user_after_create = manifest(state)
    if f"{doc_id}/{doc_class_id}/1" in user_after_create:
        raise CheckFailed("overlay A create: document v1 must not be in USER")
    if f"{BASE_ID}/{doc_class_id}/1" in user_after_create:
        raise CheckFailed("overlay A create: base v1 must not be in USER")

    invoke_fixture(
        b_exe,
        "B",
        package,
        "export-v1-package",
        expect_stage="export-v1-package",
        check_storage_unchanged=False,
    )
    package_snapshot = manifest(package)

    user_before_inspect = manifest(state)
    inspect_c = invoke_fixture(
        b_exe,
        "B",
        state,
        "inspect",
        backing_dir=package,
    )
    assert_tally(inspect_c, 0, who="overlay B inspect with backing")
    assert_extension(inspect_c, 640, who="overlay B inspect with backing")
    assert_journal_len(inspect_c, 0, who="overlay B inspect with backing")
    if int(inspect_c.get("base_extension", -1)) != 640:
        raise CheckFailed("overlay B inspect base_extension expected 640")
    unchanged(user_before_inspect, manifest(state), "overlay B inspect USER")
    user_after_inspect = manifest(state)
    if f"{doc_id}/{doc_class_id}/1" in user_after_inspect:
        raise CheckFailed("overlay B inspect: document v1 must not be in USER")
    if f"{BASE_ID}/{doc_class_id}/1" in user_after_inspect:
        raise CheckFailed("overlay B inspect: base v1 must not be in USER")

    commit_d = invoke_fixture(
        b_exe,
        "B",
        state,
        "commit-v1",
        delta=1,
        extension_delta=5,
        backing_dir=package,
        check_storage_unchanged=False,
    )
    assert_tally(commit_d, 1, who="overlay B commit-v1")
    assert_extension(commit_d, 645, who="overlay B commit-v1")
    assert_journal_len(commit_d, 1, who="overlay B commit-v1")
    event_id = int(commit_d["events"][0]["event_id"])
    user_after_commit = manifest(state)
    for key in (
        f"{doc_id}/{doc_class_id}/1",
        f"{BASE_ID}/{doc_class_id}/1",
        f"{event_id}/{event_class_id}/1",
    ):
        if key not in user_after_commit:
            raise CheckFailed(f"overlay B commit-v1: missing USER layer {key}")

    inspect_d = invoke_fixture(b_exe, "B", state, "inspect")
    assert_tally(inspect_d, 1, who="overlay B inspect after commit")
    assert_extension(inspect_d, 645, who="overlay B inspect after commit")
    if int(inspect_d.get("base_extension", -1)) != 640:
        raise CheckFailed("overlay B inspect after commit base_extension expected 640")
    assert_journal_len(inspect_d, 1, who="overlay B inspect after commit")

    v1_before_a = v1_layer_snapshot(
        user_after_commit,
        doc_id=doc_id,
        base_id=BASE_ID,
        doc_class_id=doc_class_id,
        event_class_id=event_class_id,
    )

    commit_e = invoke_fixture(
        a_exe, "A", state, "commit-v0", delta=2, check_storage_unchanged=False
    )
    assert_tally(commit_e, 3, who="overlay A commit-v0")
    assert_journal_len(commit_e, 2, who="overlay A commit-v0")
    assert_no_extension_field(commit_e, who="overlay A commit-v0")
    assert_v1_layers_unchanged(
        v1_before_a,
        v1_layer_snapshot(
            manifest(state),
            doc_id=doc_id,
            base_id=BASE_ID,
            doc_class_id=doc_class_id,
            event_class_id=event_class_id,
        ),
        context="overlay A commit-v0 preserves v1 layers",
    )

    user_before_fault = manifest(state)
    fault_f = invoke_fixture(b_exe, "B", state, "replay-from-base-fault")
    assert_replay(
        fault_f,
        who="overlay B replay-from-base-fault",
        tally=3,
        journal_len=2,
        extension=645,
    )
    if int(fault_f.get("base_extension", -1)) != 640:
        raise CheckFailed("overlay B fault replay base_extension expected 640")
    unchanged(user_before_fault, manifest(state), "overlay B fault replay USER")

    reload_g = invoke_fixture(b_exe, "B", state, "reload")
    assert_tally(reload_g, 3, who="overlay B final reload")
    assert_extension(reload_g, 645, who="overlay B final reload")
    assert_journal_len(reload_g, 2, who="overlay B final reload")
    unchanged(package_snapshot, manifest(package), "overlay PACKAGE unchanged")


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


def run_commit_sequence(
    a_exe: Path,
    b_exe: Path,
    state: Path,
    doc_id: int,
    base_id: int,
    doc_class_id: int,
    event_class_id: int,
):
    def snap_v1() -> dict[str, str]:
        return v1_layer_snapshot(
            manifest(state),
            doc_id=doc_id,
            base_id=base_id,
            doc_class_id=doc_class_id,
            event_class_id=event_class_id,
        )

    invoke_fixture(
        b_exe,
        "B",
        state,
        "commit-v1",
        delta=1,
        extension_delta=5,
        check_storage_unchanged=False,
    )
    v1_before_a = snap_v1()
    invoke_fixture(
        a_exe, "A", state, "commit-v0", delta=2, check_storage_unchanged=False
    )
    assert_v1_layers_unchanged(
        v1_before_a, snap_v1(), context="A commit-v0 preserves v1 layers"
    )

    invoke_fixture(
        b_exe, "B", state, "commit-v0", delta=4, check_storage_unchanged=False
    )
    invoke_fixture(
        b_exe,
        "B",
        state,
        "commit-v1",
        delta=0,
        extension_delta=0,
        check_storage_unchanged=False,
    )

    v1_before_a_final = snap_v1()
    a_final = invoke_fixture(
        a_exe, "A", state, "commit-v0", delta=10, check_storage_unchanged=False
    )
    assert_v1_layers_unchanged(
        v1_before_a_final, snap_v1(), context="A final commit-v0 preserves v1 layers"
    )
    assert_tally(a_final, 17, who="A final")


def record_baseline_replay_doc_only_v1(a_exe: Path, b_exe: Path, work: Path) -> dict:
    state = work / "baseline-state"
    package = work / "baseline-package"
    state.mkdir()
    package.mkdir()
    a0 = invoke_fixture(a_exe, "A", state, "create", expect_stage="create")
    doc_id = int(a0["document_id"])
    doc_class_id = int(a0["document_class_id"])
    event_class_id = int(a0["event_class_id"])

    invoke_fixture(
        b_exe,
        "B",
        package,
        "export-doc-v1-package",
        expect_stage="export-doc-v1-package",
        check_storage_unchanged=False,
    )
    install_v1_layer(state, package, doc_id, doc_class_id)
    if f"{BASE_ID}/{doc_class_id}/1" in manifest(state):
        raise CheckFailed("baseline doc-only install must not add base v1 layer")

    run_commit_sequence(
        a_exe, b_exe, state, doc_id, BASE_ID, doc_class_id, event_class_id
    )
    before = manifest(state)
    replay = invoke_fixture(
        b_exe,
        "B",
        state,
        "replay-from-base",
        expect_stage="replay-from-base",
        check_storage_unchanged=True,
    )
    after = manifest(state)
    assert_baseline_doc_only_v1_replay(
        replay,
        exit_code=0,
        before_manifest=before,
        after_manifest=after,
    )
    baseline = {
        "replay_exit_code": 0,
        "replay_ok": True,
        "tally": 17,
        "extension": 5,
        "journal_len": 5,
        "base_v1_at_install": False,
        "event_class_id": event_class_id,
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

    run_runner_selftests()

    root = Path(tempfile.mkdtemp(prefix="event-version-", dir=args.work_root))
    baseline_work = root / "baseline"
    baseline_work.mkdir()
    print("Run directory: " + str(root), flush=True)

    baseline = record_baseline_replay_doc_only_v1(args.a, args.b, baseline_work)

    state = root / "state"
    package = root / "packaged-v1"
    state.mkdir()
    package.mkdir()

    a0 = invoke_fixture(args.a, "A", state, "create", expect_stage="create")
    b_pkg = invoke_fixture(
        args.b,
        "B",
        package,
        "export-v1-package",
        expect_stage="export-v1-package",
        check_storage_unchanged=False,
    )
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

    probe = invoke_fixture(args.b, "B", state, "probe-v1-state")
    assert_base_v1_probe(probe, who="B probe after install", base_id=BASE_ID)

    a_inst = invoke_fixture(args.a, "A", state, "inspect")
    assert_tally(a_inst, 0, who="A after v1 install")
    assert_no_extension_field(a_inst, who="A")

    run_commit_sequence(
        args.a, args.b, state, doc_id, BASE_ID, doc_class_id, event_class_id
    )
    doc_v1_hash = manifest(state)[f"{doc_id}/{doc_class_id}/1"]

    b_replay = invoke_fixture(args.b, "B", state, "replay-from-base")
    assert_replay(
        b_replay,
        who="B replay after sequence",
        tally=17,
        extension=645,
        journal_len=5,
    )

    b_fault = invoke_fixture(args.b, "B", state, "replay-from-base-fault")
    assert_replay(
        b_fault,
        who="B fault replay",
        tally=17,
        extension=645,
        journal_len=5,
    )

    a_replay = invoke_fixture(args.a, "A", state, "replay-from-base")
    assert_replay(
        a_replay,
        who="A replay",
        tally=17,
        journal_len=5,
        extension=None,
    )
    assert_no_extension_field(a_replay, who="A replay output")

    v1_before_a_save = v1_layer_snapshot(
        manifest(state),
        doc_id=doc_id,
        base_id=BASE_ID,
        doc_class_id=doc_class_id,
        event_class_id=event_class_id,
    )
    a_save = invoke_fixture(
        args.a,
        "A",
        state,
        "replay-from-base-save",
        expect_stage="replay-from-base-save",
        check_storage_unchanged=False,
    )
    after_a_save = manifest(state)
    assert_v1_layers_unchanged(
        v1_before_a_save,
        v1_layer_snapshot(
            after_a_save,
            doc_id=doc_id,
            base_id=BASE_ID,
            doc_class_id=doc_class_id,
            event_class_id=event_class_id,
        ),
        context="A replay-save preserves v1 layers",
    )
    if after_a_save.get(f"{doc_id}/{doc_class_id}/1") != doc_v1_hash:
        raise CheckFailed("A replay-save changed document v1 layer bytes")
    assert_replay(
        a_save,
        who="A replay-save",
        tally=17,
        journal_len=5,
        extension=None,
    )

    b_after_a_save = invoke_fixture(args.b, "B", state, "replay-from-base")
    assert_replay(
        b_after_a_save,
        who="B replay after A replay-save",
        tally=17,
        extension=645,
        journal_len=5,
    )

    b_load = invoke_fixture(args.b, "B", state, "reload")
    assert_tally(b_load, 17, who="B reload (materialized load)")
    assert_extension(b_load, 645, who="B reload")
    assert_journal_len(b_load, 5, who="B reload")

    first_event_id = int(b_load["events"][0]["event_id"])
    if event_versions(manifest(state), first_event_id, event_class_id) != {0, 1}:
        raise CheckFailed("first v1 event layers must remain after replay roundtrip")

    overlay_work = root / "overlay-roundtrip"
    overlay_work.mkdir()
    run_overlay_version_roundtrip(args.a, args.b, overlay_work)

    print(
        json.dumps(
            {
                "result": "PASS",
                "runner_selftests": "PASS",
                "baseline_replay_doc_only_v1": baseline,
                "document_class_id": doc_class_id,
                "event_class_id": event_class_id,
                "save_load": "PASS",
                "replay": "PASS",
                "overlay_roundtrip": "PASS",
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
