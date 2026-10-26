#!/usr/bin/env python3
"""Cross-process origin facet roundtrip on disk USER/BACKING (12B.2)."""

import argparse
import hashlib
import json
import shutil
import subprocess
import tempfile
from pathlib import Path

from application_evolution_matrix import CheckFailed, manifest, unchanged

P_ID = 81001
A_ID = 81002
P_VALUE = 150
MUTATED_VALUE = 1601


def digest_file(path: Path) -> str:
    result = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            result.update(block)
    return result.hexdigest()


def user_layer_map(user_root: Path) -> dict[str, str]:
    layers = {}
    if not user_root.exists():
        return layers
    for path in sorted(user_root.rglob("*")):
        if not path.is_file():
            continue
        relative = path.relative_to(user_root)
        if len(relative.parts) != 3:
            continue
        key = relative.as_posix()
        layers[key] = digest_file(path)
    return layers


def run(exe: Path, state_root: Path, op: str):
    completed = subprocess.run(
        [str(exe), "--state-dir", str(state_root), "--op", op],
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


def run_ok(exe: Path, state_root: Path, op: str):
    code, rec, comp = run(exe, state_root, op)
    if code != 0:
        raise CheckFailed(
            f"expected exit 0 for {op}, got {code}: "
            f"stderr={comp.stderr!r} stdout={comp.stdout!r}"
        )
    if not rec or not rec.get("ok"):
        raise CheckFailed(f"missing ok for {op}: {rec}")
    return rec


def layer_key(obj_id: int, class_id: int, version: int) -> str:
    return f"{obj_id}/{class_id}/{version}"


def assert_user_layers(user_root: Path, derived_class_id: int, obj_class_id: int):
    layers = user_layer_map(user_root)
    keys = set(layers)
    need_s0 = layer_key(A_ID, derived_class_id, 0)
    need_obj_s1 = layer_key(A_ID, obj_class_id, 1)
    if need_s0 not in keys:
        raise CheckFailed(f"missing USER layer {need_s0}")
    if need_obj_s1 not in keys:
        raise CheckFailed(f"missing USER layer {need_obj_s1}")
    bad_derived_s1 = layer_key(A_ID, derived_class_id, 1)
    if bad_derived_s1 in keys:
        raise CheckFailed(f"unexpected USER layer {bad_derived_s1}")
    prefix = f"{P_ID}/"
    for key in keys:
        if key.startswith(prefix):
            raise CheckFailed(f"P layer in USER: {key}")


def assert_inspect(rec: dict, *, value: int):
    if int(rec.get("instance_id", 0)) != A_ID:
        raise CheckFailed("instance ObjId")
    if int(rec.get("value", 0)) != value:
        raise CheckFailed(f"value expected {value}, got {rec.get('value')}")
    if int(rec.get("origin_id", 0)) != P_ID:
        raise CheckFailed("origin_id")
    if rec.get("p_materialized"):
        raise CheckFailed("P materialized during inspect")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--fixture", type=Path, required=True)
    parser.add_argument("--work-root", type=Path, required=True)
    args = parser.parse_args()

    fixture = args.fixture.resolve(strict=True)
    args.work_root.mkdir(parents=True, exist_ok=True)
    state_root = Path(tempfile.mkdtemp(prefix="origin-disk-", dir=args.work_root))

    try:
        run_ok(fixture, state_root, "init-backing")
        backing_manifest = manifest(state_root / "backing")

        create_rec = run_ok(fixture, state_root, "create-instance")
        derived_class_id = int(create_rec["derived_class_id"])
        obj_class_id = int(create_rec["obj_class_id"])

        unchanged(backing_manifest, manifest(state_root / "backing"), "BACKING after create-instance")
        assert_user_layers(state_root / "user", derived_class_id, obj_class_id)

        inspect_rec = run_ok(fixture, state_root, "inspect")
        assert_inspect(inspect_rec, value=P_VALUE)
        unchanged(backing_manifest, manifest(state_root / "backing"), "BACKING after inspect")

        run_ok(fixture, state_root, "mutate")
        unchanged(backing_manifest, manifest(state_root / "backing"), "BACKING after mutate")
        inspect_mut = run_ok(fixture, state_root, "inspect")
        assert_inspect(inspect_mut, value=MUTATED_VALUE)

        layers_before_resave = user_layer_map(state_root / "user")
        run_ok(fixture, state_root, "resave")
        unchanged(backing_manifest, manifest(state_root / "backing"), "BACKING after resave")
        layers_after_resave = user_layer_map(state_root / "user")
        if layers_before_resave != layers_after_resave:
            raise CheckFailed("USER layer bytes changed after resave")

        inspect_final = run_ok(fixture, state_root, "inspect")
        assert_inspect(inspect_final, value=MUTATED_VALUE)
        user_after_resave_inspect = manifest(state_root / "user")

        run_ok(fixture, state_root, "inspect")
        unchanged(
            user_after_resave_inspect,
            manifest(state_root / "user"),
            "USER after second read-only inspect",
        )

        print("object_origin_disk_matrix OK", flush=True)
        return 0
    finally:
        shutil.rmtree(state_root, ignore_errors=True)


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except CheckFailed as error:
        print(f"object_origin_disk_matrix FAIL: {error}", flush=True)
        raise SystemExit(1)
