#!/usr/bin/env python3
"""Prefab S0 -> two instances -> new prefab layer S1 delivered through origin.

rev0 and rev1 are the same fixture source built as two schema revisions of
EvolutionItem. Every step is a separate process (cold load) over a disk USER
profile and a release BACKING; instances are created by CloneCanonicalRoot and
changed only through committed Events.
"""

import argparse
import json
import shutil
import subprocess
import tempfile
from pathlib import Path
from typing import Optional

from application_evolution_matrix import CheckFailed, manifest, unchanged

P_ID = 95001
A_ID = 95011
B_ID = 95012
P_VALUE = 150
P_TAG = 252
A_VALUE = 1601
A_VALUE_AFTER_S1 = 1700
B_TAG = 999


def run(exe: Path, op: str, backing: Path, user: Optional[Path] = None,
        target: Optional[str] = None, number: Optional[int] = None) -> dict:
    command = [str(exe), "--backing-dir", str(backing), "--op", op]
    if user is not None:
        command += ["--user-dir", str(user)]
    if target is not None:
        command += ["--target", target]
    if number is not None:
        command += ["--number", str(number)]
    completed = subprocess.run(command, capture_output=True, text=True,
                               encoding="utf-8", errors="replace", timeout=90)
    record = None
    for line in completed.stdout.splitlines():
        try:
            record = json.loads(line)
        except ValueError:
            continue
    if completed.returncode != 0 or not record or not record.get("ok"):
        raise CheckFailed(
            f"{exe.name} {op}: exit {completed.returncode}, "
            f"stderr={completed.stderr.strip()!r}, record={record}")
    return record


def user_layers(user: Path) -> set[tuple[int, int, int]]:
    layers = set()
    for path in user.rglob("*"):
        relative = path.relative_to(user)
        if path.is_file() and len(relative.parts) == 3:
            layers.add(tuple(int(part) for part in relative.parts))
    return layers


def expect(condition: bool, message: str):
    if not condition:
        raise CheckFailed(message)


def expect_item(record: dict, name: str, **fields):
    item = record[name]
    for key, want in fields.items():
        expect(item.get(key) == want,
               f"{record['op']} rev{record['revision']}: {name}.{key} "
               f"expected {want}, got {item.get(key)}")


def expect_replay_item(record: dict, name: str, want_tag: int, want_value: int,
                       context: str):
    item = record[name]
    expect(item.get("model_tag_before") == want_tag,
           f"{context}: {name} model tag before replay")
    expect(item.get("base_tag_before") == want_tag,
           f"{context}: {name} base tag before replay")
    expect(item.get("replay_ok") is True,
           f"{context}: {name} replay_ok")
    expect(item.get("model_tag_after") == want_tag,
           f"{context}: {name} model tag after replay")
    expect(item.get("model_value_after") == want_value,
           f"{context}: {name} model value after replay")


def run_fresh_replay_scenario(rev1: Path, work_root: Path):
    root = Path(tempfile.mkdtemp(prefix="origin-replay-", dir=work_root))
    user = root / "user"
    backing = root / "backing"
    try:
        run(rev1, "export-backing", backing)
        run(rev1, "create-instances", backing, user)
        cold = run(rev1, "inspect", backing, user)
        expect_item(cold, "a", tag=P_TAG, own_tag=False, value=P_VALUE)
        expect_item(cold, "b", tag=P_TAG, own_tag=False, value=P_VALUE)
        replay = run(rev1, "replay-base", backing, user)
        expect_replay_item(replay, "a", P_TAG, P_VALUE, "fresh replay")
        expect_replay_item(replay, "b", P_TAG, P_VALUE, "fresh replay")
    finally:
        shutil.rmtree(root, ignore_errors=True)


def run_compact_s0_scenario(rev1: Path, work_root: Path):
    root = Path(tempfile.mkdtemp(prefix="origin-compact-", dir=work_root))
    user = root / "user"
    backing = root / "backing"
    try:
        run(rev1, "export-backing", backing)
        backing_manifest = manifest(backing)
        run(rev1, "create-instances", backing, user)
        delivered = run(rev1, "inspect", backing, user)
        expect_item(delivered, "a", tag=P_TAG, own_tag=False)
        edited = run(rev1, "set-value", backing, user, "a", A_VALUE)
        expect_item(edited, "a", value=A_VALUE, tag=P_TAG, own_tag=False,
                    journal_len=1)
        compact = run(rev1, "compact-journal", backing, user)
        expect(compact["journal_a_before"] > 0,
               "compact S0-only: journal A non-empty before compact")
        expect(compact["journal_a_after"] == 0,
               "compact S0-only: journal A empty after compact")
        expect_item(compact, "a", value=A_VALUE, tag=P_TAG, own_tag=False,
                    journal_len=0, base_value=A_VALUE)
        expect_user_layers(user, compact, set(), "compact S0-only")
        cold = run(rev1, "inspect", backing, user)
        expect_item(cold, "a", value=A_VALUE, tag=P_TAG, own_tag=False,
                    journal_len=0, base_value=A_VALUE)
        replay = run(rev1, "replay-base", backing, user)
        expect_replay_item(replay, "a", P_TAG, A_VALUE, "compact S0 replay")
        expect_replay_item(replay, "b", P_TAG, P_VALUE, "compact S0 replay")
        expect_user_layers(user, cold, set(), "compact cold load")
        unchanged(backing_manifest, manifest(backing), "compact BACKING")
    finally:
        shutil.rmtree(root, ignore_errors=True)


def expect_lazy_origin(record: dict):
    expect(not record["p_materialized"],
           f"{record['op']}: prefab P materialized in the instance Domain")
    for name in ("a", "b"):
        item = record[name]
        expect(item["origin_id"] == P_ID, f"{record['op']}: {name} origin")
        expect(item["base_origin_id"] == P_ID,
               f"{record['op']}: {name} historical base origin")


def expect_user_layers(user: Path, record: dict, own_s1: set[int], context: str):
    layers = user_layers(user)
    item_class = record["item_class_id"]
    obj_class = record["obj_class_id"]
    expect(not any(obj_id == P_ID for obj_id, _, _ in layers),
           f"{context}: prefab P layers copied into USER")
    nodes = {A_ID: record["a"]["base_id"], B_ID: record["b"]["base_id"]}
    for node_id, base_id in nodes.items():
        for obj_id in (node_id, base_id):
            expect((obj_id, obj_class, 0) in layers, f"{context}: {obj_id} Obj/S0")
            expect((obj_id, obj_class, 1) not in layers, f"{context}: {obj_id} Obj/S1")
            expect((obj_id, item_class, 0) in layers, f"{context}: {obj_id} Item/S0")
        has_s1 = (node_id, item_class, 1) in layers
        expect(has_s1 == (node_id in own_s1),
               f"{context}: USER/{node_id} Item/S1 present={has_s1}")
        expect((base_id, item_class, 1) not in layers,
               f"{context}: USER/base {base_id} Item/S1 copied")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--rev0", type=Path, required=True)
    parser.add_argument("--rev1", type=Path, required=True)
    parser.add_argument("--work-root", type=Path, required=True)
    args = parser.parse_args()
    rev0 = args.rev0.resolve(strict=True)
    rev1 = args.rev1.resolve(strict=True)
    args.work_root.mkdir(parents=True, exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix="origin-evolution-", dir=args.work_root))
    user = root / "user"
    backing_r0 = root / "backing_r0"
    backing_r1 = root / "backing_r1"

    try:
        run_fresh_replay_scenario(rev1, args.work_root)
        run_compact_s0_scenario(rev1, args.work_root)

        run(rev0, "export-backing", backing_r0)
        r0_manifest = manifest(backing_r0)

        created = run(rev0, "create-instances", backing_r0, user)
        expect_item(created, "a", value=P_VALUE, journal_len=0, base_value=P_VALUE)
        expect_item(created, "b", value=P_VALUE, journal_len=0, base_value=P_VALUE)
        expect_lazy_origin(created)
        expect_user_layers(user, created, set(), "rev0 create-instances")

        edited = run(rev0, "set-value", backing_r0, user, "a", A_VALUE)
        expect_item(edited, "a", value=A_VALUE, journal_len=1)
        expect_item(edited, "b", value=P_VALUE, journal_len=0)

        cold0 = run(rev0, "inspect", backing_r0, user)
        expect_item(cold0, "a", value=A_VALUE, journal_len=1, base_value=P_VALUE)
        expect_item(cold0, "b", value=P_VALUE, journal_len=0)
        expect_lazy_origin(cold0)
        unchanged(r0_manifest, manifest(backing_r0), "rev0 BACKING")

        run(rev1, "export-backing", backing_r1)
        r1_manifest = manifest(backing_r1)
        user_before_s1 = manifest(user)

        delivered = run(rev1, "inspect", backing_r1, user)
        expect_item(delivered, "a", value=A_VALUE, tag=P_TAG, own_tag=False,
                    journal_len=1, base_value=P_VALUE, base_tag=P_TAG)
        expect_item(delivered, "b", value=P_VALUE, tag=P_TAG, own_tag=False,
                    journal_len=0, base_tag=P_TAG)
        expect_lazy_origin(delivered)
        unchanged(user_before_s1, manifest(user), "USER after rev1 read-only load")

        resaved = run(rev1, "resave", backing_r1, user)
        expect_item(resaved, "a", value=A_VALUE, tag=P_TAG)
        expect_lazy_origin(resaved)
        expect_user_layers(user, resaved, set(), "rev1 resave")

        tagged = run(rev1, "set-tag", backing_r1, user, "b", B_TAG)
        expect_item(tagged, "b", tag=B_TAG, own_tag=True, journal_len=1)
        expect_item(tagged, "a", tag=P_TAG, own_tag=False)
        expect_user_layers(user, tagged, {B_ID}, "rev1 set-tag b")

        priority = run(rev1, "inspect", backing_r1, user)
        expect_item(priority, "a", value=A_VALUE, tag=P_TAG, own_tag=False)
        expect_item(priority, "b", value=P_VALUE, tag=B_TAG, own_tag=True,
                    base_tag=P_TAG)
        expect_lazy_origin(priority)

        pub = run(rev1, "publish-ui", backing_r1, user, number=A_VALUE_AFTER_S1)
        expect(pub["model_a_tag"] == P_TAG, "publish-ui: model A tag")
        expect(pub["model_b_tag"] == B_TAG, "publish-ui: model B tag")
        expect(pub["mirror_a_tag_initial"] == pub["model_a_tag"],
               "publish-ui: A initial mirror tag must match model")
        expect(pub["mirror_b_tag_initial"] == pub["model_b_tag"],
               "publish-ui: B initial mirror tag must match model")
        expect(pub["mirror_a_tag_structural"] == pub["model_a_tag"],
               "publish-ui: A structural mirror tag must match model")
        expect(pub["mirror_b_tag_structural"] == pub["model_b_tag"],
               "publish-ui: B structural mirror tag must match model")
        expect(pub["model_a_value_after_event"] == A_VALUE_AFTER_S1,
               "publish-ui: model A value after event")
        expect(pub["mirror_a_value_structural"] == A_VALUE_AFTER_S1,
               "publish-ui: A value must reach UI mirror after structural pub")
        expect(pub["mirror_b_value_structural"] == pub["mirror_b_value_initial"],
               "publish-ui: B value unchanged in mirror")

        after_event = run(rev1, "set-value", backing_r1, user, "a", A_VALUE_AFTER_S1)
        expect_item(after_event, "a", value=A_VALUE_AFTER_S1, tag=P_TAG,
                    journal_len=2)

        final = run(rev1, "inspect", backing_r1, user)
        expect_item(final, "a", value=A_VALUE_AFTER_S1, tag=P_TAG, own_tag=False,
                    journal_len=2, base_value=P_VALUE, base_tag=P_TAG)
        expect_item(final, "b", value=P_VALUE, tag=B_TAG, own_tag=True,
                    journal_len=1)
        expect_lazy_origin(final)
        expect_user_layers(user, final, {B_ID}, "rev1 final")
        unchanged(r1_manifest, manifest(backing_r1), "rev1 BACKING")

        print("origin_evolution_matrix OK", flush=True)
        return 0
    finally:
        shutil.rmtree(root, ignore_errors=True)


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except CheckFailed as error:
        print(f"origin_evolution_matrix FAIL: {error}", flush=True)
        raise SystemExit(1)
