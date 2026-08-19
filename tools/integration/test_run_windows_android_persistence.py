#!/usr/bin/env python3
"""Unit tests for Windows <-> Android restart persistence. No real apps."""

from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path

from tools.integration import run_windows_android_live_chat as live
from tools.integration import run_windows_android_persistence as harness


def _record(seq: int, event: str, data: dict) -> dict:
    return {
        "schema_version": "apptraverse.runtime_event/1",
        "run_id": "run-1",
        "seq": seq,
        "event": event,
        "platform": "windows",
        "instance": "windows-phase2",
        "pid": 9,
        "t_us": 1,
        "mono_us": 1,
        "data": data,
    }


def _xml(transcript: str = "", input_text: str = "") -> str:
    return f"""<?xml version='1.0' encoding='UTF-8' standalone='yes' ?>
<hierarchy rotation="0">
  <node text="{transcript}" resource-id="{live.TRANSCRIPT_ID}" bounds="[10,10][90,80]" />
  <node text="{input_text}" resource-id="{live.MESSAGE_INPUT_ID}" bounds="[10,90][70,110]" />
  <node text="Send" resource-id="{live.SEND_ID}" bounds="[72,90][110,110]" />
</hierarchy>
"""


class PersistenceHelperTest(unittest.TestCase):
    def test_phase2_argv_omits_peer_and_auto_accept(self) -> None:
        argv = harness.windows_launch_argv(
            Path("win32_single_client_chat.exe"),
            state_dir=Path("state"),
            client_name="windows-android-persist-abc",
            inbox=Path("phase2/commit.inbox"),
            peer=None,
            auto_accept=False,
        )
        self.assertNotIn("--peer", argv)
        self.assertNotIn("--auto-accept-peer", argv)
        self.assertIn("--state-dir", argv)
        self.assertIn("windows-android-persist-abc", argv)

    def test_same_state_and_client_name_across_phases(self) -> None:
        shared = Path("windows/state")
        client = "windows-android-persist-abc"
        phase1 = harness.windows_launch_argv(
            Path("win32_single_client_chat.exe"),
            state_dir=shared,
            client_name=client,
            inbox=Path("phase1/commit.inbox"),
            peer="AND",
            auto_accept=True,
        )
        phase2 = harness.windows_launch_argv(
            Path("win32_single_client_chat.exe"),
            state_dir=shared,
            client_name=client,
            inbox=Path("phase2/commit.inbox"),
            peer=None,
            auto_accept=False,
        )
        self.assertEqual(phase1[phase1.index("--state-dir") + 1], str(shared))
        self.assertEqual(phase2[phase2.index("--state-dir") + 1], str(shared))
        self.assertEqual(phase1[phase1.index("--aether-client-name") + 1], client)
        self.assertEqual(phase2[phase2.index("--aether-client-name") + 1], client)
        self.assertIn("--peer", phase1)
        self.assertNotIn("--peer", phase2)

    def test_stable_uid_accepted(self) -> None:
        harness.require_uid_stable("UID-A", "UID-A", side="android")
        harness.require_uid_stable("UID-W", "UID-W", side="windows")

    def test_changed_uid_rejected(self) -> None:
        with self.assertRaises(harness.PersistenceFailure) as android:
            harness.require_uid_stable("A1", "A2", side="android")
        self.assertEqual(android.exception.failure_kind, "android_uid_changed")
        with self.assertRaises(harness.PersistenceFailure) as windows:
            harness.require_uid_stable("W1", "W2", side="windows")
        self.assertEqual(windows.exception.failure_kind, "windows_uid_changed")

    def test_pid_comparison(self) -> None:
        harness.require_pid_changed(11, 22, side="android")
        harness.require_pid_changed(31, 32, side="windows")
        with self.assertRaises(harness.PersistenceFailure) as android:
            harness.require_pid_changed(11, 11, side="android")
        self.assertEqual(android.exception.failure_kind, "android_pid_not_changed")
        with self.assertRaises(harness.PersistenceFailure) as windows:
            harness.require_pid_changed(7, 7, side="windows")
        self.assertEqual(windows.exception.failure_kind, "windows_pid_not_changed")

    def test_android_history_exact_once(self) -> None:
        log_text = (
            "CHAT_MESSAGE_VISIBLE platform=android text_key=pre_w_to_a_1 t_us=1\n"
            "CHAT_MESSAGE_VISIBLE platform=android text_key=pre_a_to_w_1 t_us=2\n"
            "TRANSCRIPT_PUBLISHED bytes=9 text=Windows: pre_w_to_a_1\n"
        )
        counts = harness.classify_android_history(log_text, ["pre_w_to_a_1", "pre_a_to_w_1"])
        self.assertEqual(counts["pre_w_to_a_1"], 1)
        self.assertEqual(counts["pre_a_to_w_1"], 1)

    def test_visible_marker_count_ignores_transcript_published(self) -> None:
        log_text = "TRANSCRIPT_PUBLISHED bytes=9 text=Windows: pre_w_to_a_1\n"
        self.assertEqual(harness.count_android_visible_markers(log_text, "pre_w_to_a_1"), 0)

    def test_visible_marker_zero_is_not_duplicate(self) -> None:
        self.assertEqual(harness.count_android_visible_markers("", "pre_w_to_a_1"), 0)

    def test_missing_history_classification(self) -> None:
        with self.assertRaises(harness.PersistenceFailure) as ctx:
            harness.classify_android_history("only other text", ["pre_w_to_a_1"])
        self.assertEqual(ctx.exception.failure_kind, "android_history_missing_after_restart")

    def test_duplicate_persisted_message_classification(self) -> None:
        log_text = (
            "CHAT_MESSAGE_VISIBLE platform=android text_key=pre_w_to_a_1 t_us=1\n"
            "CHAT_MESSAGE_VISIBLE platform=android text_key=pre_w_to_a_1 t_us=2\n"
        )
        with self.assertRaises(harness.PersistenceFailure) as ctx:
            harness.classify_android_history(log_text, ["pre_w_to_a_1"])
        self.assertEqual(ctx.exception.failure_kind, "duplicate_persisted_message")

    def test_sync_event_applied_is_diagnostic_only(self) -> None:
        log_text = "SYNC_EVENT_APPLIED packet=3006427504 event=908004890\n"
        self.assertTrue(harness.sync_event_applied_present(log_text, 908004890))
        self.assertTrue(harness.sync_event_applied_present(log_text, "908004890"))
        self.assertFalse(harness.sync_event_applied_present(log_text, 1))

    def test_debug_send_argv_is_explicit_and_single_action(self) -> None:
        argv = harness.debug_send_shell_args("pre_a_to_w_abc")
        self.assertEqual(argv[0], "am")
        self.assertEqual(argv[1], "broadcast")
        self.assertIn("--receiver-foreground", argv)
        self.assertEqual(argv[argv.index("-n") + 1], harness.DEBUG_SEND_RECEIVER)
        self.assertEqual(argv[argv.index("-a") + 1], harness.DEBUG_SEND_ACTION)
        self.assertEqual(argv.count("-a"), 1)
        self.assertNotIn("DEBUG_ADD_PEER", " ".join(argv))
        self.assertEqual(argv[argv.index("--es") + 1], "text")
        self.assertEqual(argv[argv.index("--es") + 2], "pre_a_to_w_abc")

    def test_debug_send_queued_marker_is_not_native_commit(self) -> None:
        queued = "DEBUG_COMMAND_SEND_QUEUED text=pre_a_to_w_1\n"
        self.assertTrue(harness.android_debug_send_queued(queued, "pre_a_to_w_1"))
        self.assertFalse(harness.android_message_committed(queued, "pre_a_to_w_1"))
        committed = "MESSAGE_COMMITTED text=pre_a_to_w_1\n"
        self.assertTrue(harness.android_message_committed(committed, "pre_a_to_w_1"))

    def test_classify_broadcast_result_codes(self) -> None:
        harness.classify_debug_send_broadcast(Completed("Broadcast completed: result=-1\n"))
        with self.assertRaises(harness.PersistenceFailure) as rejected:
            harness.classify_debug_send_broadcast(Completed("Broadcast completed: result=0\n"))
        self.assertEqual(rejected.exception.failure_kind, "android_debug_send_rejected")
        with self.assertRaises(harness.PersistenceFailure) as missing:
            harness.classify_debug_send_broadcast(Completed("Error: unknown component\n"))
        self.assertEqual(missing.exception.failure_kind, "android_debug_send_receiver_missing")
        with self.assertRaises(harness.PersistenceFailure) as failed:
            harness.classify_debug_send_broadcast(Completed("Broadcast completed: result=2\n"))
        self.assertEqual(failed.exception.failure_kind, "android_debug_send_failed")

    def test_empty_and_oversized_debug_send_rejected(self) -> None:
        class DummyAdb:
            def shell(self, args, timeout=30.0):
                raise AssertionError("broadcast must not run for rejected text")

        with self.assertRaises(harness.PersistenceFailure) as empty:
            harness.invoke_android_debug_send(DummyAdb(), "")
        self.assertEqual(empty.exception.failure_kind, "android_debug_send_rejected")
        with self.assertRaises(harness.PersistenceFailure) as oversized:
            harness.invoke_android_debug_send(DummyAdb(), "x" * (harness.DEBUG_SEND_MAX_CHARS + 1))
        self.assertEqual(oversized.exception.failure_kind, "android_debug_send_rejected")

    def test_windows_persisted_event_obj_id_check(self) -> None:
        records = [
            _record(1, "message_visible", {"text": "pre_a_to_w_1", "event_obj_id": 77}),
            _record(2, "message_visible", {"text": "pre_a_to_w_1", "event_obj_id": 77}),
            _record(3, "message_visible", {"text": "pre_w_to_a_1", "event_obj_id": 88}),
        ]
        found = harness.persisted_windows_event_ids(records, ["pre_w_to_a_1", "pre_a_to_w_1"])
        self.assertEqual(found["pre_a_to_w_1"], 77)
        self.assertEqual(found["pre_w_to_a_1"], 88)
        harness.require_event_ids_unchanged({"pre_a_to_w_1": 77}, found)

    def test_windows_missing_history_classification(self) -> None:
        with self.assertRaises(harness.PersistenceFailure) as ctx:
            harness.persisted_windows_event_ids([], ["pre_w_to_a_1"])
        self.assertEqual(ctx.exception.failure_kind, "windows_history_missing_after_restart")

    def test_compact_result_excludes_raw_artifacts(self) -> None:
        result = harness.compact_persistence_result(
            run_id="20260819-000000-abc123",
            status="ok",
            duration_ms=9,
            failure_kind=None,
            first_error=None,
            android={
                "serial": "emulator-5554",
                "uid_phase1": "A",
                "uid_phase2": "A",
                "pid_phase1": 1,
                "pid_phase2": 2,
                "uid_stable": True,
                "pid_changed": True,
                "phase1_history_count": 2,
                "phase2_history_count": 4,
            },
            windows={
                "uid_phase1": "W",
                "uid_phase2": "W",
                "pid_phase1": 3,
                "pid_phase2": 4,
                "uid_stable": True,
                "pid_changed": True,
                "phase2_peer_argument_used": False,
            },
            phase1={"bidirectional_delivery": True, "history_verified": True},
            phase2={"bidirectional_delivery": True, "history_verified": True},
            cleanup={"verbose_property_restored": True, "app_data_preserved": True},
        )
        blob = json.dumps(result)
        self.assertEqual(result["schema_version"], harness.RESULT_SCHEMA_VERSION)
        self.assertEqual(result["artifact_id"], "windows-android-persistence/20260819-000000-abc123")
        self.assertNotIn("<hierarchy", blob)
        self.assertNotIn("runtime.jsonl", blob)
        self.assertNotIn("logcat.txt", blob)
        self.assertFalse(live.result_contains_forbidden_payload(result))
        live.reject_absolute_paths(result)
        self.assertEqual(result["android_send_method"], "debug_broadcast_receiver")
        self.assertEqual(result["uiautomator_command_count"], 0)
        self.assertEqual(result["adb_input_command_count"], 0)


class FakeProc:
    def __init__(self, pid: int) -> None:
        self.pid = pid
        self._exit = None

    def poll(self):
        return self._exit


class Completed:
    def __init__(self, stdout: str = "", stderr: str = "", returncode: int = 0) -> None:
        self.stdout = stdout
        self.stderr = stderr
        self.returncode = returncode


def argv_has(argv: list[str], *parts: str) -> bool:
    for index in range(len(argv) - len(parts) + 1):
        if tuple(argv[index : index + len(parts)]) == parts:
            return True
    return False


class FakePersistWorld:
    def __init__(self, run_id: str) -> None:
        self.run_id = run_id
        self.calls: list[list[str]] = []
        self.shell_flags: list[bool] = []
        self.popen_shell: list[bool] = []
        self.windows_argvs: list[list[str]] = []
        self.verbose = "0"
        self.input_text = ""
        self.android_pid = 1001
        self.android_launches = 0
        self.force_stops = 0
        self.transcript = ""
        self.jsonl_by_instance: dict[str, Path] = {}
        self.inbox_by_instance: dict[str, Path] = {}
        self.logcat_paths: list[Path] = []
        self.windows_procs = [FakeProc(501), FakeProc(502)]
        self.logcat_proc = FakeProc(401)
        self.terminated: list[int] = []
        self.dead_pids: set[int] = set()

    def run(self, argv, capture_output=True, text=True, encoding=None, errors=None, shell=False, timeout=None):
        self.shell_flags.append(shell)
        if shell:
            raise AssertionError("shell must be False")
        self.calls.append(list(argv))
        joined = " ".join(argv)
        if "pm clear" in joined or "uninstall" in joined or "gradle" in joined.lower() or "cmake" in joined.lower():
            raise AssertionError(f"forbidden command {joined}")
        if argv_has(argv, "devices"):
            return Completed("List of devices attached\nemulator-5554\tdevice\n")
        if argv_has(argv, "getprop", "ro.product.cpu.abi"):
            return Completed("x86_64\n")
        if argv_has(argv, "pm", "path"):
            return Completed("package:/data/app/base.apk\n")
        if argv_has(argv, "getprop", live.VERBOSE_PROPERTY):
            return Completed(self.verbose + "\n")
        if argv_has(argv, "setprop", live.VERBOSE_PROPERTY):
            self.verbose = argv[argv.index(live.VERBOSE_PROPERTY) + 1]
            return Completed()
        if argv_has(argv, "logcat", "-c"):
            return Completed()
        if argv_has(argv, "am", "force-stop"):
            self.force_stops += 1
            self.android_pid = None
            return Completed()
        if argv_has(argv, "am", "start"):
            self.android_launches += 1
            was_stopped = self.android_pid is None
            self.android_pid = 1001 if self.android_launches == 1 else 2002
            if was_stopped and self.android_launches >= 2:
                short = live.short_run_id(self.run_id)
                self._append_visible(f"pre_w_to_a_{short}")
                self._append_visible(f"pre_a_to_w_{short}")
            return Completed("Starting: Intent\n")
        if argv_has(argv, "pidof"):
            return Completed("" if self.android_pid is None else f"{self.android_pid}\n")
        if argv_has(argv, "dumpsys", "activity", "activities"):
            return Completed(
                "topResumedActivity=ActivityRecord{0 u0 com.apptraverse.singleclientchat/.MainActivity t1}\n"
            )
        if argv_has(argv, "dumpsys", "window", "windows"):
            return Completed("mCurrentFocus=Window{0 u0 com.apptraverse.singleclientchat/.MainActivity}\n")
        if "uiautomator" in argv:
            raise AssertionError("uiautomator is forbidden in C2")
        if argv_has(argv, "input", "tap") or argv_has(argv, "input", "text") or argv_has(argv, "input", "keyevent"):
            raise AssertionError("adb input is forbidden in C2")
        if argv_has(argv, "am", "broadcast"):
            joined = " ".join(argv)
            if harness.DEBUG_SEND_RECEIVER not in argv:
                return Completed("Broadcast completed: result=0\n")
            if harness.DEBUG_SEND_ACTION not in argv:
                return Completed("Broadcast completed: result=0\n")
            if "--es" not in argv:
                return Completed("Broadcast completed: result=0\n")
            text = argv[argv.index("--es") + 2]
            if not text or not text.strip() or len(text) > harness.DEBUG_SEND_MAX_CHARS:
                return Completed("Broadcast completed: result=0\n")
            self.input_text = text
            self._commit_android()
            return Completed("Broadcast completed: result=-1\n")
        return Completed()

    def popen(self, argv, stdout=None, stderr=None, env=None, cwd=None, shell=False):
        self.popen_shell.append(shell)
        if shell:
            raise AssertionError("shell must be False")
        self.calls.append(list(argv))
        if "logcat" in argv:
            if stdout is not None:
                stdout.write(
                    b"AETHER_CLIENT_READY platform=android uid=AND-UID\n"
                    b"CHAT_SYNC_CONTROLLER_READY\n"
                    b"CHAT_PEER_ADDED uid=WIN-UID session_state_id=1\n"
                )
                stdout.flush()
            return self.logcat_proc
        env = env or {}
        if "APPTRAVERSE_VERBOSE_LOG" in env:
            raise AssertionError("APPTRAVERSE_VERBOSE_LOG must remain unset")
        instance = env["APPTRAVERSE_INSTANCE"]
        jsonl = Path(env["APPTRAVERSE_RUNTIME_JSONL"])
        self.jsonl_by_instance[instance] = jsonl
        self.windows_argvs.append(list(argv))
        inbox = Path(argv[argv.index("--commit-inbox") + 1])
        self.inbox_by_instance[instance] = inbox
        if instance == "windows-phase1":
            self._write_jsonl(
                jsonl,
                [
                    _record(1, "runtime_started", {"local_uid": "WIN-UID"}),
                    _record(2, "peer_add", {"peer": "AND-UID", "accepted": True}),
                ],
            )
            return self.windows_procs[0]
        self._write_jsonl(
            jsonl,
            [
                _record(1, "runtime_started", {"local_uid": "WIN-UID"}),
                _record(2, "message_visible", {"text": f"pre_w_to_a_{live.short_run_id(self.run_id)}", "event_obj_id": 101}),
                _record(3, "message_visible", {"text": f"pre_a_to_w_{live.short_run_id(self.run_id)}", "event_obj_id": 202}),
            ],
        )
        return self.windows_procs[1]

    def sleep(self, _dt: float) -> None:
        for instance, inbox in list(self.inbox_by_instance.items()):
            if inbox.exists() and instance in self.jsonl_by_instance:
                text = inbox.read_text(encoding="utf-8").strip()
                inbox.unlink()
                jsonl = self.jsonl_by_instance[instance]
                records = live.iter_jsonl_records(jsonl)
                records.append(
                    _record(10, "text_submit", {"text": text, "accepted": True, "event_obj_id": 101 if "pre_w" in text else 303})
                )
                if "pre_w" in text or "post_w" in text:
                    records.append(_record(11, "message_visible", {"text": text, "event_obj_id": 101 if "pre_w" in text else 303}))
                    self.transcript = (self.transcript + "\n" + text).strip()
                    event_id = 101 if "pre_w" in text else 303
                    self._append_visible(text, event_id=event_id)
                self._write_jsonl(jsonl, records)

    def terminate(self, pid: int) -> None:
        self.terminated.append(pid)
        self.dead_pids.add(pid)
        for proc in self.windows_procs:
            if proc.pid == pid:
                proc._exit = 1

    def pid_is_running(self, pid: int | None) -> bool:
        return pid is not None and pid not in self.dead_pids

    def _current_logcat(self) -> Path | None:
        existing = [path for path in self.logcat_paths if path.exists()]
        return existing[-1] if existing else None

    def _append_logcat(self, line: str) -> None:
        path = self._current_logcat()
        if path is None:
            return
        path.parent.mkdir(parents=True, exist_ok=True)
        with path.open("a", encoding="utf-8") as handle:
            handle.write(line + "\n")

    def _append_visible(self, message: str, event_id: object | None = None) -> None:
        self._append_logcat(f"CHAT_MESSAGE_VISIBLE platform=android text_key={message} t_us=1")
        if event_id is not None:
            self._append_logcat(f"SYNC_EVENT_APPLIED packet=1 event={event_id}")

    def _commit_android(self) -> None:
        short = live.short_run_id(self.run_id)
        message = self.input_text if self.input_text else f"pre_a_to_w_{short}"
        self.transcript = (self.transcript + "\n" + message).strip()
        path = self._current_logcat()
        if path is not None:
            with path.open("a", encoding="utf-8") as handle:
                handle.write(f"DEBUG_COMMAND_SEND_QUEUED text={message}\n")
                handle.write(f"CHAT_MESSAGE_COMMITTED platform=android event=9 text_key={message}\n")
                handle.write(f"MESSAGE_COMMITTED text={message}\n")
        self._append_visible(message)
        instance = "windows-phase2" if f"post_" in message else "windows-phase1"
        jsonl = self.jsonl_by_instance.get(instance)
        if jsonl is not None:
            records = live.iter_jsonl_records(jsonl)
            event_id = 202 if "pre_a" in message else 404
            records.append(_record(20, "message_visible", {"text": message, "event_obj_id": event_id}))
            self._write_jsonl(jsonl, records)

    @staticmethod
    def _write_jsonl(path: Path, records: list[dict]) -> None:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text("".join(json.dumps(item) + "\n" for item in records), encoding="utf-8")


class PersistenceRunnerTest(unittest.TestCase):
    def test_phase2_delivery_without_add_peer_and_cleanup(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            exe = root / "win32_single_client_chat.exe"
            exe.write_bytes(b"dummy")
            adb = root / "adb.exe"
            adb.write_bytes(b"dummy")
            run_id = "20260819-000000-abc123"
            world = FakePersistWorld(run_id)
            artifact = root / ".artifacts" / "windows-android-persistence" / run_id
            world.logcat_paths = [
                artifact / "android" / "phase1" / "logcat.txt",
                artifact / "android" / "phase2" / "logcat.txt",
            ]
            result = harness.run_windows_android_persistence(
                source_dir=root,
                windows_exe=exe,
                serial="emulator-5554",
                timeout_s=5.0,
                run_id=run_id,
                adb_path=adb,
                popen=world.popen,
                run=world.run,
                sleep=world.sleep,
                monotonic=lambda: 0.0,
                terminate=world.terminate,
                pid_is_running=world.pid_is_running,
                env={"PATH": "x", "APPTRAVERSE_VERBOSE_LOG": "1"},
            )
            self.assertEqual(result["status"], "ok", result)
            self.assertEqual(len(world.windows_argvs), 2)
            self.assertIn("--peer", world.windows_argvs[0])
            self.assertNotIn("--peer", world.windows_argvs[1])
            self.assertNotIn("--auto-accept-peer", world.windows_argvs[1])
            state1 = world.windows_argvs[0][world.windows_argvs[0].index("--state-dir") + 1]
            state2 = world.windows_argvs[1][world.windows_argvs[1].index("--state-dir") + 1]
            self.assertEqual(state1, state2)
            self.assertFalse(result["windows"]["phase2_peer_argument_used"])
            self.assertTrue(result["android"]["uid_stable"])
            self.assertTrue(result["windows"]["uid_stable"])
            self.assertTrue(result["android"]["pid_changed"])
            self.assertTrue(result["windows"]["pid_changed"])
            self.assertTrue(result["phase2"]["bidirectional_delivery"])
            self.assertEqual(world.verbose, "0")
            self.assertTrue(result["cleanup"]["verbose_property_restored"])
            self.assertTrue(result["cleanup"]["app_data_preserved"])
            joined = "\n".join(" ".join(call) for call in world.calls)
            self.assertNotIn("pm clear", joined)
            self.assertNotIn("uninstall", joined)
            self.assertNotIn("gradle", joined.lower())
            self.assertTrue(all(flag is False for flag in world.shell_flags))
            self.assertTrue(all(flag is False for flag in world.popen_shell))
            blob = json.dumps(result)
            self.assertNotIn("<hierarchy", blob)
            self.assertNotIn("add_participant", joined)
            self.assertNotIn("uiautomator", joined)
            self.assertNotIn("input tap", joined)
            self.assertNotIn("input text", joined)
            self.assertNotIn("input keyevent", joined)
            self.assertEqual(result["android_send_method"], "debug_broadcast_receiver")
            self.assertTrue(result["debug_receiver_phase1_accepted"])
            self.assertTrue(result["debug_receiver_phase2_accepted"])
            self.assertEqual(result["uiautomator_command_count"], 0)
            self.assertEqual(result["adb_input_command_count"], 0)
            broadcasts = [call for call in world.calls if "broadcast" in call]
            self.assertEqual(len(broadcasts), 2)
            for call in broadcasts:
                self.assertIn(harness.DEBUG_SEND_ACTION, call)
                self.assertIn(harness.DEBUG_SEND_RECEIVER, call)
                self.assertIn("--receiver-foreground", call)
            self.assertEqual(result["phase1"].get("sync_event_applied"), {"event": 101})
            self.assertEqual(result["phase2"]["history_count"], 4)

    def test_missing_debug_receiver_is_classified(self) -> None:
        class MissingReceiverWorld(FakePersistWorld):
            def run(self, argv, capture_output=True, text=True, encoding=None, errors=None, shell=False, timeout=None):
                if argv_has(argv, "am", "broadcast"):
                    self.shell_flags.append(shell)
                    self.calls.append(list(argv))
                    return Completed("Error: unknown component: DebugCommandReceiver\n")
                return super().run(
                    argv,
                    capture_output=capture_output,
                    text=text,
                    encoding=encoding,
                    errors=errors,
                    shell=shell,
                    timeout=timeout,
                )

        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            exe = root / "win32_single_client_chat.exe"
            exe.write_bytes(b"dummy")
            adb = root / "adb.exe"
            adb.write_bytes(b"dummy")
            run_id = "20260819-000000-recv01"
            world = MissingReceiverWorld(run_id)
            artifact = root / ".artifacts" / "windows-android-persistence" / run_id
            world.logcat_paths = [
                artifact / "android" / "phase1" / "logcat.txt",
                artifact / "android" / "phase2" / "logcat.txt",
            ]
            result = harness.run_windows_android_persistence(
                source_dir=root,
                windows_exe=exe,
                serial="emulator-5554",
                timeout_s=5.0,
                run_id=run_id,
                adb_path=adb,
                popen=world.popen,
                run=world.run,
                sleep=world.sleep,
                monotonic=lambda: 0.0,
                terminate=world.terminate,
                pid_is_running=world.pid_is_running,
                env={"PATH": "x"},
            )
            self.assertEqual(result["status"], "failed")
            self.assertEqual(result["failure_kind"], "android_debug_send_receiver_missing")
            self.assertNotEqual(result["failure_kind"], "android_ui_dump_failed")
            self.assertNotEqual(result["failure_kind"], "phase1_delivery_failed")

    def test_missing_windows_exe(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            result = harness.run_windows_android_persistence(
                source_dir=Path(tmp),
                windows_exe=Path("missing.exe"),
                serial="emulator-5554",
                timeout_s=1.0,
                run_id="20260819-000000-miss01",
            )
            self.assertEqual(result["status"], "failed")
            self.assertEqual(result["failure_kind"], "windows_executable_missing")


class DebugReceiverSourceTest(unittest.TestCase):
    _repo = Path(__file__).resolve().parents[2]
    _debug_java = (
        _repo
        / "examples"
        / "single_client_chat"
        / "android"
        / "app"
        / "src"
        / "debug"
        / "java"
        / "com"
        / "apptraverse"
        / "singleclientchat"
        / "DebugCommandReceiver.java"
    )
    _debug_manifest = (
        _repo
        / "examples"
        / "single_client_chat"
        / "android"
        / "app"
        / "src"
        / "debug"
        / "AndroidManifest.xml"
    )
    _main_manifest = (
        _repo
        / "examples"
        / "single_client_chat"
        / "android"
        / "app"
        / "src"
        / "main"
        / "AndroidManifest.xml"
    )
    _main_java_dir = (
        _repo
        / "examples"
        / "single_client_chat"
        / "android"
        / "app"
        / "src"
        / "main"
        / "java"
        / "com"
        / "apptraverse"
        / "singleclientchat"
    )

    def test_receiver_exists_only_under_debug(self) -> None:
        self.assertTrue(self._debug_java.is_file())
        self.assertFalse((self._main_java_dir / "DebugCommandReceiver.java").exists())

    def test_main_manifest_unchanged(self) -> None:
        main_text = self._main_manifest.read_text(encoding="utf-8")
        self.assertNotIn("DebugCommandReceiver", main_text)
        self.assertNotIn("DEBUG_SEND", main_text)
        debug_text = self._debug_manifest.read_text(encoding="utf-8")
        self.assertIn("DebugCommandReceiver", debug_text)
        self.assertIn("com.apptraverse.singleclientchat.DEBUG_SEND", debug_text)
        self.assertIn('android:exported="true"', debug_text)

    def test_receiver_calls_application_send_only(self) -> None:
        source = self._debug_java.read_text(encoding="utf-8")
        self.assertIn("ACTION.equals(intent.getAction())", source)
        self.assertIn("getStringExtra(EXTRA_TEXT)", source)
        self.assertIn("text.trim()", source)
        self.assertIn("MAX_TEXT_CHARS", source)
        self.assertIn("((SingleClientChatApplication) appContext).send(text)", source)
        self.assertIn("DEBUG_COMMAND_SEND_QUEUED text=", source)
        self.assertNotIn("nativeQueueSend", source)
        self.assertNotIn("addPeer", source)
        self.assertNotIn("Runtime.getRuntime", source)
        self.assertNotIn("ProcessBuilder", source)
        self.assertNotIn("exec(", source)


if __name__ == "__main__":
    unittest.main()
