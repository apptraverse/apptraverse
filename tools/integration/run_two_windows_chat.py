#!/usr/bin/env python3
"""Two-Windows chat integration harness using runtime JSONL assertions."""

from __future__ import annotations

import argparse
import json
import os
import secrets
import subprocess
import sys
import time
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Callable, IO

_REPO_ROOT = Path(__file__).resolve().parents[2]
if str(_REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(_REPO_ROOT))

from tools.runners.run_apptraverse_job import terminate_process_tree  # noqa: E402
from tools.runtime.runtime_jsonl import (  # noqa: E402
    RuntimeJsonlError,
    validate_record,
)

RESULT_SCHEMA_VERSION = "apptraverse.integration_result/1"
SCENARIO = "two_windows_bidirectional_chat"
CANONICAL_BUILD_DIR = Path("build") / "win64-vs2022-msvc-debug"
EXPECTED_EXE_NAME = "win32_single_client_chat.exe"
ALICE_TEXT = "message_from_alice"
BOB_TEXT = "message_from_bob"
ALICE_CLIENT = "integration-alice"
BOB_CLIENT = "integration-bob"
DEFAULT_TIMEOUT_SECONDS = 120
MAX_TIMEOUT_SECONDS = 180
STARTUP_TIMEOUT_S = 30.0
DELIVERY_TIMEOUT_S = 90.0
POLL_INTERVAL_S = 0.2
ALICE_INSTANCE = "alice"
BOB_INSTANCE = "bob"


class IntegrationFailure(Exception):
    def __init__(self, failure_kind: str, first_error: str) -> None:
        super().__init__(first_error)
        self.failure_kind = failure_kind
        self.first_error = first_error


def repo_root() -> Path:
    return Path(__file__).resolve().parents[2]


def new_run_id() -> str:
    stamp = datetime.now(timezone.utc).strftime("%Y%m%d-%H%M%S")
    return f"{stamp}-{secrets.token_hex(3)}"


def artifact_id_for(run_id: str) -> str:
    return f"apptraverse-integration/{run_id}"


def utc_now_iso() -> str:
    return datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def atomic_write_text(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_name(path.name + ".tmp")
    tmp.write_text(text, encoding="utf-8")
    tmp.replace(path)


def atomic_write_inbox(path: Path, text: str) -> None:
    atomic_write_text(path, text + "\n")


def atomic_write_json(path: Path, payload: dict) -> None:
    atomic_write_text(path, json.dumps(payload, indent=2) + "\n")


def expected_exe_name() -> str:
    if sys.platform.startswith("win"):
        return EXPECTED_EXE_NAME
    return "win32_single_client_chat"


def validate_exe_exists(exe: Path) -> Path:
    resolved = exe.expanduser()
    if not resolved.is_file():
        raise IntegrationFailure("executable_not_found", f"executable not found: {exe}")
    name = resolved.name.lower()
    expected = expected_exe_name().lower()
    if name != expected:
        raise IntegrationFailure(
            "executable_not_found",
            f"expected {expected_exe_name()}, got {resolved.name}",
        )
    return resolved.resolve()


def canonical_build_root(source_dir: Path) -> Path:
    return (source_dir / CANONICAL_BUILD_DIR).resolve()


def validate_mcp_exe(source_dir: Path, exe: str) -> Path:
    if not isinstance(exe, str) or not exe.strip():
        raise IntegrationFailure("executable_not_found", "exe is required")
    if ".." in Path(exe).parts:
        raise IntegrationFailure("executable_not_found", "path traversal rejected")
    candidate = Path(exe)
    if not candidate.is_absolute():
        candidate = source_dir / candidate
    try:
        resolved = candidate.resolve()
    except OSError as exc:
        raise IntegrationFailure("executable_not_found", str(exc)) from exc
    build_root = canonical_build_root(source_dir)
    try:
        resolved.relative_to(build_root)
    except ValueError as exc:
        raise IntegrationFailure(
            "executable_not_found",
            "exe must remain inside the canonical Windows build tree",
        ) from exc
    return validate_exe_exists(resolved)


def iter_complete_records(path: Path) -> list[dict[str, Any]]:
    if not path.is_file():
        return []
    text = path.read_text(encoding="utf-8", errors="replace")
    if not text:
        return []
    if not text.endswith("\n"):
        if "\n" not in text:
            return []
        text = text[: text.rfind("\n") + 1]
    records: list[dict[str, Any]] = []
    for line_number, raw_line in enumerate(text.splitlines(), start=1):
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
        records.append(loaded)
    return records


def extract_local_uid(path: Path) -> str:
    try:
        records = iter_complete_records(path)
    except RuntimeJsonlError as exc:
        raise IntegrationFailure("invalid_runtime_jsonl", str(exc)) from exc
    started = [record for record in records if record.get("event") == "runtime_started"]
    if not started:
        raise IntegrationFailure("uid_setup_failed", f"missing runtime_started in {path.name}")
    uid = started[0].get("data", {}).get("local_uid")
    if not isinstance(uid, str) or not uid.strip():
        raise IntegrationFailure("uid_setup_failed", "runtime_started.data.local_uid is empty")
    return uid


def require_distinct_uids(alice_uid: str, bob_uid: str) -> None:
    if not alice_uid or not bob_uid:
        raise IntegrationFailure("uid_setup_failed", "UID is empty")
    if alice_uid == bob_uid:
        raise IntegrationFailure("uid_collision", "Alice UID equals Bob UID")


def has_runtime_started(records: list[dict[str, Any]]) -> bool:
    return any(record.get("event") == "runtime_started" for record in records)


def has_accepted_peer_add(records: list[dict[str, Any]], peer_uid: str) -> bool:
    for record in records:
        if record.get("event") != "peer_add":
            continue
        data = record.get("data") or {}
        if data.get("accepted") is True and data.get("peer") == peer_uid:
            return True
    return False


def startup_gate_ready(records: list[dict[str, Any]], peer_uid: str) -> bool:
    return has_runtime_started(records) and has_accepted_peer_add(records, peer_uid)


PROCESS_RUNNING = "running"
PROCESS_EXITED = "exited"
PROCESS_HARNESS_TERMINATED = "harness_terminated"
COMPLETION_HARNESS_TERMINATED = "harness_terminated_after_success"
COMPLETION_NATURAL_EXIT = "natural_exit"


def accepted_text_submit_id(records: list[dict[str, Any]], text: str) -> Any:
    for record in records:
        if record.get("event") != "text_submit":
            continue
        data = record.get("data") or {}
        if data.get("text") == text and data.get("accepted") is True:
            return data.get("event_obj_id")
    return None


def text_submit_event_id(records: list[dict[str, Any]], text: str) -> Any:
    event_id = accepted_text_submit_id(records, text)
    if event_id is None:
        raise IntegrationFailure(
            "assertion_failed", f"missing accepted text_submit for {text!r}"
        )
    return event_id


def presentation_last_entry_event_id(records: list[dict[str, Any]], text: str) -> Any:
    """Last-entry helper kept only to prove it is insufficient for delivery."""
    for record in reversed(records):
        if record.get("event") != "presentation":
            continue
        data = record.get("data") or {}
        if data.get("last_entry_kind") == "message" and data.get("last_entry_text") == text:
            return data.get("last_event_obj_id")
    return None


def message_visible_event_ids(records: list[dict[str, Any]], text: str) -> list[Any]:
    ids: list[Any] = []
    for record in records:
        if record.get("event") != "message_visible":
            continue
        data = record.get("data") or {}
        if data.get("text") != text:
            continue
        event_id = data.get("event_obj_id")
        if event_id is None:
            continue
        ids.append(event_id)
    return ids


def unique_message_visible_id(records: list[dict[str, Any]], text: str) -> tuple[str, Any]:
    ids = message_visible_event_ids(records, text)
    unique: list[Any] = []
    seen: set[str] = set()
    for event_id in ids:
        key = json.dumps(event_id, sort_keys=True) if isinstance(event_id, dict) else str(event_id)
        if key in seen:
            continue
        seen.add(key)
        unique.append(event_id)
    if not unique:
        return "missing", None
    if len(unique) != 1:
        return "duplicate", unique
    return "ok", unique[0]


def remote_event_obj_id(records: list[dict[str, Any]], text: str) -> Any:
    status, event_id = unique_message_visible_id(records, text)
    if status == "duplicate":
        raise IntegrationFailure(
            "duplicate_remote_event",
            f"duplicate Event ObjIds for remote text {text!r}: {event_id!r}",
        )
    if status != "ok" or event_id is None:
        raise IntegrationFailure(
            "assertion_failed",
            f"missing message_visible for remote text {text!r}",
        )
    return event_id


def require_one_runtime_started(records: list[dict[str, Any]]) -> None:
    started = [record for record in records if record.get("event") == "runtime_started"]
    if len(started) != 1:
        raise IntegrationFailure(
            "assertion_failed",
            f"expected one runtime_started, got {len(started)}",
        )


def assert_instance_jsonl(
    records: list[dict[str, Any]],
    *,
    peer_uid: str,
    submitted_text: str,
    remote_text: str,
) -> tuple[Any, Any]:
    require_one_runtime_started(records)
    if not has_accepted_peer_add(records, peer_uid):
        raise IntegrationFailure(
            "assertion_failed", f"missing accepted peer_add for {peer_uid}"
        )
    submitted_id = text_submit_event_id(records, submitted_text)
    remote_id = remote_event_obj_id(records, remote_text)
    return submitted_id, remote_id


def inspect_instance_jsonl(
    records: list[dict[str, Any]],
    *,
    submitted_text: str,
    remote_text: str,
) -> dict[str, Any]:
    submitted_id = accepted_text_submit_id(records, submitted_text)
    status, remote_id = unique_message_visible_id(records, remote_text)
    return {
        "local_text_submit_found": submitted_id is not None,
        "submitted_event_obj_id": submitted_id,
        "remote_message_visible": status == "ok",
        "remote_event_obj_id": remote_id if status == "ok" else None,
        "duplicate_remote": status == "duplicate",
        "runtime_record_count": len(records),
    }


def compact_instance(
    *,
    instance: str,
    pid: int | None,
    exit_code: int | None,
    local_uid: str | None,
    peer_uid: str | None,
    local_text_submit_found: bool,
    submitted_event_obj_id: Any,
    remote_message_visible: bool,
    remote_event_obj_id: Any,
    process_state: str | None,
    process_completion: str | None = None,
    runtime_record_count: int | None = None,
) -> dict[str, Any]:
    payload: dict[str, Any] = {
        "instance": instance,
        "pid": pid,
        "local_uid": local_uid,
        "peer_uid": peer_uid,
        "local_text_submit_found": local_text_submit_found,
        "submitted_event_obj_id": submitted_event_obj_id,
        "remote_message_visible": remote_message_visible,
        "remote_event_obj_id": remote_event_obj_id,
        "process_state": process_state,
        "exit_code": exit_code,
    }
    if process_completion is not None:
        payload["process_completion"] = process_completion
    if runtime_record_count is not None:
        payload["runtime_record_count"] = runtime_record_count
    return payload


def process_state_for(managed: "ManagedProcess | None", *, harness_terminated: bool) -> str | None:
    if managed is None:
        return None
    if harness_terminated:
        return PROCESS_HARNESS_TERMINATED
    if managed.poll() is None:
        return PROCESS_RUNNING
    return PROCESS_EXITED


def summarize_managed(
    managed: "ManagedProcess | None",
    *,
    instance: str,
    local_uid: str | None,
    peer_uid: str | None,
    submitted_text: str,
    remote_text: str,
    harness_terminated: bool = False,
    process_completion: str | None = None,
) -> dict[str, Any] | None:
    if managed is None and local_uid is None:
        return None
    records: list[dict[str, Any]] = []
    if managed is not None and managed.jsonl_path is not None:
        try:
            records = iter_complete_records(managed.jsonl_path)
        except RuntimeJsonlError:
            records = []
    inspected = inspect_instance_jsonl(
        records, submitted_text=submitted_text, remote_text=remote_text
    )
    exit_code = managed.poll() if managed is not None else None
    return compact_instance(
        instance=instance,
        pid=managed.pid if managed is not None else None,
        exit_code=exit_code,
        local_uid=local_uid,
        peer_uid=peer_uid,
        local_text_submit_found=bool(inspected["local_text_submit_found"]),
        submitted_event_obj_id=inspected["submitted_event_obj_id"],
        remote_message_visible=bool(inspected["remote_message_visible"]),
        remote_event_obj_id=inspected["remote_event_obj_id"],
        process_state=process_state_for(managed, harness_terminated=harness_terminated),
        process_completion=process_completion,
        runtime_record_count=inspected["runtime_record_count"],
    )


def compact_result(
    *,
    run_id: str,
    status: str,
    duration_ms: int,
    failure_kind: str | None,
    first_error: str | None,
    instances: list[dict[str, Any]],
) -> dict[str, Any]:
    return {
        "schema_version": RESULT_SCHEMA_VERSION,
        "run_id": run_id,
        "artifact_id": artifact_id_for(run_id),
        "scenario": SCENARIO,
        "status": status,
        "duration_ms": duration_ms,
        "failure_kind": failure_kind,
        "first_error": first_error,
        "instances": instances,
    }


def result_contains_forbidden_payload(payload: dict[str, Any]) -> bool:
    dumped = json.dumps(payload)
    forbidden = ("stdout.log", "stderr.log", "FULL STDOUT", "FULL STDERR")
    return any(item in dumped for item in forbidden)


@dataclass
class ManagedProcess:
    name: str
    proc: Any
    stdout: IO[bytes] | None = None
    stderr: IO[bytes] | None = None
    jsonl_path: Path | None = None

    @property
    def pid(self) -> int | None:
        return getattr(self.proc, "pid", None)

    def poll(self) -> int | None:
        poll = getattr(self.proc, "poll", None)
        if poll is None:
            return None
        return poll()

    def close_logs(self) -> None:
        for handle in (self.stdout, self.stderr):
            if handle is None:
                continue
            try:
                handle.close()
            except OSError:
                pass


def terminate_managed(processes: list[ManagedProcess], terminate=terminate_process_tree) -> None:
    for managed in processes:
        pid = managed.pid
        if pid is None:
            continue
        if managed.poll() is None:
            terminate(int(pid))
        managed.close_logs()


def jsonl_env(
    *,
    base: dict[str, str],
    run_id: str,
    instance: str,
    jsonl_path: Path,
) -> dict[str, str]:
    env = dict(base)
    env["APPTRAVERSE_RUN_ID"] = run_id
    env["APPTRAVERSE_INSTANCE"] = instance
    env["APPTRAVERSE_RUNTIME_JSONL"] = str(jsonl_path)
    return env


def start_child(
    *,
    exe: Path,
    args: list[str],
    env: dict[str, str],
    stdout_path: Path,
    stderr_path: Path,
    cwd: Path,
    name: str,
    jsonl_path: Path,
    popen: Callable[..., Any] = subprocess.Popen,
) -> ManagedProcess:
    stdout_path.parent.mkdir(parents=True, exist_ok=True)
    stderr_path.parent.mkdir(parents=True, exist_ok=True)
    stdout = stdout_path.open("wb")
    stderr = stderr_path.open("wb")
    try:
        proc = popen(
            [str(exe), *args],
            stdout=stdout,
            stderr=stderr,
            env=env,
            cwd=str(cwd),
            shell=False,
        )
    except OSError as exc:
        stdout.close()
        stderr.close()
        raise IntegrationFailure("process_start_failed", str(exc)) from exc
    return ManagedProcess(
        name=name,
        proc=proc,
        stdout=stdout,
        stderr=stderr,
        jsonl_path=jsonl_path,
    )


def wait_process(managed: ManagedProcess, timeout_s: float, sleep=time.sleep) -> int:
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        code = managed.poll()
        if code is not None:
            managed.close_logs()
            return int(code)
        sleep(POLL_INTERVAL_S)
    raise IntegrationFailure(
        "uid_setup_failed", f"{managed.name} did not exit after UID setup"
    )


def wait_startup_gate(
    processes: list[ManagedProcess],
    peer_by_name: dict[str, str],
    *,
    timeout_s: float,
    sleep=time.sleep,
) -> None:
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        for managed in processes:
            if managed.poll() is not None:
                raise IntegrationFailure(
                    "process_exited_before_ready",
                    f"{managed.name} exited before startup gate",
                )
        ready = True
        for managed in processes:
            if managed.jsonl_path is None:
                ready = False
                break
            try:
                records = iter_complete_records(managed.jsonl_path)
            except RuntimeJsonlError as exc:
                raise IntegrationFailure("invalid_runtime_jsonl", str(exc)) from exc
            if not startup_gate_ready(records, peer_by_name[managed.name]):
                ready = False
                break
        if ready:
            return
        sleep(POLL_INTERVAL_S)
    raise IntegrationFailure("startup_timeout", "startup gate not reached")


def wait_delivery_gate(
    alice: ManagedProcess,
    bob: ManagedProcess,
    *,
    timeout_s: float,
    sleep=time.sleep,
) -> tuple[list[dict[str, Any]], list[dict[str, Any]]]:
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        for managed in (alice, bob):
            if managed.poll() is not None:
                raise IntegrationFailure(
                    "process_exited_before_delivery",
                    f"{managed.name} exited before delivery",
                )
        try:
            alice_records = iter_complete_records(alice.jsonl_path) if alice.jsonl_path else []
            bob_records = iter_complete_records(bob.jsonl_path) if bob.jsonl_path else []
        except RuntimeJsonlError as exc:
            raise IntegrationFailure("invalid_runtime_jsonl", str(exc)) from exc
        alice_inspect = inspect_instance_jsonl(
            alice_records, submitted_text=ALICE_TEXT, remote_text=BOB_TEXT
        )
        bob_inspect = inspect_instance_jsonl(
            bob_records, submitted_text=BOB_TEXT, remote_text=ALICE_TEXT
        )
        if alice_inspect["duplicate_remote"] or bob_inspect["duplicate_remote"]:
            raise IntegrationFailure(
                "duplicate_remote_event",
                "duplicate Event ObjIds for an expected remote message",
            )
        if (
            alice_inspect["local_text_submit_found"]
            and bob_inspect["local_text_submit_found"]
            and alice_inspect["remote_message_visible"]
            and bob_inspect["remote_message_visible"]
        ):
            return alice_records, bob_records
        sleep(POLL_INTERVAL_S)
    raise IntegrationFailure(
        "message_delivery_timeout",
        "delivery gate not reached: both sides need local text_submit and remote message_visible",
    )


def run_two_windows_chat(
    *,
    source_dir: Path,
    exe: Path,
    timeout_seconds: int = DEFAULT_TIMEOUT_SECONDS,
    startup_timeout_s: float = STARTUP_TIMEOUT_S,
    delivery_timeout_s: float = DELIVERY_TIMEOUT_S,
    popen: Callable[..., Any] = subprocess.Popen,
    terminate=terminate_process_tree,
    sleep=time.sleep,
    run_id: str | None = None,
) -> dict[str, Any]:
    started = time.monotonic()
    run_id = run_id or new_run_id()
    artifact_dir = source_dir / ".artifacts" / "apptraverse-integration" / run_id
    artifact_dir.mkdir(parents=True, exist_ok=True)
    instances: list[dict[str, Any]] = []
    alice_uid = None
    bob_uid = None
    alice: ManagedProcess | None = None
    bob: ManagedProcess | None = None
    harness_terminated = False

    def duration_ms() -> int:
        return int((time.monotonic() - started) * 1000)

    def snapshot_instances(*, process_completion: str | None = None) -> list[dict[str, Any]]:
        summaries: list[dict[str, Any]] = []
        alice_summary = summarize_managed(
            alice,
            instance=ALICE_INSTANCE,
            local_uid=alice_uid,
            peer_uid=bob_uid,
            submitted_text=ALICE_TEXT,
            remote_text=BOB_TEXT,
            harness_terminated=harness_terminated,
            process_completion=process_completion,
        )
        bob_summary = summarize_managed(
            bob,
            instance=BOB_INSTANCE,
            local_uid=bob_uid,
            peer_uid=alice_uid,
            submitted_text=BOB_TEXT,
            remote_text=ALICE_TEXT,
            harness_terminated=harness_terminated,
            process_completion=process_completion,
        )
        if alice_summary is not None:
            summaries.append(alice_summary)
        if bob_summary is not None:
            summaries.append(bob_summary)
        return summaries

    def fail(kind: str, message: str) -> dict[str, Any]:
        summaries = snapshot_instances()
        terminate_managed([item for item in (alice, bob) if item is not None], terminate)
        result = compact_result(
            run_id=run_id,
            status="failed",
            duration_ms=duration_ms(),
            failure_kind=kind,
            first_error=message,
            instances=summaries,
        )
        atomic_write_json(artifact_dir / "result.json", result)
        return result

    try:
        if not isinstance(timeout_seconds, int) or timeout_seconds < 1:
            raise IntegrationFailure("assertion_failed", "invalid timeout_seconds")
        if timeout_seconds > MAX_TIMEOUT_SECONDS:
            raise IntegrationFailure("assertion_failed", "timeout_seconds exceeds 180")
        exe_path = validate_exe_exists(Path(exe))
        remaining = float(timeout_seconds)
        setup_timeout = min(45.0, remaining / 3)

        alice_dir = artifact_dir / "alice"
        bob_dir = artifact_dir / "bob"
        setup_dir = artifact_dir / "setup"
        alice_state = alice_dir / "state"
        bob_state = bob_dir / "state"
        alice_state.mkdir(parents=True, exist_ok=True)
        bob_state.mkdir(parents=True, exist_ok=True)
        setup_dir.mkdir(parents=True, exist_ok=True)

        base_env = os.environ.copy()
        alice_setup_jsonl = setup_dir / "alice_uid.jsonl"
        bob_setup_jsonl = setup_dir / "bob_uid.jsonl"

        alice_setup = start_child(
            exe=exe_path,
            args=[
                "--state-dir",
                str(alice_state),
                "--aether-client-name",
                ALICE_CLIENT,
                "--print-aether-uid",
            ],
            env=jsonl_env(
                base=base_env,
                run_id=run_id,
                instance="alice-setup",
                jsonl_path=alice_setup_jsonl,
            ),
            stdout_path=setup_dir / "alice_stdout.log",
            stderr_path=setup_dir / "alice_stderr.log",
            cwd=source_dir,
            name="alice-setup",
            jsonl_path=alice_setup_jsonl,
            popen=popen,
        )
        alice_setup_code = wait_process(alice_setup, setup_timeout, sleep=sleep)
        if alice_setup_code != 0:
            raise IntegrationFailure(
                "uid_setup_failed", f"alice UID setup exit {alice_setup_code}"
            )
        alice_uid = extract_local_uid(alice_setup_jsonl)

        bob_setup = start_child(
            exe=exe_path,
            args=[
                "--state-dir",
                str(bob_state),
                "--aether-client-name",
                BOB_CLIENT,
                "--print-aether-uid",
            ],
            env=jsonl_env(
                base=base_env,
                run_id=run_id,
                instance="bob-setup",
                jsonl_path=bob_setup_jsonl,
            ),
            stdout_path=setup_dir / "bob_stdout.log",
            stderr_path=setup_dir / "bob_stderr.log",
            cwd=source_dir,
            name="bob-setup",
            jsonl_path=bob_setup_jsonl,
            popen=popen,
        )
        bob_setup_code = wait_process(bob_setup, setup_timeout, sleep=sleep)
        if bob_setup_code != 0:
            raise IntegrationFailure(
                "uid_setup_failed", f"bob UID setup exit {bob_setup_code}"
            )
        bob_uid = extract_local_uid(bob_setup_jsonl)
        require_distinct_uids(alice_uid, bob_uid)

        alice_jsonl = alice_dir / "runtime.jsonl"
        bob_jsonl = bob_dir / "runtime.jsonl"
        alice_inbox = alice_dir / "commit.inbox"
        bob_inbox = bob_dir / "commit.inbox"

        alice = start_child(
            exe=exe_path,
            args=[
                "--state-dir",
                str(alice_state),
                "--aether-client-name",
                ALICE_CLIENT,
                "--peer",
                bob_uid,
                "--commit-inbox",
                str(alice_inbox),
            ],
            env=jsonl_env(
                base=base_env,
                run_id=run_id,
                instance=ALICE_INSTANCE,
                jsonl_path=alice_jsonl,
            ),
            stdout_path=alice_dir / "stdout.log",
            stderr_path=alice_dir / "stderr.log",
            cwd=source_dir,
            name=ALICE_INSTANCE,
            jsonl_path=alice_jsonl,
            popen=popen,
        )
        bob = start_child(
            exe=exe_path,
            args=[
                "--state-dir",
                str(bob_state),
                "--aether-client-name",
                BOB_CLIENT,
                "--peer",
                alice_uid,
                "--commit-inbox",
                str(bob_inbox),
            ],
            env=jsonl_env(
                base=base_env,
                run_id=run_id,
                instance=BOB_INSTANCE,
                jsonl_path=bob_jsonl,
            ),
            stdout_path=bob_dir / "stdout.log",
            stderr_path=bob_dir / "stderr.log",
            cwd=source_dir,
            name=BOB_INSTANCE,
            jsonl_path=bob_jsonl,
            popen=popen,
        )
        wait_startup_gate(
            [alice, bob],
            {ALICE_INSTANCE: bob_uid, BOB_INSTANCE: alice_uid},
            timeout_s=startup_timeout_s,
            sleep=sleep,
        )
        atomic_write_inbox(alice_inbox, ALICE_TEXT)
        atomic_write_inbox(bob_inbox, BOB_TEXT)
        alice_records, bob_records = wait_delivery_gate(
            alice, bob, timeout_s=delivery_timeout_s, sleep=sleep
        )
        alice_submitted, alice_remote = assert_instance_jsonl(
            alice_records,
            peer_uid=bob_uid,
            submitted_text=ALICE_TEXT,
            remote_text=BOB_TEXT,
        )
        bob_submitted, bob_remote = assert_instance_jsonl(
            bob_records,
            peer_uid=alice_uid,
            submitted_text=BOB_TEXT,
            remote_text=ALICE_TEXT,
        )
        terminate_managed([alice, bob], terminate)
        harness_terminated = True
        instances = snapshot_instances(
            process_completion=COMPLETION_HARNESS_TERMINATED
        )
        result = compact_result(
            run_id=run_id,
            status="ok",
            duration_ms=duration_ms(),
            failure_kind=None,
            first_error=None,
            instances=instances,
        )
        if result_contains_forbidden_payload(result):
            raise IntegrationFailure("assertion_failed", "compact result contained logs")
        atomic_write_json(artifact_dir / "result.json", result)
        return result
    except IntegrationFailure as exc:
        return fail(exc.failure_kind, exc.first_error)
    except RuntimeJsonlError as exc:
        return fail("invalid_runtime_jsonl", str(exc))
    except Exception as exc:  # noqa: BLE001
        return fail("assertion_failed", str(exc))


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Run two-Windows chat integration")
    parser.add_argument("--exe", required=True, help="Path to win32_single_client_chat.exe")
    parser.add_argument(
        "--timeout-seconds",
        type=int,
        default=DEFAULT_TIMEOUT_SECONDS,
        help="Overall scenario timeout (max 180)",
    )
    parser.add_argument(
        "--source-dir",
        default=str(repo_root()),
        help="Repository root",
    )
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    result = run_two_windows_chat(
        source_dir=Path(args.source_dir),
        exe=Path(args.exe),
        timeout_seconds=args.timeout_seconds,
    )
    sys.stdout.write(json.dumps(result) + "\n")
    return 0 if result.get("status") == "ok" else 1


if __name__ == "__main__":
    raise SystemExit(main())
