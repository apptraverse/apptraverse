#!/usr/bin/env python3
"""Bounded JSONL parser/query for App Traverse runtime events."""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any, Iterator

RUNTIME_SCHEMA_VERSION = "apptraverse.runtime_event/1"
RUNTIME_ARTIFACT_PREFIX = "apptraverse-runtime/"
DEFAULT_LIMIT = 50
MAX_LIMIT = 100
ALLOWED_ARTIFACT_CHARS = set(
    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-"
)

ENVELOPE_FIELDS = (
    "schema_version",
    "run_id",
    "seq",
    "event",
    "platform",
    "instance",
    "pid",
    "t_us",
    "mono_us",
    "data",
)


class RuntimeJsonlError(ValueError):
    """Raised when a JSONL line or record is invalid."""


def parse_runtime_artifact_id(artifact_id: str) -> tuple[str, str] | None:
    if not isinstance(artifact_id, str) or not artifact_id:
        return None
    if artifact_id.strip() != artifact_id:
        return None
    if Path(artifact_id).is_absolute():
        return None
    if ".." in artifact_id or "\\" in artifact_id:
        return None
    if not artifact_id.startswith(RUNTIME_ARTIFACT_PREFIX):
        return None
    tail = artifact_id[len(RUNTIME_ARTIFACT_PREFIX) :]
    if not tail or tail.startswith("/"):
        return None
    parts = tail.split("/")
    if len(parts) != 2:
        return None
    run_id, instance = parts
    if not run_id or not instance:
        return None
    if run_id in {".", ".."} or instance in {".", ".."}:
        return None
    if any(ch not in ALLOWED_ARTIFACT_CHARS for ch in run_id + instance):
        return None
    return run_id, instance


def validate_record(record: dict[str, Any]) -> None:
    if not isinstance(record, dict):
        raise RuntimeJsonlError("record must be a JSON object")
    missing = [field for field in ENVELOPE_FIELDS if field not in record]
    if missing:
        raise RuntimeJsonlError(f"missing required envelope field(s): {', '.join(missing)}")
    if record["schema_version"] != RUNTIME_SCHEMA_VERSION:
        raise RuntimeJsonlError(
            f"unsupported schema_version: {record['schema_version']!r}"
        )
    if not isinstance(record["data"], dict):
        raise RuntimeJsonlError("data must be a JSON object")


def iter_records(path: Path) -> Iterator[tuple[int, dict[str, Any]]]:
    with path.open("r", encoding="utf-8") as handle:
        for line_number, raw_line in enumerate(handle, start=1):
            line = raw_line.strip()
            if not line:
                continue
            try:
                loaded = json.loads(line)
            except json.JSONDecodeError as exc:
                raise RuntimeJsonlError(
                    f"malformed JSON at line {line_number}: {exc.msg}"
                ) from exc
            if not isinstance(loaded, dict):
                raise RuntimeJsonlError(
                    f"record at line {line_number} must be a JSON object"
                )
            validate_record(loaded)
            yield line_number, loaded


def query_records(
    path: Path,
    *,
    event: str | None = None,
    after_seq: int | None = None,
    limit: int = DEFAULT_LIMIT,
) -> tuple[list[dict[str, Any]], bool]:
    if limit < 1:
        raise ValueError("limit must be >= 1")
    if limit > MAX_LIMIT:
        raise ValueError(f"limit must be <= {MAX_LIMIT}")
    matched: list[dict[str, Any]] = []
    has_more = False
    for _line_number, record in iter_records(path):
        seq = record.get("seq")
        if after_seq is not None:
            if not isinstance(seq, int) or seq <= after_seq:
                continue
        if event is not None and record.get("event") != event:
            continue
        if len(matched) == limit:
            has_more = True
            break
        matched.append(record)
    return matched, has_more


def resolve_runtime_log_path(root: Path, artifact_id: str) -> Path | None:
    parsed = parse_runtime_artifact_id(artifact_id)
    if parsed is None:
        return None
    run_id, instance = parsed
    base = (root / ".artifacts" / "apptraverse-runtime").resolve()
    log_path = (base / run_id / f"{instance}.jsonl").resolve()
    try:
        log_path.relative_to(base)
    except ValueError:
        return None
    return log_path
