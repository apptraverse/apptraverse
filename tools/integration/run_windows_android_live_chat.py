#!/usr/bin/env python3
"""Live Windows <-> Android x86_64 chat validation. Stdlib only. One JSON result."""

from __future__ import annotations

import argparse
import json
import os
import re
import secrets
import shutil
import subprocess
import sys
import time
import xml.etree.ElementTree as ET
from dataclasses import dataclass, field
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Callable, IO

_REPO_ROOT = Path(__file__).resolve().parents[2]
if str(_REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(_REPO_ROOT))

from tools.runners.run_apptraverse_job import terminate_process_tree  # noqa: E402

RESULT_SCHEMA_VERSION = "apptraverse.windows_android_result/1"
PACKAGE = "com.apptraverse.singleclientchat"
MAIN_ACTIVITY = f"{PACKAGE}/.MainActivity"
TRANSCRIPT_ID = f"{PACKAGE}:id/transcript"
MESSAGE_INPUT_ID = f"{PACKAGE}:id/message_input"
SEND_ID = f"{PACKAGE}:id/send"
VERBOSE_PROPERTY = "debug.apptraverse.verbose_log"
EXPECTED_ABI = "x86_64"
EXPECTED_EXE_NAME = "win32_single_client_chat.exe"
ARTIFACT_PREFIX = "windows-android-live"
READY_TIMEOUT_S = 60.0
POLL_INTERVAL_S = 0.25
DUMP_ATTEMPT_LIMIT = 3
DUMP_RETRY_DELAY_S = 0.5
FOREGROUND_TIMEOUT_S = 10.0
EXCERPT_LIMIT = 1000
MAX_OBSERVED_RESOURCE_IDS = 20
FORBIDDEN_TOKENS = (
    "pm clear",
    "uninstall",
    "gradlew",
    "gradle",
    "cmake --build",
    "--clean",
    "clean",
    ":app:install",
    "installDebug",
    "--print-aether-uid",
)
FATAL_PATTERNS = (
    "FATAL EXCEPTION",
    "Fatal signal",
    "A/libc",
    "F/libc",
    "*** FATAL ***",
)
PAIRING_MARKERS = (
    "CHAT_PEER_ADDED",
    "CHAT_PEER_ONLINE",
    "CHAT_SYNC_INITIAL_COMPLETE",
)
AETHER_UID_RE = re.compile(
    r"AETHER_CLIENT_READY\s+platform=android\s+uid=(\S+)"
)
BOUNDS_RE = re.compile(r"\[(\d+),(\d+)\]\[(\d+),(\d+)\]")


class LiveChatFailure(Exception):
    def __init__(
        self,
        failure_kind: str,
        first_error: str,
        extras: dict[str, Any] | None = None,
    ) -> None:
        super().__init__(first_error)
        self.failure_kind = failure_kind
        self.first_error = first_error
        self.extras = extras or {}


def repo_root() -> Path:
    return Path(__file__).resolve().parents[2]


def new_run_id() -> str:
    stamp = datetime.now(timezone.utc).strftime("%Y%m%d-%H%M%S")
    return f"{stamp}-{secrets.token_hex(3)}"


def short_run_id(run_id: str) -> str:
    if "-" in run_id:
        return run_id.rsplit("-", 1)[-1]
    return run_id[-6:]


def artifact_id_for(run_id: str) -> str:
    return f"{ARTIFACT_PREFIX}/{run_id}"


def atomic_write_text(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_name(path.name + ".tmp")
    tmp.write_text(text, encoding="utf-8")
    tmp.replace(path)


def atomic_write_inbox(path: Path, text: str) -> None:
    atomic_write_text(path, text + "\n")


def atomic_write_json(path: Path, payload: dict[str, Any]) -> None:
    atomic_write_text(path, json.dumps(payload, indent=2) + "\n")


def find_adb() -> Path:
    names = ("adb.exe", "adb") if sys.platform.startswith("win") else ("adb",)
    for key in ("ANDROID_HOME", "ANDROID_SDK_ROOT"):
        root = os.environ.get(key)
        if not root:
            continue
        for name in names:
            candidate = Path(root) / "platform-tools" / name
            if candidate.is_file():
                return candidate
    if sys.platform.startswith("win"):
        local = (
            Path.home()
            / "AppData"
            / "Local"
            / "Android"
            / "Sdk"
            / "platform-tools"
            / "adb.exe"
        )
        if local.is_file():
            return local
    which = shutil.which("adb")
    if which:
        return Path(which)
    raise LiveChatFailure("android_device_unavailable", "adb executable not found")


def parse_adb_devices(text: str) -> list[dict[str, str]]:
    devices: list[dict[str, str]] = []
    for raw_line in text.splitlines():
        line = raw_line.strip()
        if not line or line.startswith("List of devices"):
            continue
        parts = line.split()
        if len(parts) < 2:
            continue
        serial, state = parts[0], parts[1]
        if serial == "*":
            continue
        devices.append({"serial": serial, "state": state})
    return devices


def select_android_device(
    devices: list[dict[str, str]], serial: str
) -> dict[str, str]:
    for device in devices:
        if device.get("serial") == serial and device.get("state") == "device":
            return device
    raise LiveChatFailure(
        "android_device_unavailable",
        f"serial {serial} is not in state device",
    )


def require_abi(abi: str) -> None:
    if abi.strip() != EXPECTED_ABI:
        raise LiveChatFailure(
            "android_device_unavailable",
            f"expected ABI {EXPECTED_ABI}, got {abi.strip() or '<empty>'}",
        )


def parse_android_uid(log_text: str) -> str | None:
    match = AETHER_UID_RE.search(log_text)
    if match is None:
        return None
    uid = match.group(1).strip().rstrip(".,;]")
    return uid or None


def iter_jsonl_records(path: Path) -> list[dict[str, Any]]:
    if not path.exists():
        return []
    records: list[dict[str, Any]] = []
    text = path.read_text(encoding="utf-8", errors="replace")
    for raw_line in text.splitlines():
        line = raw_line.strip()
        if not line:
            continue
        try:
            loaded = json.loads(line)
        except json.JSONDecodeError:
            continue
        if isinstance(loaded, dict):
            records.append(loaded)
    return records


def extract_runtime_started_uid(records: list[dict[str, Any]]) -> str | None:
    for record in records:
        if record.get("event") != "runtime_started":
            continue
        data = record.get("data") or {}
        uid = data.get("local_uid")
        if isinstance(uid, str) and uid.strip():
            return uid.strip()
    return None


def accepted_peer_add(records: list[dict[str, Any]], peer_uid: str) -> bool:
    for record in records:
        if record.get("event") != "peer_add":
            continue
        data = record.get("data") or {}
        if data.get("accepted") is True and data.get("peer") == peer_uid:
            return True
    return False


def accepted_text_submit(records: list[dict[str, Any]], text: str) -> Any:
    for record in records:
        if record.get("event") != "text_submit":
            continue
        data = record.get("data") or {}
        if data.get("text") == text and data.get("accepted") is True:
            event_id = data.get("event_obj_id")
            if event_id is not None:
                return event_id
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


def unique_message_visible_id(records: list[dict[str, Any]], text: str) -> Any:
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
        return None
    if len(unique) != 1:
        raise LiveChatFailure(
            "duplicate_message",
            f"expected one unique Event ObjId for {text!r}, got {unique!r}",
        )
    return unique[0]


def parse_bounds(raw: str) -> tuple[int, int, int, int]:
    match = BOUNDS_RE.fullmatch((raw or "").strip())
    if match is None:
        raise LiveChatFailure("android_ui_control_missing", f"invalid bounds: {raw!r}")
    left, top, right, bottom = (int(match.group(i)) for i in range(1, 5))
    return left, top, right, bottom


def bounds_center(bounds: tuple[int, int, int, int]) -> tuple[int, int]:
    left, top, right, bottom = bounds
    return (left + right) // 2, (top + bottom) // 2


def excerpt_text(text: str | None, limit: int = EXCERPT_LIMIT) -> str:
    return (text or "")[:limit]


def remote_dump_path(run_id: str, logical_name: str, attempt: int) -> str:
    safe = re.sub(r"[^A-Za-z0-9._-]+", "_", logical_name)
    return f"/data/local/tmp/apptraverse_uidump_{run_id}_{safe}_{attempt}.xml"


def attempt_artifact_name(logical_name: str, attempt: int) -> str:
    stem = logical_name[:-4] if logical_name.endswith(".xml") else logical_name
    return f"{stem}.attempt{attempt}.json"


def extract_hierarchy_xml(raw: str) -> str | None:
    if not raw:
        return None
    start = raw.find("<?xml")
    if start < 0:
        start = raw.find("<hierarchy")
    if start < 0:
        return None
    end = raw.find("</hierarchy>", start)
    if end < 0:
        return None
    return raw[start : end + len("</hierarchy>")]


def parse_hierarchy_xml(xml_text: str) -> ET.Element:
    try:
        return ET.fromstring(xml_text)
    except ET.ParseError as exc:
        raise LiveChatFailure("android_ui_xml_invalid", f"invalid UI XML: {exc}") from exc


def find_nodes_by_resource(root: ET.Element, resource_id: str) -> list[ET.Element]:
    found: list[ET.Element] = []
    for node in root.iter():
        if node.get("resource-id") == resource_id:
            found.append(node)
    return found


def observed_resource_ids(root: ET.Element, limit: int = MAX_OBSERVED_RESOURCE_IDS) -> list[str]:
    ids: list[str] = []
    seen: set[str] = set()
    for node in root.iter():
        resource_id = node.get("resource-id")
        if not resource_id or resource_id in seen:
            continue
        seen.add(resource_id)
        ids.append(resource_id)
        if len(ids) >= limit:
            break
    return ids


def apptraverse_resource_ids(root: ET.Element) -> list[str]:
    prefix = f"{PACKAGE}:id/"
    return [item for item in observed_resource_ids(root, limit=100) if item.startswith(prefix)]


def node_text(node: ET.Element) -> str:
    return node.get("text") or ""


def missing_control_failure(
    xml_text: str,
    resource_id: str,
    focused_window: str = "",
) -> LiveChatFailure:
    root = parse_hierarchy_xml(xml_text)
    return LiveChatFailure(
        "android_ui_control_missing",
        f"missing UI control {resource_id}",
        extras={
            "requested_resource_id": resource_id,
            "observed_resource_ids": observed_resource_ids(root),
            "focused_window": focused_window,
        },
    )


def extract_control_text(xml_text: str, resource_id: str, focused_window: str = "") -> str:
    nodes = find_nodes_by_resource(parse_hierarchy_xml(xml_text), resource_id)
    if not nodes:
        raise missing_control_failure(xml_text, resource_id, focused_window)
    return node_text(nodes[0])


def extract_control_center(
    xml_text: str, resource_id: str, focused_window: str = ""
) -> tuple[int, int]:
    nodes = find_nodes_by_resource(parse_hierarchy_xml(xml_text), resource_id)
    if not nodes:
        raise missing_control_failure(xml_text, resource_id, focused_window)
    return bounds_center(parse_bounds(nodes[0].get("bounds") or ""))


def parse_focused_window(dumpsys_windows: str) -> str:
    for key in ("mCurrentFocus=", "mFocusedApp=", "mObscuringWindow="):
        for raw_line in dumpsys_windows.splitlines():
            if key in raw_line:
                return raw_line.strip()[:240]
    return ""


def count_transcript_message(transcript: str, message: str) -> int:
    if not message:
        return 0
    return transcript.count(message)


def encode_adb_input_text(message: str) -> str:
    return message.replace(" ", "%s")


def windows_env(
    base: dict[str, str],
    *,
    run_id: str,
    jsonl_path: Path,
    instance: str = "windows",
) -> dict[str, str]:
    env = dict(base)
    env.pop("APPTRAVERSE_VERBOSE_LOG", None)
    env["APPTRAVERSE_RUNTIME_JSONL"] = str(jsonl_path)
    env["APPTRAVERSE_RUN_ID"] = run_id
    env["APPTRAVERSE_INSTANCE"] = instance
    return env


def argv_contains_forbidden(argv: list[str]) -> str | None:
    joined = " ".join(argv).lower()
    for token in FORBIDDEN_TOKENS:
        needle = token.lower()
        if needle == "clean":
            if any(part.lower() in {"clean", "--clean"} for part in argv):
                return token
            continue
        if needle in joined:
            return token
    return None


def result_contains_forbidden_payload(result: dict[str, Any]) -> bool:
    text = json.dumps(result)
    return any(token in text for token in ("<hierarchy", "runtime.jsonl", "stdout.log", "stderr.log"))


def compact_result(
    *,
    run_id: str,
    status: str,
    duration_ms: int,
    failure_kind: str | None,
    first_error: str | None,
    android: dict[str, Any] | None,
    windows: dict[str, Any] | None,
    messages: dict[str, Any] | None,
) -> dict[str, Any]:
    payload = {
        "schema_version": RESULT_SCHEMA_VERSION,
        "run_id": run_id,
        "artifact_id": artifact_id_for(run_id),
        "status": status,
        "duration_ms": duration_ms,
        "failure_kind": failure_kind,
        "first_error": sanitize_error(first_error) if first_error else first_error,
        "android": android,
        "windows": windows,
        "messages": messages,
    }
    return payload


def sanitize_error(message: str) -> str:
    cleaned = re.sub(r"[A-Za-z]:[\\/][^\s\"']+", "<path>", message)
    cleaned = re.sub(r"(?<![A-Za-z:])/(?:home|Users|data|sdcard)/[^\s\"']+", "<path>", cleaned)
    return cleaned


def looks_like_absolute_path(value: str) -> bool:
    if len(value) >= 3 and value[1] == ":" and value[0].isalpha():
        return True
    return value.startswith("\\\\") or value.startswith("/home/") or value.startswith("/Users/")


def reject_absolute_paths(result: dict[str, Any]) -> None:
    def walk(item: Any) -> None:
        if isinstance(item, dict):
            for value in item.values():
                walk(value)
            return
        if isinstance(item, list):
            for value in item:
                walk(value)
            return
        if isinstance(item, str) and looks_like_absolute_path(item):
            raise LiveChatFailure(
                "scenario_timeout", "compact result contained an absolute path"
            )

    walk(result)


def pairing_evidence(log_text: str, windows_uid: str) -> bool:
    if not windows_uid or windows_uid not in log_text:
        return False
    return any(marker in log_text for marker in PAIRING_MARKERS)


def has_fatal_android_error(log_text: str) -> bool:
    return any(pattern in log_text for pattern in FATAL_PATTERNS)


def activity_resumed(dumpsys_text: str) -> bool:
    for raw_line in dumpsys_text.splitlines():
        line = raw_line.strip().replace("\\", "")
        if MAIN_ACTIVITY not in line and f"{PACKAGE}.MainActivity" not in line:
            continue
        lowered = line.lower()
        if (
            "topresumedactivity" in lowered
            or "mresumedactivity" in lowered
            or lowered.startswith("resumed")
            or "resumed:" in lowered
        ):
            return True
    return False


def validate_windows_exe(path: Path) -> Path:
    resolved = path.expanduser()
    if not resolved.is_file():
        raise LiveChatFailure("windows_executable_missing", f"windows executable missing")
    if resolved.name.lower() != EXPECTED_EXE_NAME.lower():
        raise LiveChatFailure(
            "windows_executable_missing",
            f"expected {EXPECTED_EXE_NAME}, got {resolved.name}",
        )
    return resolved.resolve()


class AdbClient:
    def __init__(
        self,
        adb: Path,
        serial: str,
        *,
        run: Callable[..., subprocess.CompletedProcess[str]] | None = None,
        popen: Callable[..., Any] = subprocess.Popen,
        command_log: list[list[str]] | None = None,
    ) -> None:
        self.adb = adb
        self.serial = serial
        self._run = run or subprocess.run
        self._popen = popen
        self.command_log = command_log if command_log is not None else []

    def argv(self, args: list[str]) -> list[str]:
        return [str(self.adb), "-s", self.serial, *args]

    def run(self, args: list[str], *, timeout: float = 30.0) -> subprocess.CompletedProcess[str]:
        argv = self.argv(args)
        forbidden = argv_contains_forbidden(argv)
        if forbidden:
            raise LiveChatFailure("scenario_timeout", f"forbidden command: {forbidden}")
        self.command_log.append(argv)
        return self._run(
            argv,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            shell=False,
            timeout=timeout,
        )

    def shell(self, args: list[str], *, timeout: float = 30.0) -> subprocess.CompletedProcess[str]:
        return self.run(["shell", *args], timeout=timeout)

    def shell_text(self, args: list[str], *, timeout: float = 30.0) -> str:
        completed = self.shell(args, timeout=timeout)
        return (completed.stdout or "").strip()

    def start_logcat(self, path: Path) -> Any:
        path.parent.mkdir(parents=True, exist_ok=True)
        handle = path.open("ab")
        argv = self.argv(["logcat", "-v", "threadtime"])
        self.command_log.append(argv)
        try:
            proc = self._popen(
                argv,
                stdout=handle,
                stderr=subprocess.STDOUT,
                shell=False,
            )
        except OSError:
            handle.close()
            raise
        proc._log_handle = handle  # type: ignore[attr-defined]
        return proc


def write_dump_attempt_artifact(dump_dir: Path, logical_name: str, record: dict[str, Any]) -> None:
    dump_dir.mkdir(parents=True, exist_ok=True)
    atomic_write_json(dump_dir / attempt_artifact_name(logical_name, int(record["attempt"])), record)


def dump_ui_hierarchy(
    adb: AdbClient,
    *,
    run_id: str,
    logical_name: str,
    android_dir: Path,
    focused_window: str,
    sleep: Callable[[float], None],
    state: "ScenarioState",
) -> str:
    dump_dir = android_dir / "ui-dump"
    last_kind = "android_ui_dump_failed"
    last_error = "no valid XML hierarchy could be acquired"
    methods = (
        ("exec_out_compressed_tty", 1),
        ("shell_compressed_file", 2),
        ("shell_file", 3),
    )
    for method, attempt in methods:
        state.ui_dump_attempt_count += 1
        remote = remote_dump_path(run_id, logical_name, attempt)
        adb.shell(["rm", "-f", remote], timeout=15.0)
        stdout = ""
        stderr = ""
        return_code: int | None = None
        xml_found = False
        xml_valid = False
        failure: str | None = None
        fragment: str | None = None
        try:
            if method == "exec_out_compressed_tty":
                completed = adb.run(
                    ["exec-out", "uiautomator", "dump", "--compressed", "/dev/tty"],
                    timeout=30.0,
                )
            elif method == "shell_compressed_file":
                completed = adb.shell(
                    ["uiautomator", "dump", "--compressed", remote],
                    timeout=30.0,
                )
            else:
                completed = adb.shell(["uiautomator", "dump", remote], timeout=30.0)
            return_code = completed.returncode
            stdout = completed.stdout or ""
            stderr = completed.stderr or ""
        except subprocess.TimeoutExpired as exc:
            failure = "android_ui_dump_failed"
            last_kind = failure
            last_error = str(exc)
            stdout = getattr(exc, "stdout", "") or ""
            stderr = getattr(exc, "stderr", "") or ""
        except OSError as exc:
            failure = "android_ui_dump_failed"
            last_kind = failure
            last_error = str(exc)

        combined = stdout + "\n" + stderr
        fragment = extract_hierarchy_xml(combined)
        if fragment is None and method != "exec_out_compressed_tty":
            cat = adb.shell(["cat", remote], timeout=15.0)
            fragment = extract_hierarchy_xml((cat.stdout or "") + "\n" + (cat.stderr or ""))
            if not stdout:
                stdout = cat.stdout or ""
            if not stderr:
                stderr = cat.stderr or ""

        xml_found = fragment is not None
        if fragment is not None:
            try:
                parse_hierarchy_xml(fragment)
                xml_valid = True
            except LiveChatFailure as exc:
                xml_valid = False
                failure = "android_ui_xml_invalid"
                last_kind = "android_ui_xml_invalid"
                last_error = exc.first_error
        elif failure is None:
            failure = "android_ui_dump_failed"
            last_kind = "android_ui_dump_failed"
            last_error = "uiautomator dump produced no hierarchy XML"

        record = {
            "logical_name": logical_name,
            "attempt": attempt,
            "method": method,
            "return_code": return_code,
            "stdout_excerpt": excerpt_text(stdout),
            "stderr_excerpt": excerpt_text(stderr),
            "focused_window": focused_window,
            "xml_found": xml_found,
            "xml_valid": xml_valid,
            "failure": None if xml_valid else failure,
        }
        if xml_valid and fragment is not None:
            state.last_ui_dump_failure = None
            state.last_ui_dump_method = method
            atomic_write_text(android_dir / logical_name, fragment)
            return fragment
        write_dump_attempt_artifact(dump_dir, logical_name, record)
        state.last_ui_dump_failure = last_kind
        if attempt < DUMP_ATTEMPT_LIMIT:
            sleep(DUMP_RETRY_DELAY_S)

    raise LiveChatFailure(last_kind, last_error)


def ensure_main_activity_foreground(
    adb: AdbClient,
    state: "ScenarioState",
    *,
    sleep: Callable[[float], None],
    monotonic: Callable[[], float],
) -> None:
    adb.shell(["am", "start", "-W", "-n", MAIN_ACTIVITY], timeout=30.0)

    def ready() -> bool:
        activities = adb.shell_text(["dumpsys", "activity", "activities"], timeout=20.0)
        state.activity_resumed = activity_resumed(activities)
        windows = adb.shell_text(["dumpsys", "window", "windows"], timeout=20.0)
        state.focused_window = parse_focused_window(windows)
        return state.activity_resumed

    try:
        wait_until(
            ready,
            timeout_s=FOREGROUND_TIMEOUT_S,
            failure_kind="android_foreground_obstructed",
            message="MainActivity did not become the resumed foreground activity",
            sleep=sleep,
            monotonic=monotonic,
        )
    except LiveChatFailure as exc:
        raise LiveChatFailure(
            "android_foreground_obstructed",
            f"{exc.first_error}; focused_window={state.focused_window or '<unknown>'}",
            extras={"focused_window": state.focused_window},
        ) from exc


@dataclass
class ScenarioState:
    android_pid: int | None = None
    android_uid: str | None = None
    android_api: str | None = None
    android_abi: str | None = None
    activity_resumed: bool = False
    windows_pid: int | None = None
    windows_uid: str | None = None
    windows_message_visible_count: int | None = None
    android_message_committed: bool | None = None
    local_submit_event_obj_id: Any = None
    android_message_visible_event_obj_id: Any = None
    windows_to_android: str | None = None
    android_to_windows: str | None = None
    verbose_previous: str = ""
    verbose_restored: bool = False
    command_log: list[list[str]] = field(default_factory=list)
    focused_window: str | None = None
    ui_dump_attempt_count: int = 0
    last_ui_dump_failure: str | None = None
    last_ui_dump_method: str | None = None
    requested_resource_id: str | None = None
    observed_resource_ids: list[str] | None = None


def snapshot_android(state: ScenarioState, serial: str) -> dict[str, Any]:
    payload: dict[str, Any] = {
        "serial": serial,
        "pid": state.android_pid,
        "uid": state.android_uid,
        "api": state.android_api,
        "abi": state.android_abi,
        "activity_resumed": state.activity_resumed,
        "windows_message_visible_count": state.windows_message_visible_count,
        "android_message_committed": state.android_message_committed,
        "ui_dump_attempt_count": state.ui_dump_attempt_count,
        "last_ui_dump_failure": state.last_ui_dump_failure,
        "focused_window": state.focused_window,
    }
    if state.requested_resource_id is not None:
        payload["requested_resource_id"] = state.requested_resource_id
    if state.observed_resource_ids is not None:
        payload["observed_resource_ids"] = state.observed_resource_ids
    return payload


def snapshot_windows(state: ScenarioState) -> dict[str, Any]:
    return {
        "pid": state.windows_pid,
        "uid": state.windows_uid,
        "peer_uid": state.android_uid,
        "local_submit_event_obj_id": state.local_submit_event_obj_id,
        "android_message_visible_event_obj_id": state.android_message_visible_event_obj_id,
    }


def snapshot_messages(state: ScenarioState) -> dict[str, Any]:
    return {
        "windows_to_android": state.windows_to_android,
        "android_to_windows": state.android_to_windows,
    }


def wait_until(
    predicate: Callable[[], bool],
    *,
    timeout_s: float,
    failure_kind: str,
    message: str,
    sleep: Callable[[float], None],
    monotonic: Callable[[], float],
    on_tick: Callable[[], None] | None = None,
) -> None:
    deadline = monotonic() + timeout_s
    while monotonic() < deadline:
        if on_tick is not None:
            on_tick()
        if predicate():
            return
        sleep(POLL_INTERVAL_S)
    raise LiveChatFailure(failure_kind, message)


def close_proc_log(proc: Any) -> None:
    handle = getattr(proc, "_log_handle", None)
    if handle is None:
        return
    try:
        handle.close()
    except OSError:
        pass


def run_windows_android_live_chat(
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
    env: dict[str, str] | None = None,
) -> dict[str, Any]:
    started = monotonic()
    run_id = run_id or new_run_id()
    short_id = short_run_id(run_id)
    artifact_dir = source_dir / ".artifacts" / ARTIFACT_PREFIX / run_id
    artifact_dir.mkdir(parents=True, exist_ok=True)
    windows_dir = artifact_dir / "windows"
    android_dir = artifact_dir / "android"
    windows_dir.mkdir(parents=True, exist_ok=True)
    android_dir.mkdir(parents=True, exist_ok=True)
    logcat_path = android_dir / "logcat.txt"
    windows_jsonl = windows_dir / "runtime.jsonl"
    windows_inbox = windows_dir / "commit.inbox"
    windows_state = windows_dir / "state"
    windows_state.mkdir(parents=True, exist_ok=True)
    state = ScenarioState()
    windows_proc: Any = None
    logcat_proc: Any = None
    windows_stdout: IO[bytes] | None = None
    windows_stderr: IO[bytes] | None = None
    adb: AdbClient | None = None

    def duration_ms() -> int:
        return int((monotonic() - started) * 1000)

    def logcat_text() -> str:
        if not logcat_path.exists():
            return ""
        return logcat_path.read_text(encoding="utf-8", errors="replace")

    def fail(kind: str, message: str) -> dict[str, Any]:
        result = compact_result(
            run_id=run_id,
            status="failed",
            duration_ms=duration_ms(),
            failure_kind=kind,
            first_error=message,
            android=snapshot_android(state, serial),
            windows=snapshot_windows(state),
            messages=snapshot_messages(state),
        )
        atomic_write_json(artifact_dir / "result.json", result)
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

    def cleanup() -> None:
        if windows_proc is not None:
            pid = getattr(windows_proc, "pid", None)
            poll = getattr(windows_proc, "poll", None)
            still_running = True
            if callable(poll):
                still_running = poll() is None
            if pid and still_running:
                terminate(int(pid))
        if windows_stdout is not None:
            try:
                windows_stdout.close()
            except OSError:
                pass
        if windows_stderr is not None:
            try:
                windows_stderr.close()
            except OSError:
                pass
        if logcat_proc is not None:
            pid = getattr(logcat_proc, "pid", None)
            poll = getattr(logcat_proc, "poll", None)
            still_running = True
            if callable(poll):
                still_running = poll() is None
            if pid and still_running:
                terminate(int(pid))
            close_proc_log(logcat_proc)
        if adb is not None:
            try:
                adb.shell(["am", "force-stop", PACKAGE], timeout=15.0)
            except Exception:  # noqa: BLE001
                pass
        restore_verbose()

    try:
        exe_path = Path(windows_exe)
        if not exe_path.is_absolute():
            exe_path = source_dir / exe_path
        exe_path = validate_windows_exe(exe_path)

        adb_exe = adb_path or find_adb()
        adb = AdbClient(
            adb_exe,
            serial,
            run=run,
            popen=popen,
            command_log=state.command_log,
        )
        devices_out = adb.run(["devices"]).stdout
        select_android_device(parse_adb_devices(devices_out), serial)
        abi = adb.shell_text(["getprop", "ro.product.cpu.abi"])
        state.android_abi = abi
        require_abi(abi)
        state.android_api = adb.shell_text(["getprop", "ro.build.version.sdk"])
        pkg = adb.shell_text(["pm", "path", PACKAGE])
        if "package:" not in pkg:
            raise LiveChatFailure("android_package_missing", f"{PACKAGE} is not installed")

        state.verbose_previous = adb.shell_text(["getprop", VERBOSE_PROPERTY])
        adb.shell(["setprop", VERBOSE_PROPERTY, "1"])
        adb.run(["logcat", "-c"])
        adb.shell(["am", "force-stop", PACKAGE])
        logcat_proc = adb.start_logcat(logcat_path)
        start = adb.shell(["am", "start", "-n", MAIN_ACTIVITY])
        if start.returncode not in (0, None) and "Error" in (start.stderr or ""):
            raise LiveChatFailure("android_runtime_not_ready", "failed to launch MainActivity")

        def android_ready() -> bool:
            text = logcat_text()
            if has_fatal_android_error(text):
                raise LiveChatFailure("fatal_android_error", "fatal Android error during startup")
            uid = parse_android_uid(text)
            if uid:
                state.android_uid = uid
            pid_text = adb.shell_text(["pidof", PACKAGE]) if adb is not None else ""
            if pid_text.split():
                try:
                    state.android_pid = int(pid_text.split()[0])
                except ValueError:
                    state.android_pid = None
            dumpsys = adb.shell_text(["dumpsys", "activity", "activities"]) if adb is not None else ""
            state.activity_resumed = activity_resumed(dumpsys)
            return (
                bool(state.android_uid)
                and "CHAT_SYNC_CONTROLLER_READY" in text
                and state.android_pid is not None
                and state.activity_resumed
            )

        wait_until(
            android_ready,
            timeout_s=timeout_s,
            failure_kind="android_runtime_not_ready",
            message="Android runtime markers not observed",
            sleep=sleep,
            monotonic=monotonic,
        )

        windows_to_android = f"w_to_a_{short_id}"
        android_to_windows = f"a_to_w_{short_id}"
        state.windows_to_android = windows_to_android
        state.android_to_windows = android_to_windows

        stdout_path = windows_dir / "stdout.log"
        stderr_path = windows_dir / "stderr.log"
        windows_stdout = stdout_path.open("wb")
        windows_stderr = stderr_path.open("wb")
        child_env = windows_env(env if env is not None else os.environ.copy(), run_id=run_id, jsonl_path=windows_jsonl)
        windows_argv = [
            str(exe_path),
            "--state-dir",
            str(windows_state),
            "--aether-client-name",
            f"windows-android-live-{short_id}",
            "--peer",
            str(state.android_uid),
            "--commit-inbox",
            str(windows_inbox),
        ]
        if argv_contains_forbidden(windows_argv):
            raise LiveChatFailure("scenario_timeout", "windows argv contained a forbidden token")
        state.command_log.append(windows_argv)
        windows_proc = popen(
            windows_argv,
            stdout=windows_stdout,
            stderr=windows_stderr,
            env=child_env,
            cwd=str(source_dir),
            shell=False,
        )
        state.windows_pid = getattr(windows_proc, "pid", None)

        def windows_alive() -> None:
            poll = getattr(windows_proc, "poll", None)
            if callable(poll) and poll() is not None:
                raise LiveChatFailure("process_exited_early", "Windows process exited early")

        def windows_ready() -> bool:
            windows_alive()
            records = iter_jsonl_records(windows_jsonl)
            uid = extract_runtime_started_uid(records)
            if uid:
                state.windows_uid = uid
            return bool(state.windows_uid) and accepted_peer_add(records, str(state.android_uid))

        wait_until(
            windows_ready,
            timeout_s=timeout_s,
            failure_kind="windows_runtime_not_ready",
            message="Windows runtime_started/peer_add not observed",
            sleep=sleep,
            monotonic=monotonic,
        )

        def android_paired() -> bool:
            windows_alive()
            text = logcat_text()
            if has_fatal_android_error(text):
                raise LiveChatFailure("fatal_android_error", "fatal Android error during pairing")
            return pairing_evidence(text, str(state.windows_uid))

        wait_until(
            android_paired,
            timeout_s=timeout_s,
            failure_kind="pairing_timeout",
            message="Android did not accept/add the Windows peer",
            sleep=sleep,
            monotonic=monotonic,
        )

        atomic_write_inbox(windows_inbox, windows_to_android)

        def windows_submitted() -> bool:
            windows_alive()
            records = iter_jsonl_records(windows_jsonl)
            event_id = accepted_text_submit(records, windows_to_android)
            if event_id is not None:
                state.local_submit_event_obj_id = event_id
                return True
            return False

        wait_until(
            windows_submitted,
            timeout_s=timeout_s,
            failure_kind="windows_submit_failed",
            message="Windows text_submit was not accepted",
            sleep=sleep,
            monotonic=monotonic,
        )

        def apply_failure_extras(exc: LiveChatFailure) -> None:
            extras = exc.extras or {}
            if "focused_window" in extras and extras["focused_window"]:
                state.focused_window = extras["focused_window"]
            if "requested_resource_id" in extras:
                state.requested_resource_id = extras["requested_resource_id"]
            if "observed_resource_ids" in extras:
                state.observed_resource_ids = extras["observed_resource_ids"]

        def dump_ui(name: str) -> str:
            if adb is None:
                raise LiveChatFailure("android_device_unavailable", "adb client missing")
            return dump_ui_hierarchy(
                adb,
                run_id=run_id,
                logical_name=name,
                android_dir=android_dir,
                focused_window=state.focused_window or "",
                sleep=sleep,
                state=state,
            )

        def require_foreground() -> None:
            if adb is None:
                raise LiveChatFailure("android_device_unavailable", "adb client missing")
            ensure_main_activity_foreground(adb, state, sleep=sleep, monotonic=monotonic)

        require_foreground()
        dump_ui("ui_before.xml")

        def windows_message_visible() -> bool:
            windows_alive()
            try:
                require_foreground()
                xml_text = dump_ui("ui_after_windows_message.xml")
            except LiveChatFailure as exc:
                apply_failure_extras(exc)
                if exc.failure_kind in {"android_ui_dump_failed", "android_ui_xml_invalid"}:
                    return False
                raise
            try:
                transcript = extract_control_text(
                    xml_text, TRANSCRIPT_ID, focused_window=state.focused_window or ""
                )
            except LiveChatFailure as exc:
                apply_failure_extras(exc)
                raise
            count = count_transcript_message(transcript, windows_to_android)
            state.windows_message_visible_count = count
            if count > 1:
                raise LiveChatFailure(
                    "duplicate_message",
                    "Windows->Android message appeared more than once",
                )
            return count == 1

        try:
            wait_until(
                windows_message_visible,
                timeout_s=timeout_s,
                failure_kind="android_message_not_visible",
                message="Android UI transcript did not show the Windows message",
                sleep=sleep,
                monotonic=monotonic,
            )
        except LiveChatFailure as exc:
            if (
                exc.failure_kind == "android_message_not_visible"
                and state.windows_message_visible_count is None
                and state.last_ui_dump_failure
            ):
                raise LiveChatFailure(
                    state.last_ui_dump_failure,
                    "Android UI hierarchy was not acquired while waiting for the Windows message",
                ) from exc
            raise

        require_foreground()
        input_xml = dump_ui("ui_after_windows_message.xml")
        tap_x, tap_y = extract_control_center(
            input_xml, MESSAGE_INPUT_ID, focused_window=state.focused_window or ""
        )
        adb.shell(["input", "tap", str(tap_x), str(tap_y)])
        adb.shell(["input", "keyevent", "123"])
        for _ in range(24):
            adb.shell(["input", "keyevent", "67"])
        adb.shell(["input", "text", encode_adb_input_text(android_to_windows)])
        typed_xml = dump_ui("ui_after_android_message.xml")
        typed = extract_control_text(
            typed_xml, MESSAGE_INPUT_ID, focused_window=state.focused_window or ""
        )
        if typed != android_to_windows:
            raise LiveChatFailure(
                "android_input_failed",
                f"message_input text {typed!r} != {android_to_windows!r}",
            )
        send_x, send_y = extract_control_center(
            typed_xml, SEND_ID, focused_window=state.focused_window or ""
        )
        adb.shell(["input", "tap", str(send_x), str(send_y)])

        def android_committed() -> bool:
            windows_alive()
            text = logcat_text()
            if has_fatal_android_error(text):
                raise LiveChatFailure("fatal_android_error", "fatal Android error during Android submit")
            marker = f"MESSAGE_COMMITTED text={android_to_windows}"
            if marker in text:
                state.android_message_committed = True
                return True
            return False

        wait_until(
            android_committed,
            timeout_s=timeout_s,
            failure_kind="android_submit_failed",
            message="Android MESSAGE_COMMITTED not observed",
            sleep=sleep,
            monotonic=monotonic,
        )
        require_foreground()
        dump_ui("ui_after_android_message.xml")

        def windows_saw_android() -> bool:
            windows_alive()
            records = iter_jsonl_records(windows_jsonl)
            event_id = unique_message_visible_id(records, android_to_windows)
            if event_id is not None:
                state.android_message_visible_event_obj_id = event_id
                return True
            return False

        wait_until(
            windows_saw_android,
            timeout_s=timeout_s,
            failure_kind="windows_message_not_visible",
            message="Windows message_visible not observed for Android text",
            sleep=sleep,
            monotonic=monotonic,
        )

        cleanup()
        if not state.verbose_restored:
            return fail(
                "android_verbose_property_cleanup_failed",
                "debug.apptraverse.verbose_log was not restored to 0",
            )
        result = compact_result(
            run_id=run_id,
            status="ok",
            duration_ms=duration_ms(),
            failure_kind=None,
            first_error=None,
            android=snapshot_android(state, serial),
            windows=snapshot_windows(state),
            messages=snapshot_messages(state),
        )
        reject_absolute_paths(result)
        atomic_write_json(artifact_dir / "result.json", result)
        return result
    except LiveChatFailure as exc:
        extras = exc.extras or {}
        if extras.get("focused_window"):
            state.focused_window = extras["focused_window"]
        if "requested_resource_id" in extras:
            state.requested_resource_id = extras["requested_resource_id"]
        if "observed_resource_ids" in extras:
            state.observed_resource_ids = extras["observed_resource_ids"]
        cleanup()
        kind = exc.failure_kind
        if not state.verbose_restored and kind != "windows_executable_missing":
            if adb is not None:
                kind = (
                    exc.failure_kind
                    if state.verbose_restored
                    else (
                        exc.failure_kind
                        if exc.failure_kind == "android_verbose_property_cleanup_failed"
                        else exc.failure_kind
                    )
                )
            if adb is not None and not state.verbose_restored:
                # Prefer the original scenario failure; cleanup failure only when
                # restore itself is the reported error after an otherwise complete run.
                pass
        return fail(kind, exc.first_error)
    except subprocess.TimeoutExpired as exc:
        cleanup()
        return fail("scenario_timeout", str(exc))
    except Exception as exc:  # noqa: BLE001
        cleanup()
        return fail("scenario_timeout", str(exc))


def run_ui_dump_preflight(
    *,
    source_dir: Path,
    windows_exe: Path,
    serial: str,
    adb_path: Path | None = None,
    run: Callable[..., subprocess.CompletedProcess[str]] = subprocess.run,
    sleep: Callable[[float], None] = time.sleep,
    monotonic: Callable[[], float] = time.monotonic,
    env: dict[str, str] | None = None,
) -> dict[str, Any]:
    del env
    started = monotonic()
    run_id = new_run_id()
    artifact_dir = source_dir / ".artifacts" / ARTIFACT_PREFIX / f"{run_id}-preflight"
    android_dir = artifact_dir / "android"
    android_dir.mkdir(parents=True, exist_ok=True)
    state = ScenarioState()

    def duration_ms() -> int:
        return int((monotonic() - started) * 1000)

    def fail(kind: str, message: str) -> dict[str, Any]:
        extras = {
            "preflight": True,
            "method": state.last_ui_dump_method,
            "apptraverse_resource_id_found": False,
        }
        android = snapshot_android(state, serial)
        android.update(extras)
        result = compact_result(
            run_id=run_id,
            status="failed",
            duration_ms=duration_ms(),
            failure_kind=kind,
            first_error=message,
            android=android,
            windows=None,
            messages=None,
        )
        atomic_write_json(artifact_dir / "result.json", result)
        return result

    try:
        exe_path = Path(windows_exe)
        if not exe_path.is_absolute():
            exe_path = source_dir / exe_path
        validate_windows_exe(exe_path)
        adb_exe = adb_path or find_adb()
        adb = AdbClient(adb_exe, serial, run=run, command_log=state.command_log)
        devices_out = adb.run(["devices"]).stdout
        select_android_device(parse_adb_devices(devices_out), serial)
        abi = adb.shell_text(["getprop", "ro.product.cpu.abi"])
        state.android_abi = abi
        require_abi(abi)
        state.android_api = adb.shell_text(["getprop", "ro.build.version.sdk"])
        pkg = adb.shell_text(["pm", "path", PACKAGE])
        if "package:" not in pkg:
            raise LiveChatFailure("android_package_missing", f"{PACKAGE} is not installed")
        ensure_main_activity_foreground(adb, state, sleep=sleep, monotonic=monotonic)
        xml_text = dump_ui_hierarchy(
            adb,
            run_id=run_id,
            logical_name="preflight_ui.xml",
            android_dir=android_dir,
            focused_window=state.focused_window or "",
            sleep=sleep,
            state=state,
        )
        root = parse_hierarchy_xml(xml_text)
        found_ids = apptraverse_resource_ids(root)
        if not found_ids:
            raise LiveChatFailure(
                "android_ui_control_missing",
                "preflight XML contained no App Traverse resource IDs",
                extras={
                    "requested_resource_id": f"{PACKAGE}:id/*",
                    "observed_resource_ids": observed_resource_ids(root),
                    "focused_window": state.focused_window or "",
                },
            )
        android = snapshot_android(state, serial)
        android["preflight"] = True
        android["method"] = state.last_ui_dump_method
        android["apptraverse_resource_id_found"] = True
        result = compact_result(
            run_id=run_id,
            status="ok",
            duration_ms=duration_ms(),
            failure_kind=None,
            first_error=None,
            android=android,
            windows=None,
            messages=None,
        )
        reject_absolute_paths(result)
        atomic_write_json(artifact_dir / "result.json", result)
        return result
    except LiveChatFailure as exc:
        extras = exc.extras or {}
        if extras.get("focused_window"):
            state.focused_window = extras["focused_window"]
        if "requested_resource_id" in extras:
            state.requested_resource_id = extras["requested_resource_id"]
        if "observed_resource_ids" in extras:
            state.observed_resource_ids = extras["observed_resource_ids"]
        return fail(exc.failure_kind, exc.first_error)


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Validate one live Windows <-> Android chat")
    parser.add_argument("--serial", required=True)
    parser.add_argument("--windows-exe", required=True)
    parser.add_argument("--source-dir", default=str(repo_root()))
    parser.add_argument("--timeout-seconds", type=float, default=READY_TIMEOUT_S)
    parser.add_argument("--adb", default="")
    parser.add_argument("--preflight", action="store_true")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    if args.preflight:
        result = run_ui_dump_preflight(
            source_dir=Path(args.source_dir),
            windows_exe=Path(args.windows_exe),
            serial=args.serial,
            adb_path=Path(args.adb) if args.adb else None,
        )
    else:
        result = run_windows_android_live_chat(
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
