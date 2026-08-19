#!/usr/bin/env python3
"""Windows <-> Android restart and persistence validation. Stdlib only."""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Callable, IO

_REPO_ROOT = Path(__file__).resolve().parents[2]
if str(_REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(_REPO_ROOT))

from tools.integration import run_windows_android_live_chat as live  # noqa: E402
from tools.runners.run_apptraverse_job import terminate_process_tree  # noqa: E402

PersistenceFailure = live.LiveChatFailure

RESULT_SCHEMA_VERSION = "apptraverse.windows_android_persistence_result/1"
ARTIFACT_PREFIX = "windows-android-persistence"
PACKAGE = live.PACKAGE
MAIN_ACTIVITY = live.MAIN_ACTIVITY
TRANSCRIPT_ID = live.TRANSCRIPT_ID
MESSAGE_INPUT_ID = live.MESSAGE_INPUT_ID
SEND_ID = live.SEND_ID
VERBOSE_PROPERTY = live.VERBOSE_PROPERTY
READY_TIMEOUT_S = live.READY_TIMEOUT_S
STOP_TIMEOUT_S = 20.0
ANDROID_COMMIT_RE = re.compile(
    r"CHAT_MESSAGE_COMMITTED\s+platform=android\s+event=(\S+).*?text_key=(\S+)"
)
ANDROID_MESSAGE_COMMITTED_RE = re.compile(r"MESSAGE_COMMITTED\s+text=(\S+)")


def artifact_id_for(run_id: str) -> str:
    return f"{ARTIFACT_PREFIX}/{run_id}"


def windows_launch_argv(
    exe: Path,
    *,
    state_dir: Path,
    client_name: str,
    inbox: Path,
    peer: str | None = None,
    auto_accept: bool = False,
) -> list[str]:
    argv = [
        str(exe),
        "--state-dir",
        str(state_dir),
        "--aether-client-name",
        client_name,
        "--commit-inbox",
        str(inbox),
    ]
    if peer is not None:
        argv.extend(["--peer", peer])
    if auto_accept:
        argv.append("--auto-accept-peer")
    return argv


def require_uid_stable(phase1: str, phase2: str, *, side: str) -> None:
    if not phase1 or not phase2:
        raise PersistenceFailure(f"{side}_uid_changed", f"{side} UID missing across restart")
    if phase1 != phase2:
        raise PersistenceFailure(
            f"{side}_uid_changed",
            f"{side} UID changed: {phase1} -> {phase2}",
        )


def require_pid_changed(phase1: int | None, phase2: int | None, *, side: str) -> None:
    if phase1 is None or phase2 is None:
        raise PersistenceFailure(
            f"{side}_pid_not_changed",
            f"{side} PID missing across restart",
        )
    if phase1 == phase2:
        raise PersistenceFailure(
            f"{side}_pid_not_changed",
            f"{side} PID did not change: {phase1}",
        )


def classify_android_history(transcript: str, messages: list[str]) -> dict[str, int]:
    counts: dict[str, int] = {}
    for message in messages:
        count = live.count_transcript_message(transcript, message)
        counts[message] = count
        if count == 0:
            raise PersistenceFailure(
                "android_history_missing_after_restart",
                f"missing persisted Android message {message!r}",
            )
        if count > 1:
            raise PersistenceFailure(
                "duplicate_persisted_message",
                f"persisted Android message appeared {count} times: {message!r}",
            )
    return counts


def persisted_windows_event_ids(records: list[dict[str, Any]], messages: list[str]) -> dict[str, Any]:
    found: dict[str, Any] = {}
    for message in messages:
        try:
            event_id = live.unique_message_visible_id(records, message)
        except live.LiveChatFailure as exc:
            if exc.failure_kind == "duplicate_message":
                raise PersistenceFailure(
                    "duplicate_persisted_message",
                    f"duplicate Windows Event ObjId for {message!r}",
                ) from exc
            raise
        if event_id is None:
            raise PersistenceFailure(
                "windows_history_missing_after_restart",
                f"missing Windows persisted message_visible for {message!r}",
            )
        found[message] = event_id
    return found


def require_event_ids_unchanged(phase1: dict[str, Any], phase2: dict[str, Any]) -> None:
    for message, first_id in phase1.items():
        second_id = phase2.get(message)
        if first_id is not None and second_id is not None and first_id != second_id:
            raise PersistenceFailure(
                "windows_history_missing_after_restart",
                f"Event ObjId changed across restart for {message!r}",
            )


def parse_android_commit_event_id(log_text: str, message: str) -> Any:
    for match in ANDROID_COMMIT_RE.finditer(log_text):
        if match.group(2) == message:
            raw = match.group(1).rstrip(",;")
            try:
                return int(raw)
            except ValueError:
                return raw
    return None


def android_message_committed(log_text: str, message: str) -> bool:
    marker = f"MESSAGE_COMMITTED text={message}"
    return marker in log_text


def compact_persistence_result(
    *,
    run_id: str,
    status: str,
    duration_ms: int,
    failure_kind: str | None,
    first_error: str | None,
    android: dict[str, Any] | None,
    windows: dict[str, Any] | None,
    phase1: dict[str, Any] | None,
    phase2: dict[str, Any] | None,
    cleanup: dict[str, Any] | None,
) -> dict[str, Any]:
    payload = {
        "schema_version": RESULT_SCHEMA_VERSION,
        "run_id": run_id,
        "artifact_id": artifact_id_for(run_id),
        "status": status,
        "duration_ms": duration_ms,
        "failure_kind": failure_kind,
        "first_error": live.sanitize_error(first_error) if first_error else first_error,
        "android": android,
        "windows": windows,
        "phase1": phase1,
        "phase2": phase2,
        "cleanup": cleanup,
    }
    if live.result_contains_forbidden_payload(payload):
        raise PersistenceFailure("scenario_timeout", "compact result contained logs")
    return payload


def pid_running(pid: int | None) -> bool:
    if pid is None:
        return False
    if sys.platform.startswith("win"):
        completed = subprocess.run(
            ["tasklist.exe", "/NH", "/FI", f"PID eq {pid}"],
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            shell=False,
        )
        out = completed.stdout or ""
        if "No tasks" in out:
            return False
        return str(pid) in out
    try:
        os.kill(pid, 0)
    except OSError:
        return False
    return True


def android_pid_of(adb: live.AdbClient) -> int | None:
    text = adb.shell_text(["pidof", PACKAGE])
    parts = text.split()
    if not parts:
        return None
    try:
        return int(parts[0])
    except ValueError:
        return None


@dataclass
class PersistenceState:
    serial: str = ""
    android_uid_phase1: str | None = None
    android_uid_phase2: str | None = None
    android_pid_phase1: int | None = None
    android_pid_phase2: int | None = None
    windows_uid_phase1: str | None = None
    windows_uid_phase2: str | None = None
    windows_pid_phase1: int | None = None
    windows_pid_phase2: int | None = None
    windows_phase1_stopped: bool = False
    android_phase1_stopped: bool = False
    phase2_peer_argument_used: bool = False
    verbose_restored: bool = False
    app_data_preserved: bool = True
    command_log: list[list[str]] = field(default_factory=list)
    dump_state: live.ScenarioState = field(default_factory=live.ScenarioState)
    phase1: dict[str, Any] = field(default_factory=dict)
    phase2: dict[str, Any] = field(default_factory=dict)
    pre_w_to_a: str = ""
    pre_a_to_w: str = ""
    post_w_to_a: str = ""
    post_a_to_w: str = ""
    client_name: str = ""


def snapshot(state: PersistenceState) -> tuple[dict[str, Any], dict[str, Any], dict[str, Any]]:
    android = {
        "serial": state.serial,
        "uid_phase1": state.android_uid_phase1,
        "uid_phase2": state.android_uid_phase2,
        "pid_phase1": state.android_pid_phase1,
        "pid_phase2": state.android_pid_phase2,
        "uid_stable": bool(
            state.android_uid_phase1
            and state.android_uid_phase1 == state.android_uid_phase2
        ),
        "pid_changed": bool(
            state.android_pid_phase1
            and state.android_pid_phase2
            and state.android_pid_phase1 != state.android_pid_phase2
        ),
        "phase1_history_count": state.phase1.get("history_count"),
        "phase2_history_count": state.phase2.get("history_count"),
    }
    windows = {
        "uid_phase1": state.windows_uid_phase1,
        "uid_phase2": state.windows_uid_phase2,
        "pid_phase1": state.windows_pid_phase1,
        "pid_phase2": state.windows_pid_phase2,
        "uid_stable": bool(
            state.windows_uid_phase1
            and state.windows_uid_phase1 == state.windows_uid_phase2
        ),
        "pid_changed": bool(
            state.windows_pid_phase1
            and state.windows_pid_phase2
            and state.windows_pid_phase1 != state.windows_pid_phase2
        ),
        "phase2_peer_argument_used": state.phase2_peer_argument_used,
    }
    cleanup = {
        "verbose_property_restored": state.verbose_restored,
        "app_data_preserved": state.app_data_preserved,
        "windows_phase1_stopped": state.windows_phase1_stopped,
        "android_phase1_stopped": state.android_phase1_stopped,
    }
    return android, windows, cleanup


def dump_transcript(
    adb: live.AdbClient,
    *,
    run_id: str,
    android_root: Path,
    logical_name: str,
    sleep: Callable[[float], None],
    monotonic: Callable[[], float],
    state: PersistenceState,
) -> str:
    live.ensure_main_activity_foreground(
        adb, state.dump_state, sleep=sleep, monotonic=monotonic
    )
    xml_text = live.dump_ui_hierarchy(
        adb,
        run_id=run_id,
        logical_name=logical_name,
        android_dir=android_root,
        focused_window=state.dump_state.focused_window or "",
        sleep=sleep,
        state=state.dump_state,
    )
    return live.extract_control_text(
        xml_text, TRANSCRIPT_ID, focused_window=state.dump_state.focused_window or ""
    )


def send_android_message(
    adb: live.AdbClient,
    *,
    run_id: str,
    android_root: Path,
    logical_name: str,
    message: str,
    sleep: Callable[[float], None],
    monotonic: Callable[[], float],
    state: PersistenceState,
) -> str:
    live.ensure_main_activity_foreground(
        adb, state.dump_state, sleep=sleep, monotonic=monotonic
    )
    xml_text = live.dump_ui_hierarchy(
        adb,
        run_id=run_id,
        logical_name=logical_name,
        android_dir=android_root,
        focused_window=state.dump_state.focused_window or "",
        sleep=sleep,
        state=state.dump_state,
    )
    tap_x, tap_y = live.extract_control_center(
        xml_text, MESSAGE_INPUT_ID, focused_window=state.dump_state.focused_window or ""
    )
    adb.shell(["input", "tap", str(tap_x), str(tap_y)])
    adb.shell(["input", "keyevent", "123"])
    for _ in range(24):
        adb.shell(["input", "keyevent", "67"])
    adb.shell(["input", "text", live.encode_adb_input_text(message)])
    typed_xml = live.dump_ui_hierarchy(
        adb,
        run_id=run_id,
        logical_name=logical_name,
        android_dir=android_root,
        focused_window=state.dump_state.focused_window or "",
        sleep=sleep,
        state=state.dump_state,
    )
    typed = live.extract_control_text(
        typed_xml, MESSAGE_INPUT_ID, focused_window=state.dump_state.focused_window or ""
    )
    if typed != message:
        raise PersistenceFailure(
            "phase2_delivery_failed" if "post_" in message else "phase1_delivery_failed",
            f"message_input text {typed!r} != {message!r}",
        )
    send_x, send_y = live.extract_control_center(
        typed_xml, SEND_ID, focused_window=state.dump_state.focused_window or ""
    )
    adb.shell(["input", "tap", str(send_x), str(send_y)])
    return typed_xml


def start_windows(
    *,
    exe: Path,
    argv: list[str],
    env: dict[str, str],
    stdout_path: Path,
    stderr_path: Path,
    cwd: Path,
    popen: Callable[..., Any],
) -> tuple[Any, IO[bytes], IO[bytes]]:
    forbidden = live.argv_contains_forbidden(argv)
    if forbidden:
        raise PersistenceFailure("scenario_timeout", f"forbidden command: {forbidden}")
    stdout_path.parent.mkdir(parents=True, exist_ok=True)
    stderr_path.parent.mkdir(parents=True, exist_ok=True)
    stdout = stdout_path.open("wb")
    stderr = stderr_path.open("wb")
    try:
        proc = popen(
            argv,
            stdout=stdout,
            stderr=stderr,
            env=env,
            cwd=str(cwd),
            shell=False,
        )
    except OSError as exc:
        stdout.close()
        stderr.close()
        raise PersistenceFailure("process_exited_early", str(exc)) from exc
    return proc, stdout, stderr


def run_windows_android_persistence(
    *,
    source_dir: Path,
    windows_exe: Path,
    serial: str,
    timeout_s: float = READY_TIMEOUT_S,
    run_id: str | None = None,
    adb_path: Path | None = None,
    popen: Callable[..., Any] = subprocess.Popen,
    run: Callable[..., subprocess.CompletedProcess[str]] = subprocess.run,
    sleep: Callable[[float], None] = time.sleep,
    monotonic: Callable[[], float] = time.monotonic,
    terminate: Callable[[int], None] = terminate_process_tree,
    pid_is_running: Callable[[int | None], bool] = pid_running,
    env: dict[str, str] | None = None,
) -> dict[str, Any]:
    started = monotonic()
    run_id = run_id or live.new_run_id()
    short_id = live.short_run_id(run_id)
    artifact_dir = source_dir / ".artifacts" / ARTIFACT_PREFIX / run_id
    windows_dir = artifact_dir / "windows"
    android_root = artifact_dir / "android"
    phase1_win = windows_dir / "phase1"
    phase2_win = windows_dir / "phase2"
    phase1_and = android_root / "phase1"
    phase2_and = android_root / "phase2"
    shared_state = windows_dir / "state"
    for path in (phase1_win, phase2_win, phase1_and, phase2_and, shared_state):
        path.mkdir(parents=True, exist_ok=True)

    state = PersistenceState(serial=serial)
    state.client_name = f"windows-android-persist-{short_id}"
    state.pre_w_to_a = f"pre_w_to_a_{short_id}"
    state.pre_a_to_w = f"pre_a_to_w_{short_id}"
    state.post_w_to_a = f"post_w_to_a_{short_id}"
    state.post_a_to_w = f"post_a_to_w_{short_id}"
    adb: live.AdbClient | None = None
    windows_proc: Any = None
    logcat_proc: Any = None
    windows_stdout: IO[bytes] | None = None
    windows_stderr: IO[bytes] | None = None

    def duration_ms() -> int:
        return int((monotonic() - started) * 1000)

    def fail(kind: str, message: str) -> dict[str, Any]:
        android, windows, cleanup = snapshot(state)
        result = compact_persistence_result(
            run_id=run_id,
            status="failed",
            duration_ms=duration_ms(),
            failure_kind=kind,
            first_error=message,
            android=android,
            windows=windows,
            phase1=state.phase1 or None,
            phase2=state.phase2 or None,
            cleanup=cleanup,
        )
        live.atomic_write_json(artifact_dir / "result.json", result)
        return result

    def restore_verbose() -> None:
        if adb is None:
            return
        try:
            adb.shell(["setprop", VERBOSE_PROPERTY, "0"], timeout=15.0)
            current = adb.shell_text(["getprop", VERBOSE_PROPERTY], timeout=15.0)
            state.verbose_restored = current.strip() in {"", "0"}
        except Exception:  # noqa: BLE001
            state.verbose_restored = False

    def stop_logcat() -> None:
        nonlocal logcat_proc
        if logcat_proc is None:
            return
        pid = getattr(logcat_proc, "pid", None)
        poll = getattr(logcat_proc, "poll", None)
        if pid and (not callable(poll) or poll() is None):
            terminate(int(pid))
        live.close_proc_log(logcat_proc)
        logcat_proc = None

    def stop_windows() -> None:
        nonlocal windows_proc, windows_stdout, windows_stderr
        if windows_proc is not None:
            pid = getattr(windows_proc, "pid", None)
            poll = getattr(windows_proc, "poll", None)
            if pid and (not callable(poll) or poll() is None):
                terminate(int(pid))
        if windows_stdout is not None:
            try:
                windows_stdout.close()
            except OSError:
                pass
            windows_stdout = None
        if windows_stderr is not None:
            try:
                windows_stderr.close()
            except OSError:
                pass
            windows_stderr = None
        windows_proc = None

    def cleanup() -> None:
        stop_windows()
        stop_logcat()
        if adb is not None:
            try:
                adb.shell(["am", "force-stop", PACKAGE], timeout=15.0)
            except Exception:  # noqa: BLE001
                pass
        restore_verbose()

    def wait_android_ready(logcat_path: Path, *, uid_holder: str) -> tuple[str, int]:
        def ready() -> bool:
            text = logcat_path.read_text(encoding="utf-8", errors="replace") if logcat_path.exists() else ""
            if live.has_fatal_android_error(text):
                raise PersistenceFailure("fatal_android_error", "fatal Android error during startup")
            uid = live.parse_android_uid(text)
            pid = android_pid_of(adb) if adb is not None else None
            dumpsys = adb.shell_text(["dumpsys", "activity", "activities"]) if adb is not None else ""
            state.dump_state.activity_resumed = live.activity_resumed(dumpsys)
            if uid_holder == "phase1":
                if uid:
                    state.android_uid_phase1 = uid
                if pid:
                    state.android_pid_phase1 = pid
                return bool(state.android_uid_phase1 and state.android_pid_phase1 and "CHAT_SYNC_CONTROLLER_READY" in text and state.dump_state.activity_resumed)
            if uid:
                state.android_uid_phase2 = uid
            if pid:
                state.android_pid_phase2 = pid
            return bool(state.android_uid_phase2 and state.android_pid_phase2 and "CHAT_SYNC_CONTROLLER_READY" in text and state.dump_state.activity_resumed)

        live.wait_until(
            ready,
            timeout_s=timeout_s,
            failure_kind="phase2_reconnect_timeout" if uid_holder == "phase2" else "phase1_pairing_timeout",
            message="Android runtime not ready",
            sleep=sleep,
            monotonic=monotonic,
        )
        if uid_holder == "phase1":
            return str(state.android_uid_phase1), int(state.android_pid_phase1 or 0)
        return str(state.android_uid_phase2), int(state.android_pid_phase2 or 0)

    try:
        exe_path = Path(windows_exe)
        if not exe_path.is_absolute():
            exe_path = source_dir / exe_path
        exe_path = live.validate_windows_exe(exe_path)
        adb_exe = adb_path or live.find_adb()
        adb = live.AdbClient(
            adb_exe,
            serial,
            run=run,
            popen=popen,
            command_log=state.command_log,
        )
        live.select_android_device(live.parse_adb_devices(adb.run(["devices"]).stdout), serial)
        abi = adb.shell_text(["getprop", "ro.product.cpu.abi"])
        live.require_abi(abi)
        pkg = adb.shell_text(["pm", "path", PACKAGE])
        if "package:" not in pkg:
            raise PersistenceFailure("android_package_missing", f"{PACKAGE} is not installed")

        adb.shell(["setprop", VERBOSE_PROPERTY, "1"])
        adb.run(["logcat", "-c"])
        adb.shell(["am", "force-stop", PACKAGE])
        logcat_proc = adb.start_logcat(phase1_and / "logcat.txt")
        adb.shell(["am", "start", "-n", MAIN_ACTIVITY])
        wait_android_ready(phase1_and / "logcat.txt", uid_holder="phase1")

        phase1_jsonl = phase1_win / "runtime.jsonl"
        phase1_inbox = phase1_win / "commit.inbox"
        phase1_argv = windows_launch_argv(
            exe_path,
            state_dir=shared_state,
            client_name=state.client_name,
            inbox=phase1_inbox,
            peer=state.android_uid_phase1,
            auto_accept=True,
        )
        state.command_log.append(phase1_argv)
        child_env = live.windows_env(
            env if env is not None else os.environ.copy(),
            run_id=run_id,
            jsonl_path=phase1_jsonl,
            instance="windows-phase1",
        )
        windows_proc, windows_stdout, windows_stderr = start_windows(
            exe=exe_path,
            argv=phase1_argv,
            env=child_env,
            stdout_path=phase1_win / "stdout.log",
            stderr_path=phase1_win / "stderr.log",
            cwd=source_dir,
            popen=popen,
        )
        state.windows_pid_phase1 = getattr(windows_proc, "pid", None)

        def windows_alive() -> None:
            poll = getattr(windows_proc, "poll", None)
            if callable(poll) and poll() is not None:
                raise PersistenceFailure("process_exited_early", "Windows process exited early")

        def windows_phase1_ready() -> bool:
            windows_alive()
            records = live.iter_jsonl_records(phase1_jsonl)
            uid = live.extract_runtime_started_uid(records)
            if uid:
                state.windows_uid_phase1 = uid
            return bool(state.windows_uid_phase1) and live.accepted_peer_add(
                records, str(state.android_uid_phase1)
            )

        live.wait_until(
            windows_phase1_ready,
            timeout_s=timeout_s,
            failure_kind="phase1_pairing_timeout",
            message="Windows phase-1 runtime/peer_add not observed",
            sleep=sleep,
            monotonic=monotonic,
        )

        def android_paired() -> bool:
            windows_alive()
            text = (phase1_and / "logcat.txt").read_text(encoding="utf-8", errors="replace")
            if live.has_fatal_android_error(text):
                raise PersistenceFailure("fatal_android_error", "fatal Android error during pairing")
            return live.pairing_evidence(text, str(state.windows_uid_phase1))

        live.wait_until(
            android_paired,
            timeout_s=timeout_s,
            failure_kind="phase1_pairing_timeout",
            message="Android did not accept the Windows peer",
            sleep=sleep,
            monotonic=monotonic,
        )

        live.atomic_write_inbox(phase1_inbox, state.pre_w_to_a)

        def windows_submitted(path: Path, text: str, bucket: dict[str, Any]) -> bool:
            windows_alive()
            event_id = live.accepted_text_submit(live.iter_jsonl_records(path), text)
            if event_id is not None:
                bucket.setdefault("submitted_event_obj_ids", {})[text] = event_id
                return True
            return False

        live.wait_until(
            lambda: windows_submitted(phase1_jsonl, state.pre_w_to_a, state.phase1),
            timeout_s=timeout_s,
            failure_kind="phase1_delivery_failed",
            message="phase-1 Windows text_submit not accepted",
            sleep=sleep,
            monotonic=monotonic,
        )

        def android_has_message(logical: str, message: str) -> bool:
            windows_alive()
            try:
                transcript = dump_transcript(
                    adb,
                    run_id=run_id,
                    android_root=android_root,
                    logical_name=logical,
                    sleep=sleep,
                    monotonic=monotonic,
                    state=state,
                )
            except live.LiveChatFailure as exc:
                if exc.failure_kind in {"android_ui_dump_failed", "android_ui_xml_invalid"}:
                    return False
                raise
            count = live.count_transcript_message(transcript, message)
            if count > 1:
                raise PersistenceFailure("duplicate_persisted_message", f"{message!r} appeared {count} times")
            return count == 1

        live.wait_until(
            lambda: android_has_message("phase1/transcript.xml", state.pre_w_to_a),
            timeout_s=timeout_s,
            failure_kind="phase1_delivery_failed",
            message="phase-1 Windows message not visible on Android",
            sleep=sleep,
            monotonic=monotonic,
        )

        send_android_message(
            adb,
            run_id=run_id,
            android_root=android_root,
            logical_name="phase1/transcript.xml",
            message=state.pre_a_to_w,
            sleep=sleep,
            monotonic=monotonic,
            state=state,
        )

        def android_committed(path: Path, message: str, bucket: dict[str, Any]) -> bool:
            windows_alive()
            text = path.read_text(encoding="utf-8", errors="replace") if path.exists() else ""
            if live.has_fatal_android_error(text):
                raise PersistenceFailure("fatal_android_error", "fatal Android error during submit")
            if not android_message_committed(text, message):
                return False
            event_id = parse_android_commit_event_id(text, message)
            if event_id is not None:
                bucket.setdefault("submitted_event_obj_ids", {})[message] = event_id
            return True

        live.wait_until(
            lambda: android_committed(phase1_and / "logcat.txt", state.pre_a_to_w, state.phase1),
            timeout_s=timeout_s,
            failure_kind="phase1_delivery_failed",
            message="phase-1 Android MESSAGE_COMMITTED not observed",
            sleep=sleep,
            monotonic=monotonic,
        )

        def windows_visible(path: Path, message: str, bucket: dict[str, Any]) -> bool:
            windows_alive()
            records = live.iter_jsonl_records(path)
            try:
                event_id = live.unique_message_visible_id(records, message)
            except live.LiveChatFailure as exc:
                if exc.failure_kind == "duplicate_message":
                    raise PersistenceFailure("duplicate_persisted_message", str(exc)) from exc
                raise
            if event_id is None:
                return False
            bucket.setdefault("visible_event_obj_ids", {})[message] = event_id
            return True

        live.wait_until(
            lambda: windows_visible(phase1_jsonl, state.pre_a_to_w, state.phase1),
            timeout_s=timeout_s,
            failure_kind="phase1_delivery_failed",
            message="phase-1 Android message not visible on Windows",
            sleep=sleep,
            monotonic=monotonic,
        )
        transcript = dump_transcript(
            adb,
            run_id=run_id,
            android_root=android_root,
            logical_name="phase1/transcript.xml",
            sleep=sleep,
            monotonic=monotonic,
            state=state,
        )
        state.phase1["history_count"] = sum(
            live.count_transcript_message(transcript, item)
            for item in (state.pre_w_to_a, state.pre_a_to_w)
        )
        state.phase1["messages"] = {
            "windows_to_android": state.pre_w_to_a,
            "android_to_windows": state.pre_a_to_w,
        }
        state.phase1["history_verified"] = True
        state.phase1["bidirectional_delivery"] = True
        phase1_visible = dict(state.phase1.get("visible_event_obj_ids") or {})
        phase1_submit = live.accepted_text_submit(live.iter_jsonl_records(phase1_jsonl), state.pre_w_to_a)
        if phase1_submit is not None:
            phase1_visible.setdefault(state.pre_w_to_a, phase1_submit)
            state.phase1.setdefault("submitted_event_obj_ids", {})[state.pre_w_to_a] = phase1_submit
        state.phase1["visible_event_obj_ids"] = {
            **(state.phase1.get("visible_event_obj_ids") or {}),
            **({state.pre_w_to_a: phase1_submit} if phase1_submit is not None else {}),
        }

        old_win_pid = state.windows_pid_phase1
        old_and_pid = state.android_pid_phase1
        stop_windows()
        stop_logcat()
        adb.shell(["am", "force-stop", PACKAGE])

        def processes_gone() -> bool:
            win_gone = not pid_is_running(old_win_pid)
            current = android_pid_of(adb) if adb is not None else None
            and_gone = current is None or current != old_and_pid
            return win_gone and and_gone

        live.wait_until(
            processes_gone,
            timeout_s=STOP_TIMEOUT_S,
            failure_kind="process_stop_failed",
            message="phase-1 processes still present after stop",
            sleep=sleep,
            monotonic=monotonic,
        )
        state.windows_phase1_stopped = True
        state.android_phase1_stopped = True

        adb.run(["logcat", "-c"])
        logcat_proc = adb.start_logcat(phase2_and / "logcat.txt")
        adb.shell(["am", "start", "-n", MAIN_ACTIVITY])
        wait_android_ready(phase2_and / "logcat.txt", uid_holder="phase2")
        require_uid_stable(str(state.android_uid_phase1), str(state.android_uid_phase2), side="android")
        require_pid_changed(state.android_pid_phase1, state.android_pid_phase2, side="android")

        restart_transcript = dump_transcript(
            adb,
            run_id=run_id,
            android_root=android_root,
            logical_name="phase2/transcript_after_restart.xml",
            sleep=sleep,
            monotonic=monotonic,
            state=state,
        )
        classify_android_history(restart_transcript, [state.pre_w_to_a, state.pre_a_to_w])

        phase2_jsonl = phase2_win / "runtime.jsonl"
        phase2_inbox = phase2_win / "commit.inbox"
        phase2_argv = windows_launch_argv(
            exe_path,
            state_dir=shared_state,
            client_name=state.client_name,
            inbox=phase2_inbox,
            peer=None,
            auto_accept=False,
        )
        state.phase2_peer_argument_used = "--peer" in phase2_argv or "--auto-accept-peer" in phase2_argv
        if state.phase2_peer_argument_used:
            raise PersistenceFailure("phase2_reconnect_timeout", "phase-2 Windows argv re-authorized the peer")
        state.command_log.append(phase2_argv)
        child_env = live.windows_env(
            env if env is not None else os.environ.copy(),
            run_id=run_id,
            jsonl_path=phase2_jsonl,
            instance="windows-phase2",
        )
        windows_proc, windows_stdout, windows_stderr = start_windows(
            exe=exe_path,
            argv=phase2_argv,
            env=child_env,
            stdout_path=phase2_win / "stdout.log",
            stderr_path=phase2_win / "stderr.log",
            cwd=source_dir,
            popen=popen,
        )
        state.windows_pid_phase2 = getattr(windows_proc, "pid", None)

        def windows_phase2_ready() -> bool:
            windows_alive()
            records = live.iter_jsonl_records(phase2_jsonl)
            uid = live.extract_runtime_started_uid(records)
            if uid:
                state.windows_uid_phase2 = uid
            return bool(state.windows_uid_phase2)

        live.wait_until(
            windows_phase2_ready,
            timeout_s=timeout_s,
            failure_kind="phase2_reconnect_timeout",
            message="Windows phase-2 runtime_started not observed",
            sleep=sleep,
            monotonic=monotonic,
        )
        require_uid_stable(str(state.windows_uid_phase1), str(state.windows_uid_phase2), side="windows")
        require_pid_changed(state.windows_pid_phase1, state.windows_pid_phase2, side="windows")

        try:
            phase2_history = persisted_windows_event_ids(
                live.iter_jsonl_records(phase2_jsonl),
                [state.pre_w_to_a, state.pre_a_to_w],
            )
        except PersistenceFailure as exc:
            if exc.failure_kind == "windows_history_missing_after_restart":
                raise PersistenceFailure(
                    "windows_persistence_observability_missing",
                    "Windows JSONL did not expose persisted phase-1 messages after restart",
                ) from exc
            raise
        require_event_ids_unchanged(
            {
                state.pre_a_to_w: (state.phase1.get("visible_event_obj_ids") or {}).get(state.pre_a_to_w),
                state.pre_w_to_a: (state.phase1.get("submitted_event_obj_ids") or {}).get(state.pre_w_to_a),
            },
            phase2_history,
        )
        state.phase2["visible_event_obj_ids"] = dict(phase2_history)

        live.atomic_write_inbox(phase2_inbox, state.post_w_to_a)
        live.wait_until(
            lambda: windows_submitted(phase2_jsonl, state.post_w_to_a, state.phase2),
            timeout_s=timeout_s,
            failure_kind="phase2_delivery_failed",
            message="phase-2 Windows text_submit not accepted",
            sleep=sleep,
            monotonic=monotonic,
        )
        live.wait_until(
            lambda: android_has_message("phase2/transcript_after_messages.xml", state.post_w_to_a),
            timeout_s=timeout_s,
            failure_kind="phase2_delivery_failed",
            message="phase-2 Windows message not visible on Android",
            sleep=sleep,
            monotonic=monotonic,
        )
        send_android_message(
            adb,
            run_id=run_id,
            android_root=android_root,
            logical_name="phase2/transcript_after_messages.xml",
            message=state.post_a_to_w,
            sleep=sleep,
            monotonic=monotonic,
            state=state,
        )
        live.wait_until(
            lambda: android_committed(phase2_and / "logcat.txt", state.post_a_to_w, state.phase2),
            timeout_s=timeout_s,
            failure_kind="phase2_delivery_failed",
            message="phase-2 Android MESSAGE_COMMITTED not observed",
            sleep=sleep,
            monotonic=monotonic,
        )
        live.wait_until(
            lambda: windows_visible(phase2_jsonl, state.post_a_to_w, state.phase2),
            timeout_s=timeout_s,
            failure_kind="phase2_delivery_failed",
            message="phase-2 Android message not visible on Windows",
            sleep=sleep,
            monotonic=monotonic,
        )

        final_transcript = dump_transcript(
            adb,
            run_id=run_id,
            android_root=android_root,
            logical_name="phase2/transcript_after_messages.xml",
            sleep=sleep,
            monotonic=monotonic,
            state=state,
        )
        four = [state.pre_w_to_a, state.pre_a_to_w, state.post_w_to_a, state.post_a_to_w]
        counts = classify_android_history(final_transcript, four)
        state.phase2["history_count"] = sum(counts.values())
        state.phase2["messages"] = {
            "windows_to_android": state.post_w_to_a,
            "android_to_windows": state.post_a_to_w,
        }
        state.phase2["history_verified"] = True
        state.phase2["bidirectional_delivery"] = True
        final_windows = persisted_windows_event_ids(
            live.iter_jsonl_records(phase2_jsonl),
            [state.pre_w_to_a, state.pre_a_to_w, state.post_a_to_w],
        )
        post_submit = live.accepted_text_submit(live.iter_jsonl_records(phase2_jsonl), state.post_w_to_a)
        if post_submit is not None:
            final_windows[state.post_w_to_a] = post_submit
        else:
            try:
                final_windows[state.post_w_to_a] = persisted_windows_event_ids(
                    live.iter_jsonl_records(phase2_jsonl), [state.post_w_to_a]
                )[state.post_w_to_a]
            except PersistenceFailure:
                if state.post_w_to_a not in final_windows:
                    raise PersistenceFailure(
                        "phase2_delivery_failed",
                        "phase-2 Windows JSONL missing post Windows message Event ObjId",
                    )
        state.phase2["visible_event_obj_ids"] = final_windows

        add_peer_used = any("add_participant" in " ".join(call) for call in state.command_log)
        if add_peer_used or state.phase2_peer_argument_used:
            raise PersistenceFailure("phase2_reconnect_timeout", "phase 2 re-paired the peer")

        cleanup()
        if not state.verbose_restored:
            return fail(
                "android_verbose_property_cleanup_failed",
                "debug.apptraverse.verbose_log was not restored to 0",
            )
        android, windows, cleanup_summary = snapshot(state)
        result = compact_persistence_result(
            run_id=run_id,
            status="ok",
            duration_ms=duration_ms(),
            failure_kind=None,
            first_error=None,
            android=android,
            windows=windows,
            phase1=state.phase1,
            phase2=state.phase2,
            cleanup=cleanup_summary,
        )
        live.reject_absolute_paths(result)
        live.atomic_write_json(artifact_dir / "result.json", result)
        return result
    except live.LiveChatFailure as exc:
        cleanup()
        return fail(exc.failure_kind, exc.first_error)
    except subprocess.TimeoutExpired as exc:
        cleanup()
        return fail("scenario_timeout", str(exc))
    except Exception as exc:  # noqa: BLE001
        cleanup()
        return fail("scenario_timeout", str(exc))


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Validate Windows/Android restart persistence")
    parser.add_argument("--serial", required=True)
    parser.add_argument("--windows-exe", required=True)
    parser.add_argument("--source-dir", default=str(live.repo_root()))
    parser.add_argument("--timeout-seconds", type=float, default=READY_TIMEOUT_S)
    parser.add_argument("--adb", default="")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    result = run_windows_android_persistence(
        source_dir=Path(args.source_dir),
        windows_exe=Path(args.windows_exe),
        serial=args.serial,
        timeout_s=float(args.timeout_seconds),
        adb_path=Path(args.adb) if args.adb else None,
    )
    sys.stdout.write(json.dumps(result, separators=(",", ":")) + "\n")
    return 0 if result.get("status") == "ok" else 1


if __name__ == "__main__":
    raise SystemExit(main())
