"""Shared storage invariants for event downgrade matrices."""

from __future__ import annotations

from application_evolution_matrix import CheckFailed


def assert_a_refused_without_storage_change(
    *,
    before: dict,
    after: dict,
    obj_key_prefix: str,
    probe_layer: str,
    a_code: int,
    a_record: dict | None,
    forbidden_class_ids: set[int],
) -> None:
    if a_code == 0 and a_record and a_record.get("ok"):
        loaded = a_record.get("loaded_class_id")
        if loaded in forbidden_class_ids:
            raise CheckFailed(
                f"A materialized forbidden sibling class_id={loaded} "
                "(unknown stored type substitution)"
            )
        raise CheckFailed(
            "A must refuse LoadRoot/resave for storage with unknown stored class"
        )
    if a_record and a_record.get("ok"):
        raise CheckFailed("A reported successful load for unknown Event storage")

    added_layers = sorted(
        k for k in after if k not in before and k.startswith(obj_key_prefix)
    )
    if after != before:
        raise CheckFailed(
            "storage must be unchanged after A resave refusal; "
            f"diff keys: {sorted(set(after) ^ set(before))[:8]}"
        )
    if added_layers:
        raise CheckFailed(
            "A added foreign class layers on probe ObjId: "
            + ", ".join(added_layers)
        )
    if after.get(probe_layer) != before.get(probe_layer):
        raise CheckFailed("A changed serialized bytes of unknown probe layer")

    for class_id in forbidden_class_ids:
        foreign_layer = f"{obj_key_prefix}{class_id}/0"
        if foreign_layer in after and foreign_layer not in before:
            raise CheckFailed(
                f"foreign class layer appeared on ObjId: {foreign_layer}"
            )


def assert_b_load_verify(
    *,
    b_code: int,
    record: dict | None,
    scenario: str,
    probe_class_id: int,
) -> None:
    if b_code != 0 or not record or not record.get("ok"):
        raise CheckFailed(
            f"B load-verify failed ({scenario}): "
            f"code={b_code} record={record}"
        )
    if record.get("stage") != "load-verify":
        raise CheckFailed("B load-verify returned unexpected stage")
    if int(record.get("loaded_class_id", -1)) != probe_class_id:
        raise CheckFailed(
            "B loaded unexpected type: "
            f"loaded_class_id={record.get('loaded_class_id')} "
            f"expected={probe_class_id}"
        )
    obj_base = int(record.get("obj_base_class_id", 0))
    class_ids = [int(x) for x in record.get("class_ids", [])]
    if obj_base and obj_base not in class_ids:
        raise CheckFailed("B storage missing ae::Obj base layer after load-verify")

    if scenario == "flat":
        if record.get("scenario") != "flat":
            raise CheckFailed("B expected flat DowngradeProbeEvent scenario")
        if int(record.get("payload", -1)) != 0xA1B2C3D4:
            raise CheckFailed("B flat payload mismatch after LoadRoot")
    elif scenario == "chain":
        if record.get("scenario") != "chain":
            raise CheckFailed("B expected chain NewEvent scenario")
        if int(record.get("payload", -1)) != 0xDEADBEEF:
            raise CheckFailed("B chain payload mismatch after LoadRoot")
        if int(record.get("step", -1)) != 1:
            raise CheckFailed("B chain intermediate step mismatch after LoadRoot")
        if int(record.get("new_event_class_id", -1)) != probe_class_id:
            raise CheckFailed("B chain new_event_class_id mismatch")
    else:
        raise CheckFailed(f"unknown scenario {scenario}")


def assert_b_load_verify_unchanged(*, before: dict, after: dict) -> None:
    keys = (
        "loaded_class_id",
        "payload",
        "step",
        "class_ids",
        "scenario",
        "new_event_class_id",
        "probe_class_id",
    )
    for key in keys:
        if key in before or key in after:
            if before.get(key) != after.get(key):
                raise CheckFailed(
                    f"B load-verify field changed after A attempt: {key} "
                    f"before={before.get(key)!r} after={after.get(key)!r}"
                )
