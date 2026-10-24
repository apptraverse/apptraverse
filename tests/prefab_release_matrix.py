#!/usr/bin/env python3
"""Headless prefab-aware R0→R2 proof (NullPresenter / embedded backing)."""

import argparse
import json
import subprocess
import tempfile
from pathlib import Path

from application_evolution_matrix import CheckFailed, manifest
from event_downgrade_storage_checks import assert_a_refused_without_storage_change

WINDOW_A = 61001
WINDOW_B = 61002
MAIN_PREFAB = 60001


def run(exe, state_dir, op):
    completed = subprocess.run(
        [str(exe), "--state-dir", str(state_dir), "--op", op],
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        timeout=90,
    )
    record = None
    for line in completed.stdout.splitlines():
        try:
            record = json.loads(line)
        except ValueError:
            continue
    return completed.returncode, record, completed


def parse_ok(record):
    if not record or not record.get("ok"):
        raise CheckFailed("missing ok record: " + str(record))


def run_ok(exe, state_dir, op):
    code, rec, comp = run(exe, state_dir, op)
    if code != 0:
        raise CheckFailed(
            f"expected exit 0 for {op}, got {code}: stderr={comp.stderr!r}"
        )
    parse_ok(rec)
    return rec


def layer_key(obj_id: int, class_id: int, version: int) -> str:
    return f"{obj_id}/{class_id}/{version}"


def assert_no_layer(manifest_map, obj_id: int, class_id: int, version: int):
    key = layer_key(obj_id, class_id, version)
    if key in manifest_map:
        raise CheckFailed(f"unexpected USER layer {key}")


def assert_has_layer(manifest_map, obj_id: int, class_id: int, version: int):
    key = layer_key(obj_id, class_id, version)
    if key not in manifest_map:
        raise CheckFailed(f"missing USER layer {key}")


def run_absolute_scenario(r0, r2, work_root: Path):
    state = work_root / "absolute" / "user"
    state.mkdir(parents=True)
    run_ok(r0, state, "r0-init")
    rec = run_ok(r2, state, "r2-verify")
    if int(rec.get("posx_a", 0)) != 10:
        raise CheckFailed("absolute: prefab S1 fallback")
    run_ok(r2, state, "r2-set-posx-a")
    rec = run_ok(r2, state, "r2-get-posx-a")
    if int(rec.get("posx_a", 0)) != 99:
        raise CheckFailed("absolute: USER override")


def run_delta_eager_scenario(r0, r2, work_root: Path):
    state = work_root / "delta_eager" / "user"
    state.mkdir(parents=True)
    run_ok(r0, state, "d1-r0-init")

    before = manifest(state)
    rec = run_ok(r2, state, "d1-r2-readonly-a")
    after = manifest(state)
    if before != after:
        raise CheckFailed("delta eager: readonly changed USER manifest")
    class_id = int(rec["window_class_id"])
    assert_no_layer(before, WINDOW_A, class_id, 1)
    if int(rec.get("posx_a", 0)) != 10:
        raise CheckFailed("delta eager: readonly posx")
    if rec.get("user_s1_instance_present"):
        raise CheckFailed("delta eager: instance S1 present early")
    if rec.get("user_s1_base_present"):
        raise CheckFailed("delta eager: base S1 present early")

    run_ok(r2, state, "d1-r2-delta-a")
    rec = run_ok(r2, state, "d1-r2-inspect-a")
    if int(rec.get("posx_a", 0)) != 15:
        raise CheckFailed("delta eager: instance posx after delta")
    if int(rec.get("base_posx", 0)) != 10:
        raise CheckFailed("delta eager: base S1 must stay prefab default 10")
    if int(rec.get("journal_len", 0)) != 1:
        raise CheckFailed("delta eager: journal length")
    assert_has_layer(manifest(state), WINDOW_A, class_id, 1)
    inst_base = int(rec["instance_base_id"])
    prefab_base = int(rec["prefab_base_id"])
    if inst_base == 0 or prefab_base == 0 or inst_base == prefab_base:
        raise CheckFailed("delta eager: base ObjId chain")
    assert_has_layer(manifest(state), inst_base, class_id, 1)

    rec = run_ok(r2, state, "d1-r2-replay-a")
    if int(rec.get("materialized_posx", 0)) != 15:
        raise CheckFailed("delta eager: materialized replay load")
    if int(rec.get("replay_posx", 0)) != 15:
        raise CheckFailed("delta eager: replay posx")
    if int(rec.get("replay_twice", 0)) != 15:
        raise CheckFailed("delta eager: replay idempotent")

    rec = run_ok(r2, state, "d1-r2-replay-a-fault")
    if int(rec.get("replay_posx", 0)) != 15:
        raise CheckFailed("delta eager: fault injection replay")

    rec = run_ok(r2, state, "d1-r2-replay-a-v30")
    if int(rec.get("replay_posx", 0)) != 15:
        raise CheckFailed("delta eager: v30 backing must not change replay to 35")


def run_delta_lazy_scenario(r0, r2, work_root: Path):
    state = work_root / "delta_lazy" / "user"
    state.mkdir(parents=True)
    run_ok(r0, state, "d1l-r0-init")

    rec = run_ok(r2, state, "d1l-r2-load-ws")
    if rec.get("window_b_loaded"):
        raise CheckFailed("lazy: window B loaded on workspace load")

    rec = run_ok(r2, state, "d1l-r2-delta-b")
    if int(rec.get("lazy_load_count", 0)) < 1:
        raise CheckFailed("lazy: explicit load must increment lazy counter")
    rec = run_ok(r2, state, "d1l-r2-replay-b")
    if int(rec.get("materialized_posx", 0)) != 17:
        raise CheckFailed("lazy: materialized 17")
    if int(rec.get("replay_posx", 0)) != 17:
        raise CheckFailed("lazy: replay 17")
def run_unknown_flat(unknown_old, unknown_new, work_root: Path):
    state = work_root / "unknown_flat" / "user"
    state.mkdir(parents=True)
    b_write = run_ok(unknown_new, state, "write-unknown")
    probe_layer = f"{b_write['object_id']}/{b_write['probe_class_id']}/0"
    before = manifest(state)
    if probe_layer not in before:
        raise CheckFailed("unknown flat: probe layer missing")
    code, a_resave, _ = run(unknown_old, state, "resave")
    after = manifest(state)
    assert_a_refused_without_storage_change(
        before=before,
        after=after,
        obj_key_prefix=f"{b_write['object_id']}/",
        probe_layer=probe_layer,
        a_code=code,
        a_record=a_resave,
        forbidden_class_ids=set(),
    )
    b_load = run_ok(unknown_new, state, "load-verify")
    if int(b_load.get("payload", 0)) != 42:
        raise CheckFailed("unknown flat: payload")


def run_unknown_journal(unknown_old, unknown_new, work_root: Path):
    state = work_root / "unknown_journal" / "user"
    state.mkdir(parents=True)
    write = run_ok(unknown_new, state, "write-journal-unknown")
    if int(write.get("journal_len", 0)) != 1:
        raise CheckFailed("unknown journal: journal len on write")
    if int(write.get("tag", 0)) != 42:
        raise CheckFailed("unknown journal: materialized tag on write")
    before = manifest(state)

    code, load_rec, _ = run(unknown_old, state, "load-journal-target")
    after = manifest(state)
    if code == 0 and load_rec and load_rec.get("ok"):
        raise CheckFailed("old EXE must refuse LoadRoot with unknown journal Event")
    if before != after:
        raise CheckFailed("unknown journal: USER changed after old load attempt")

    verify = run_ok(unknown_new, state, "verify-journal-target")
    if int(verify.get("journal_len", 0)) != 1:
        raise CheckFailed("unknown journal: journal len on reload")
    if int(verify.get("tag", 0)) != 42:
        raise CheckFailed("unknown journal: tag")
    if int(verify.get("replay_tag", 0)) != 42:
        raise CheckFailed("unknown journal: replay")
    if int(verify.get("replay_twice", 0)) != 42:
        raise CheckFailed("unknown journal: replay idempotent")


def run_prefab_origin_smoke(r0, r2, work_root: Path):
    state = work_root / "prefab_origin_smoke" / "user"
    state.mkdir(parents=True)
    run_ok(r0, state, "r0-init")
    rec = run_ok(r2, state, "r2-verify")
    if int(rec.get("window_a_instance_id", 0)) != WINDOW_A:
        raise CheckFailed("smoke: window A instance id")
    if int(rec.get("window_a_prefab_id", 0)) != MAIN_PREFAB:
        raise CheckFailed("smoke: window A prefab id")
    if rec.get("alias_ok") is not True:
        raise CheckFailed("smoke: alias")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--r0", type=Path, required=True)
    parser.add_argument("--r2", type=Path, required=True)
    parser.add_argument("--unknown-old", type=Path, required=True)
    parser.add_argument("--unknown-new", type=Path, required=True)
    parser.add_argument("--work-root", type=Path, required=True)
    args = parser.parse_args()
    args.work_root.mkdir(parents=True, exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix="prefab-release-", dir=args.work_root))
    print("Run directory: " + str(root), flush=True)

    run_prefab_origin_smoke(args.r0, args.r2, root)
    run_absolute_scenario(args.r0, args.r2, root)
    run_delta_eager_scenario(args.r0, args.r2, root)
    run_delta_lazy_scenario(args.r0, args.r2, root)
    run_unknown_flat(args.unknown_old, args.unknown_new, root)
    run_unknown_journal(args.unknown_old, args.unknown_new, root)

    print("prefab_release_matrix OK", flush=True)


if __name__ == "__main__":
    main()
