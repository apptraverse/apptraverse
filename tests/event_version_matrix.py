#!/usr/bin/env python3
"""Same Event ClassId with independent v0 semantics and optional v1 fields."""

import argparse
import json
import shutil
import subprocess
import tempfile
from pathlib import Path

from application_evolution_matrix import CheckFailed, digest, manifest
from event_version_storage_checks import (
    assert_class_ids_match,
    assert_extension,
    assert_journal_len,
    assert_no_extension_field,
    assert_tally,
    doc_version_slots,
    event_versions,
    last_event,
    parse_fixture_record,
)


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
    if completed.returncode == 0:
        record = parse_fixture_record(completed.stdout)
    return completed.returncode, record, completed


def install_doc_v1_layer(state: Path, package: Path, doc_id: int, class_id: int):
    rel = Path(str(doc_id)) / str(class_id) / "1"
    source = package / rel
    target = state / rel
    if not source.is_file():
        raise CheckFailed(f"package missing {rel}")
    if target.exists():
        raise CheckFailed(f"refusing to overwrite existing {rel}")
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, target)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--a", type=Path, required=True)
    parser.add_argument("--b", type=Path, required=True)
    parser.add_argument("--work-root", type=Path, required=True)
    args = parser.parse_args()
    args.work_root.mkdir(parents=True, exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix="event-version-", dir=args.work_root))
    state = root / "state"
    package = root / "packaged-doc-v1"
    state.mkdir()
    package.mkdir()
    print("Run directory: " + str(root), flush=True)

    code, a0, _ = run(args.a, state, "create")
    if code != 0 or not a0:
        raise CheckFailed("A create failed")

    code, b_pkg, _ = run(args.b, package, "export-doc-v1-package")
    if code != 0 or not b_pkg:
        raise CheckFailed("B export-doc-v1-package failed")
    assert_class_ids_match(a0, b_pkg)

    doc_id = int(a0["document_id"])
    doc_class_id = int(a0["document_class_id"])
    event_class_id = int(a0["event_class_id"])

    v0_path = state / str(doc_id) / str(doc_class_id) / "0"
    v0_hash = digest(v0_path)
    install_doc_v1_layer(state, package, doc_id, doc_class_id)
    if doc_version_slots(manifest(state), doc_id, doc_class_id) != {0, 1}:
        raise CheckFailed("expected document v0 and v1 slots after install")
    if digest(v0_path) != v0_hash:
        raise CheckFailed("installing document v1 changed v0 bytes")

    code, b_inst, _ = run(args.b, state, "install-doc-v1-from-user")
    assert_extension(b_inst, 640, who="B after v1 install")
    code, a_inst, _ = run(args.a, state, "inspect")
    assert_tally(a_inst, 0, who="A after v1 install")
    assert_no_extension_field(a_inst, who="A")

    code, b1, _ = run(args.b, state, "commit-v1", delta=1, extension_delta=5)
    if code != 0:
        raise CheckFailed("B commit-v1 failed")
    assert_tally(b1, 1, who="B")
    assert_extension(b1, 645, who="B")
    after_b1 = manifest(state)
    ev1 = last_event(b1)
    if event_versions(after_b1, int(ev1["event_id"]), event_class_id) != {0, 1}:
        raise CheckFailed("B v1 event must persist v0 and v1 layers")
    doc_v1_hash = after_b1[f"{doc_id}/{doc_class_id}/1"]

    code, a1, _ = run(args.a, state, "commit-v0", delta=2)
    if code != 0:
        raise CheckFailed("A commit-v0 failed after B v1 event")
    assert_tally(a1, 3, who="A")
    after_a1 = manifest(state)
    if after_a1.get(f"{doc_id}/{doc_class_id}/1") != doc_v1_hash:
        raise CheckFailed("A save changed document v1 layer bytes")

    code, b2, _ = run(args.b, state, "inspect")
    assert_tally(b2, 3, who="B after A")
    assert_extension(b2, 645, who="B after A")
    assert_journal_len(b2, 2, who="B")

    code, b3, _ = run(args.b, state, "commit-v0", delta=4)
    assert_tally(b3, 7, who="B after v0-only commit")
    assert_extension(b3, 645, who="B after v0-only commit")
    ev_v0 = last_event(b3)
    if ev_v0.get("extension_delta_set") is True:
        raise CheckFailed("v0-only commit must not set extension_delta_set")
    v0_only_layers = event_versions(
        manifest(state), int(ev_v0["event_id"]), event_class_id
    )
    if 0 not in v0_only_layers:
        raise CheckFailed("v0-only commit must write event v0 layer")

    code, b4, _ = run(args.b, state, "commit-v1", delta=0, extension_delta=0)
    assert_extension(b4, 645, who="B after explicit zero extension delta")
    ev_zero = last_event(b4)
    if ev_zero.get("extension_delta_set") is not True:
        raise CheckFailed("commit-v1 must set extension_delta_set even for zero")
    zero_layers = event_versions(
        manifest(state), int(ev_zero["event_id"]), event_class_id
    )
    if zero_layers != {0, 1}:
        raise CheckFailed("explicit zero v1 commit must write both event layers")
    if 1 in v0_only_layers and v0_only_layers == {0, 1}:
        v0_only_v1_key = f"{ev_v0['event_id']}/{event_class_id}/1"
        zero_v1_key = f"{ev_zero['event_id']}/{event_class_id}/1"
        after = manifest(state)
        if after.get(v0_only_v1_key) == after.get(zero_v1_key):
            raise CheckFailed(
                "intentional v1 write must differ from incidental v0-only v1 slot"
            )

    code, a2, _ = run(args.a, state, "commit-v0", delta=10)
    assert_tally(a2, 17, who="A second commit")
    code, b5, _ = run(args.b, state, "reload")
    assert_tally(b5, 17, who="B reload")
    assert_extension(b5, 645, who="B reload")
    assert_journal_len(b5, 5, who="B reload")

    print(
        json.dumps(
            {
                "result": "PASS",
                "document_class_id": doc_class_id,
                "event_class_id": event_class_id,
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
