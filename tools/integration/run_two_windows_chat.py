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


def text_submit_event_id(records: list[dict[str, Any]], text: str) -> Any:
    matches = [
        record
        for record in records
        if record.get("event") == "text_submit"
        and (record.get("data") or {}).get("text") == text
        and (record.get("data") or {}).get("accepted") is True
    ]
    if not matches:
        raise IntegrationFailure(
            "assertion_failed", f"missing accepted text_submit for {text!r}"
        )
    return matches[0].get("data", {}).get("event_obj_id")


def remote_event_obj_id(records: list[dict[str, Any]], text: str) -> Any:
    ids: list[Any] = []
    for record in records:
        if record.get("event") != "presentation":
            continue
        data = record.get("data") or {}
        if data.get("last_entry_kind") != "message":
            continue
        if data.get("last_entry_text") != text:
            continue
        event_id = data.get("last_event_obj_id")
        if event_id is None:
            continue
        ids.append(event_id)
    unique = {json.dumps(item, sort_keys=True) if isinstance(item, dict) else item for item in ids}
    if len(unique) != 1:
        raise IntegrationFailure(
            "assertion_failed",
            f"expected exactly one unique Event ObjId for remote text {text!r}, got {sorted(unique)!r}",
        )
    return ids[0]


def require_one_runtime_started(records: list[dict[str, Any]]) -> None:
    started = [record for record in records if record.get("event") == "runtime_started"]
    if len(started) != 1:
        raise IntegrationFailure(
            "assertion_failed",
            f"expected one runtime_started, got {len(started)}",
        )


def require_runtime_stopped(records: list[dict[str, Any]]) -> None:
    stopped = [record for record in records if record.get("event") == "runtime_stopped"]
    if not stopped:
        raise IntegrationFailure("assertion_failed", "missing runtime_stopped")


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
    require_runtime_stopped(records)
    return submitted_id, remote_id


def compact_instance(
    *,
    instance: str,
    pid: int | None,
    exit_code: int | None,
    local_uid: str | None,
    peer_uid: str | None,
    submitted_text: str,
    submitted_event_obj_id: Any,
    remote_text: str,
    remote_event_obj_id: Any,
    runtime_record_count: int,
) -> dict[str, Any]:
    return {
        "instance": instance,
        "pid": pid,
        "exit_code": exit_code,
        "local_uid": local_uid,
        "peer_uid": peer_uid,
        "submitted_text": submitted_text,
        "submitted_event_obj_id": submitted_event_obj_id,
        "remote_text": remote_text,
        "remote_event_obj_id": remote_event_obj_id,
        "runtime_record_count": runtime_record_count,
    }


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


def wait_both_exit(
    processes: list[ManagedProcess],
    *,
    timeout_s: float,
    sleep=time.sleep,
) -> dict[str, int]:
    deadline = time.monotonic() + timeout_s
    codes: dict[str, int] = {}
    while time.monotonic() < deadline:
        for managed in processes:
            if managed.name in codes:
                continue
            code = managed.poll()
            if code is not None:
                managed.close_logs()
                codes[managed.name] = int(code)
        if len(codes) == len(processes):
            return codes
        sleep(POLL_INTERVAL_S)
    raise IntegrationFailure(
        "message_delivery_timeout", "processes did not exit after message injection"
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

    def duration_ms() -> int:
        return int((time.monotonic() - started) * 1000)

    def fail(kind: str, message: str) -> dict[str, Any]:
        terminate_managed([item for item in (alice, bob) if item is not None], terminate)
        result = compact_result(
            run_id=run_id,
            status="failed",
            duration_ms=duration_ms(),
            failure_kind=kind,
            first_error=message,
            instances=instances,
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
                "--auto-accept-peer",
                "--commit-inbox",
                str(alice_inbox),
                "--wait-for-message",
                BOB_TEXT,
                "--exit-after-message",
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
                "--auto-accept-peer",
                "--commit-inbox",
                str(bob_inbox),
                "--wait-for-message",
                ALICE_TEXT,
                "--exit-after-message",
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
        codes = wait_both_exit([alice, bob], timeout_s=delivery_timeout_s, sleep=sleep)
        alice_records = iter_complete_records(alice_jsonl)
        bob_records = iter_complete_records(bob_jsonl)
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
        if codes.get(ALICE_INSTANCE) != 0 or codes.get(BOB_INSTANCE) != 0:
            raise IntegrationFailure(
                "assertion_failed",
                f"non-zero exit alice={codes.get(ALICE_INSTANCE)} bob={codes.get(BOB_INSTANCE)}",
            )
        instances = [
            compact_instance(
                instance=ALICE_INSTANCE,
                pid=alice.pid,
                exit_code=codes.get(ALICE_INSTANCE),
                local_uid=alice_uid,
                peer_uid=bob_uid,
                submitted_text=ALICE_TEXT,
                submitted_event_obj_id=alice_submitted,
                remote_text=BOB_TEXT,
                remote_event_obj_id=alice_remote,
                runtime_record_count=len(alice_records),
            ),
            compact_instance(
                instance=BOB_INSTANCE,
                pid=bob.pid,
                exit_code=codes.get(BOB_INSTANCE),
                local_uid=bob_uid,
                peer_uid=alice_uid,
                submitted_text=BOB_TEXT,
                submitted_event_obj_id=bob_submitted,
                remote_text=ALICE_TEXT,
                remote_event_obj_id=bob_remote,
                runtime_record_count=len(bob_records),
            ),
        ]
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
