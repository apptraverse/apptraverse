#!/usr/bin/env python3
"""Unit tests for the Windows <-> Android live chat runner. No real apps."""

from __future__ import annotations

import json
import subprocess
import tempfile
import unittest
from pathlib import Path

from tools.integration import run_windows_android_live_chat as harness


def _xml_hierarchy(
    *,
    transcript: str = "",
    input_text: str = "",
    transcript_bounds: str = "[10,10][90,80]",
    input_bounds: str = "[10,90][70,110]",
    send_bounds: str = "[72,90][110,110]",
) -> str:
    return f"""<?xml version='1.0' encoding='UTF-8' standalone='yes' ?>
<hierarchy rotation="0">
  <node index="0" text="" resource-id="" class="android.widget.FrameLayout" bounds="[0,0][120,160]">
    <node index="0" text="{transcript}" resource-id="{harness.TRANSCRIPT_ID}" class="android.widget.TextView" bounds="{transcript_bounds}" />
    <node index="1" text="{input_text}" resource-id="{harness.MESSAGE_INPUT_ID}" class="android.widget.EditText" bounds="{input_bounds}" />
    <node index="2" text="Send" resource-id="{harness.SEND_ID}" class="android.widget.Button" bounds="{send_bounds}" />
  </node>
</hierarchy>
"""


def _jsonl_record(seq: int, event: str, data: dict) -> dict:
    return {
        "schema_version": "apptraverse.runtime_event/1",
        "run_id": "run-1",
        "seq": seq,
        "event": event,
        "platform": "windows",
        "instance": "windows",
        "pid": 9,
        "t_us": 1,
        "mono_us": 1,
        "data": data,
    }


class FakeProc:
    def __init__(self, pid: int, exit_code: int | None = None) -> None:
        self.pid = pid
        self._exit_code = exit_code

    def poll(self) -> int | None:
        return self._exit_code


class Completed:
    def __init__(self, stdout: str = "", stderr: str = "", returncode: int = 0) -> None:
        self.stdout = stdout
        self.stderr = stderr
        self.returncode = returncode


class LiveChatHelperTest(unittest.TestCase):
    def test_adb_device_and_abi_selection(self) -> None:
        devices = harness.parse_adb_devices(
            "List of devices attached\nemulator-5554\tdevice\nemulator-5556\toffline\n"
        )
        selected = harness.select_android_device(devices, "emulator-5554")
        self.assertEqual(selected["serial"], "emulator-5554")
        harness.require_abi("x86_64")
        with self.assertRaises(harness.LiveChatFailure) as missing:
            harness.select_android_device(devices, "emulator-5556")
        self.assertEqual(missing.exception.failure_kind, "android_device_unavailable")
        with self.assertRaises(harness.LiveChatFailure) as abi:
            harness.require_abi("arm64-v8a")
        self.assertEqual(abi.exception.failure_kind, "android_device_unavailable")

    def test_android_uid_marker_parsing(self) -> None:
        log = "noise\nAETHER_CLIENT_READY platform=android uid=3.14.15-android\nCHAT_SYNC_CONTROLLER_READY\n"
        self.assertEqual(harness.parse_android_uid(log), "3.14.15-android")
        self.assertIsNone(harness.parse_android_uid("AETHER_CLIENT_READY platform=windows uid=nope"))

    def test_windows_runtime_started_uid_extraction(self) -> None:
        records = [_jsonl_record(1, "runtime_started", {"local_uid": "WIN-UID-9"})]
        self.assertEqual(harness.extract_runtime_started_uid(records), "WIN-UID-9")
        self.assertIsNone(harness.extract_runtime_started_uid([]))

    def test_xml_transcript_extraction(self) -> None:
        xml_text = _xml_hierarchy(transcript="hello from windows")
        self.assertEqual(
            harness.extract_control_text(xml_text, harness.TRANSCRIPT_ID),
            "hello from windows",
        )

    def test_xml_entity_decoding(self) -> None:
        xml_text = _xml_hierarchy(transcript="a &amp; b &lt; c")
        self.assertEqual(
            harness.extract_control_text(xml_text, harness.TRANSCRIPT_ID),
            "a & b < c",
        )

    def test_control_bounds_parsing(self) -> None:
        self.assertEqual(harness.parse_bounds("[10,20][30,40]"), (10, 20, 30, 40))
        self.assertEqual(harness.bounds_center((10, 20, 30, 40)), (20, 30))
        xml_text = _xml_hierarchy(input_bounds="[10,90][70,110]")
        self.assertEqual(
            harness.extract_control_center(xml_text, harness.MESSAGE_INPUT_ID),
            (40, 100),
        )
        with self.assertRaises(harness.LiveChatFailure) as ctx:
            harness.parse_bounds("nope")
        self.assertEqual(ctx.exception.failure_kind, "android_ui_control_missing")

    def test_exact_input_verification(self) -> None:
        xml_text = _xml_hierarchy(input_text="a_to_w_abc123")
        self.assertEqual(
            harness.extract_control_text(xml_text, harness.MESSAGE_INPUT_ID),
            "a_to_w_abc123",
        )
        self.assertNotEqual(
            harness.extract_control_text(xml_text, harness.MESSAGE_INPUT_ID),
            "a_to_w_abc123 ",
        )

    def test_atomic_inbox_write(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "commit.inbox"
            harness.atomic_write_inbox(path, "w_to_a_abc123")
            self.assertEqual(path.read_text(encoding="utf-8"), "w_to_a_abc123\n")
            self.assertFalse(path.with_name("commit.inbox.tmp").exists())

    def test_android_transcript_exact_once_count(self) -> None:
        self.assertEqual(harness.count_transcript_message("x w_to_a_1 y", "w_to_a_1"), 1)
        self.assertEqual(harness.count_transcript_message("w_to_a_1\nw_to_a_1", "w_to_a_1"), 2)
        self.assertEqual(harness.count_transcript_message("nope", "w_to_a_1"), 0)

    def test_windows_message_visible_unique_event_obj_id(self) -> None:
        records = [
            _jsonl_record(4, "message_visible", {"text": "a_to_w_1", "event_obj_id": 77}),
            _jsonl_record(5, "message_visible", {"text": "a_to_w_1", "event_obj_id": 77}),
        ]
        self.assertEqual(harness.unique_message_visible_id(records, "a_to_w_1"), 77)
        duplicates = records + [
            _jsonl_record(6, "message_visible", {"text": "a_to_w_1", "event_obj_id": 78})
        ]
        with self.assertRaises(harness.LiveChatFailure) as ctx:
            harness.unique_message_visible_id(duplicates, "a_to_w_1")
        self.assertEqual(ctx.exception.failure_kind, "duplicate_message")
        self.assertIsNone(harness.unique_message_visible_id(records, "missing"))

    def test_compact_result_excludes_full_logs(self) -> None:
        result = harness.compact_result(
            run_id="20260818-000000-abc123",
            status="ok",
            duration_ms=12,
            failure_kind=None,
            first_error=None,
            android={
                "serial": "emulator-5554",
                "pid": 1,
                "uid": "AND",
                "api": "34",
                "abi": "x86_64",
                "activity_resumed": True,
                "windows_message_visible_count": 1,
                "android_message_committed": True,
            },
            windows={
                "pid": 2,
                "uid": "WIN",
                "peer_uid": "AND",
                "local_submit_event_obj_id": 10,
                "android_message_visible_event_obj_id": 11,
            },
            messages={
                "windows_to_android": "w_to_a_abc123",
                "android_to_windows": "a_to_w_abc123",
            },
        )
        blob = json.dumps(result)
        self.assertEqual(result["schema_version"], harness.RESULT_SCHEMA_VERSION)
        self.assertEqual(result["artifact_id"], "windows-android-live/20260818-000000-abc123")
        self.assertNotIn("<hierarchy", blob)
        self.assertNotIn("runtime.jsonl", blob)
        self.assertNotIn("stdout.log", blob)
        self.assertNotIn("AETHER_CLIENT_READY", blob)
        self.assertFalse(harness.result_contains_forbidden_payload(result))
        harness.reject_absolute_paths(result)

    def test_activity_resumed_ignores_background_task(self) -> None:
        launcher = (
            "topResumedActivity=ActivityRecord{a u0 com.google.android.apps.nexuslauncher/.NexusLauncherActivity t1}\n"
            "mLastFocusedRootTask=Task{1 type=standard A=10193:com.apptraverse.singleclientchat}\n"
        )
        self.assertFalse(harness.activity_resumed(launcher))
        resumed = (
            "topResumedActivity=ActivityRecord{b u0 com.apptraverse.singleclientchat/.MainActivity t2}\n"
        )
        self.assertTrue(harness.activity_resumed(resumed))

    def test_windows_env_leaves_verbose_unset(self) -> None:
        env = harness.windows_env(
            {"APPTRAVERSE_VERBOSE_LOG": "1", "PATH": "x"},
            run_id="r1",
            jsonl_path=Path("runtime.jsonl"),
        )
        self.assertNotIn("APPTRAVERSE_VERBOSE_LOG", env)
        self.assertEqual(env["APPTRAVERSE_INSTANCE"], "windows")
        self.assertEqual(env["APPTRAVERSE_RUN_ID"], "r1")


class FakeAdbWorld:
    def __init__(self, *, xml_transcript: str, run_id: str) -> None:
        self.calls: list[list[str]] = []
        self.shell_flags: list[bool] = []
        self.verbose = "0"
        self.input_text = ""
        self.send_tapped = False
        self.xml_transcript = xml_transcript
        self.run_id = run_id
        self.jsonl_path: Path | None = None
        self.logcat_path: Path | None = None
        self.inbox: Path | None = None
        self.windows_proc = FakeProc(400)
        self.logcat_proc = FakeProc(401)
        self.popen_shell: list[bool] = []
        self.terminated: list[int] = []

    def run(self, argv, capture_output=True, text=True, encoding=None, errors=None, shell=False, timeout=None):
        self.shell_flags.append(shell)
        if shell:
            raise AssertionError("shell must be False")
        self.calls.append(list(argv))
        joined = " ".join(argv)
        if argv_has(argv, "devices"):
            return Completed("List of devices attached\nemulator-5554\tdevice\n")
        if argv_has(argv, "getprop", "ro.product.cpu.abi"):
            return Completed("x86_64\n")
        if argv_has(argv, "getprop", "ro.build.version.sdk"):
            return Completed("34\n")
        if argv_has(argv, "pm", "path"):
            self.assert_not_clear(argv)
            return Completed("package:/data/app/com.apptraverse.singleclientchat/base.apk\n")
        if argv_has(argv, "getprop", harness.VERBOSE_PROPERTY):
            return Completed(self.verbose + "\n")
        if argv_has(argv, "setprop", harness.VERBOSE_PROPERTY):
            self.verbose = argv[argv.index(harness.VERBOSE_PROPERTY) + 1]
            return Completed()
        if argv_has(argv, "logcat", "-c"):
            return Completed()
        if argv_has(argv, "am", "force-stop"):
            return Completed()
        if argv_has(argv, "am", "start"):
            return Completed("Starting: Intent { cmp=com.apptraverse.singleclientchat/.MainActivity }\n")
        if argv_has(argv, "pidof"):
            return Completed("5428\n")
        if argv_has(argv, "dumpsys", "activity", "activities"):
            return Completed(
                "topResumedActivity=ActivityRecord{0 u0 com.apptraverse.singleclientchat/.MainActivity t1}\n"
            )
        if argv_has(argv, "uiautomator", "dump"):
            return Completed("UI hierchary dumped to: /data/local/tmp/apptraverse_uidump.xml\n")
        if argv_has(argv, "cat", harness.UIDUMP_REMOTE) or argv_has(argv, "cat", "/sdcard/window_dump.xml"):
            return Completed(
                _xml_hierarchy(transcript=self.xml_transcript, input_text=self.input_text)
            )
        if argv_has(argv, "input", "text"):
            self.input_text = argv[-1].replace("%s", " ")
            return Completed()
        if argv_has(argv, "input", "tap"):
            x, y = argv[-2], argv[-1]
            if x == "91" and y == "100":
                self.send_tapped = True
                self._commit_android()
            return Completed()
        if argv_has(argv, "input", "keyevent"):
            if argv[-1] == "66":
                raise AssertionError("KEYCODE_ENTER must not be used to send")
            return Completed()
        if "gradle" in joined.lower() or "gradlew" in joined.lower():
            raise AssertionError("gradle/build command is forbidden")
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
        self.jsonl_path = Path(env["APPTRAVERSE_RUNTIME_JSONL"])
        self._write_jsonl(
            [
                _jsonl_record(1, "runtime_started", {"local_uid": "WIN-UID"}),
                _jsonl_record(2, "peer_add", {"peer": "AND-UID", "accepted": True}),
            ]
        )
        if "--print-aether-uid" in argv:
            raise AssertionError("--print-aether-uid is forbidden in this slice")
        self.inbox = Path(argv[argv.index("--commit-inbox") + 1])
        return self.windows_proc

    def sleep(self, _dt: float) -> None:
        if self.inbox is not None and self.inbox.exists() and self.jsonl_path is not None:
            text = self.inbox.read_text(encoding="utf-8").strip()
            self.inbox.unlink()
            records = harness.iter_jsonl_records(self.jsonl_path)
            records.append(
                _jsonl_record(
                    3,
                    "text_submit",
                    {"text": text, "accepted": True, "event_obj_id": 101},
                )
            )
            self._write_jsonl(records)
            self.xml_transcript = text

    def terminate(self, pid: int) -> None:
        self.terminated.append(pid)

    def _commit_android(self) -> None:
        message = f"a_to_w_{harness.short_run_id(self.run_id)}"
        if self.logcat_path is not None:
            with self.logcat_path.open("a", encoding="utf-8") as handle:
                handle.write(f"MESSAGE_COMMITTED text={message}\n")
        if self.jsonl_path is not None:
            records = harness.iter_jsonl_records(self.jsonl_path)
            records.append(
                _jsonl_record(
                    4,
                    "message_visible",
                    {"text": message, "event_obj_id": 202},
                )
            )
            records.append(
                _jsonl_record(
                    5,
                    "message_visible",
                    {"text": message, "event_obj_id": 202},
                )
            )
            self._write_jsonl(records)

    def _write_jsonl(self, records: list[dict]) -> None:
        assert self.jsonl_path is not None
        self.jsonl_path.parent.mkdir(parents=True, exist_ok=True)
        self.jsonl_path.write_text(
            "".join(json.dumps(item) + "\n" for item in records),
            encoding="utf-8",
        )

    @staticmethod
    def assert_not_clear(argv: list[str]) -> None:
        joined = " ".join(argv)
        if "pm clear" in joined or "uninstall" in joined:
            raise AssertionError("pm clear/uninstall is forbidden")


def argv_has(argv: list[str], *parts: str) -> bool:
    if not parts:
        return False
    for index in range(len(argv) - len(parts) + 1):
        if tuple(argv[index : index + len(parts)]) == parts:
            return True
    return False


class LiveChatRunnerTest(unittest.TestCase):
    def test_cleanup_restores_verbose_property_and_full_runner(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            exe = root / "win32_single_client_chat.exe"
            exe.write_bytes(b"dummy")
            adb = root / "adb.exe"
            adb.write_bytes(b"dummy")
            run_id = "20260818-000000-abc123"
            world = FakeAdbWorld(xml_transcript="", run_id=run_id)
            artifact = root / ".artifacts" / "windows-android-live" / run_id
            world.logcat_path = artifact / "android" / "logcat.txt"
            result = harness.run_windows_android_live_chat(
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
                env={"PATH": "x", "APPTRAVERSE_VERBOSE_LOG": "1"},
            )
            self.assertEqual(result["status"], "ok", result)
            self.assertIsNone(result["failure_kind"])
            self.assertEqual(result["android"]["uid"], "AND-UID")
            self.assertEqual(result["android"]["pid"], 5428)
            self.assertEqual(result["android"]["windows_message_visible_count"], 1)
            self.assertTrue(result["android"]["android_message_committed"])
            self.assertEqual(result["windows"]["uid"], "WIN-UID")
            self.assertEqual(result["windows"]["local_submit_event_obj_id"], 101)
            self.assertEqual(result["windows"]["android_message_visible_event_obj_id"], 202)
            self.assertEqual(result["messages"]["windows_to_android"], "w_to_a_abc123")
            self.assertEqual(result["messages"]["android_to_windows"], "a_to_w_abc123")
            self.assertEqual(world.verbose, "0")
            self.assertIn(400, world.terminated)
            joined = "\n".join(" ".join(call) for call in world.calls)
            self.assertNotIn("pm clear", joined)
            self.assertNotIn("uninstall", joined)
            self.assertNotIn("gradle", joined.lower())
            self.assertNotIn("gradlew", joined.lower())
            self.assertNotIn("cmake --build", joined)
            self.assertNotIn("--print-aether-uid", joined)
            self.assertNotIn(" input keyevent 66", " " + joined)
            self.assertTrue(all(flag is False for flag in world.shell_flags))
            self.assertTrue(all(flag is False for flag in world.popen_shell))
            blob = json.dumps(result)
            self.assertNotIn("<hierarchy", blob)
            self.assertNotIn(str(root), blob)

    def test_missing_windows_exe(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            result = harness.run_windows_android_live_chat(
                source_dir=Path(tmp),
                windows_exe=Path("missing.exe"),
                serial="emulator-5554",
                timeout_s=1.0,
                run_id="20260818-000000-miss01",
            )
            self.assertEqual(result["status"], "failed")
            self.assertEqual(result["failure_kind"], "windows_executable_missing")

    def test_adb_client_shell_false(self) -> None:
        recorded: list[bool] = []

        def fake_run(argv, capture_output=True, text=True, encoding=None, errors=None, shell=False, timeout=None):
            recorded.append(shell)
            return Completed("ok")

        client = harness.AdbClient(Path("adb"), "emulator-5554", run=fake_run)
        client.shell(["getprop", "ro.product.cpu.abi"])
        self.assertEqual(recorded, [False])


if __name__ == "__main__":
    unittest.main()
