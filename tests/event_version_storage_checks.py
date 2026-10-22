"""Assertions for same-ClassId Event v0/v1 compatibility matrix."""

from __future__ import annotations

import json

from application_evolution_matrix import CheckFailed


def parse_fixture_record(stdout: str) -> dict:
    record = None
    for line in stdout.splitlines():
        try:
            candidate = json.loads(line)
        except ValueError:
            continue
        if isinstance(candidate, dict) and candidate.get("ok"):
            record = candidate
    if record is None:
        raise CheckFailed(f"fixture returned no ok JSON: {stdout!r}")
    return record


def assert_class_ids_match(a_record: dict, b_record: dict) -> None:
    for key in ("document_class_id", "event_class_id"):
        if int(a_record[key]) != int(b_record[key]):
            raise CheckFailed(
                f"ClassId mismatch on {key}: A={a_record[key]} B={b_record[key]}"
            )


def assert_tally(record: dict, expected: int, *, who: str) -> None:
    if int(record.get("tally", -1)) != expected:
        raise CheckFailed(f"{who} tally expected {expected}, got {record.get('tally')}")


def assert_extension(record: dict, expected: int, *, who: str) -> None:
    if "extension" not in record:
        raise CheckFailed(f"{who} missing extension field in fixture output")
    if int(record["extension"]) != expected:
        raise CheckFailed(
            f"{who} extension expected {expected}, got {record.get('extension')}"
        )


def assert_no_extension_field(record: dict, *, who: str) -> None:
    if "extension" in record:
        raise CheckFailed(f"{who} must not report v1 extension field")


def event_versions(manifest: dict, event_id: int, event_class_id: int) -> set[int]:
    prefix = f"{event_id}/{event_class_id}/"
    return {
        int(key[len(prefix) :])
        for key in manifest
        if key.startswith(prefix)
    }


def doc_version_slots(manifest: dict, doc_id: int, doc_class_id: int) -> set[int]:
    prefix = f"{doc_id}/{doc_class_id}/"
    return {
        int(key[len(prefix) :])
        for key in manifest
        if key.startswith(prefix)
    }


def last_event(record: dict) -> dict:
    events = record.get("events") or []
    if not events:
        raise CheckFailed("expected at least one journal event")
    return events[-1]


def assert_journal_len(record: dict, expected: int, *, who: str) -> None:
    if int(record.get("journal_len", -1)) != expected:
        raise CheckFailed(
            f"{who} journal_len expected {expected}, got {record.get('journal_len')}"
        )


def assert_replay(
    record: dict,
    *,
    who: str,
    tally: int,
    journal_len: int,
    extension: int | None = None,
) -> None:
    if record.get("stage") not in (
        "replay-from-base",
        "replay-from-base-fault",
        "replay-from-base-save",
    ):
        raise CheckFailed(f"{who} replay returned unexpected stage: {record.get('stage')}")
    if record.get("replay_ok") is not True:
        raise CheckFailed(f"{who} TryReplayFromBase failed: {record}")
    assert_tally(record, tally, who=who)
    assert_journal_len(record, journal_len, who=who)
    if extension is not None:
        assert_extension(record, extension, who=who)
    if int(record.get("tally_after_first_replay", -1)) != tally:
        raise CheckFailed(f"{who} first replay tally mismatch")
    if int(record.get("tally_after_second_replay", -1)) != tally:
        raise CheckFailed(f"{who} second replay tally mismatch (double apply)")
    if extension is not None:
        if int(record.get("extension_after_first_replay", -1)) != extension:
            raise CheckFailed(f"{who} first replay extension mismatch")
        if int(record.get("extension_after_second_replay", -1)) != extension:
            raise CheckFailed(f"{who} second replay extension mismatch")


def assert_base_v1_probe(record: dict, *, who: str, base_id: int) -> None:
    if int(record.get("base_object_id", -1)) != base_id:
        raise CheckFailed(f"{who} base_object_id mismatch")
    if int(record.get("base_extension", -1)) != 640:
        raise CheckFailed(
            f"{who} base_extension expected 640, got {record.get('base_extension')}"
        )
    if int(record.get("extension", -1)) != 640:
        raise CheckFailed(
            f"{who} document extension expected 640 at probe, got {record.get('extension')}"
        )
