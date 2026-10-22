"""Shared storage invariants for event downgrade matrices."""

from __future__ import annotations

from application_evolution_matrix import CheckFailed, manifest


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


def assert_b_inspect_unchanged(
    *,
    b_code: int,
    b_inspect: dict | None,
    probe_class_id: int,
    before_class_ids: list[int],
) -> None:
    if b_code != 0 or not b_inspect or not b_inspect.get("ok"):
        raise CheckFailed("B inspect failed after A resave")
    class_ids = b_inspect.get("class_ids", [])
    if probe_class_id not in set(class_ids):
        raise CheckFailed("B lost probe class layer after A resave")
    if class_ids != before_class_ids:
        raise CheckFailed(
            "B class layer set changed after A resave: "
            f"before={before_class_ids} after={class_ids}"
        )
