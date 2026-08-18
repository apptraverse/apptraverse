#!/usr/bin/env python3
"""Unit tests for the two-Windows chat integration harness."""

from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from tools.integration import run_two_windows_chat as harness
from tools.mcp import apptraverse_mcp as mcp_mod


def _record(seq: int, event: str, data: dict, instance: str = "alice") -> dict:
    return {
        "schema_version": "apptraverse.runtime_event/1",
        "run_id": "run-1",
        "seq": seq,
        "event": event,
        "platform": "windows",
        "instance": instance,
        "pid": 100,
        "t_us": 1,
        "mono_us": 1,
        "data": data,
    }


def _write_jsonl(path: Path, records: list[dict]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("".join(json.dumps(item) + "\n" for item in records), encoding="utf-8")


class FakeProc:
    def __init__(self, pid: int, exit_code: int | None = None) -> None:
        self.pid = pid
        self._exit_code = exit_code

    def poll(self) -> int | None:
        return self._exit_code

    def exit(self, code: int) -> None:
        self._exit_code = code


class HarnessHelperTest(unittest.TestCase):
    def test_uid_extraction_from_setup_jsonl(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "alice_uid.jsonl"
            _write_jsonl(
                path,
                [_record(1, "runtime_started", {"local_uid": "UID-ALICE"})],
            )
            self.assertEqual(harness.extract_local_uid(path), "UID-ALICE")

    def test_two_distinct_uids(self) -> None:
        harness.require_distinct_uids("a", "b")
        with self.assertRaises(harness.IntegrationFailure) as ctx:
            harness.require_distinct_uids("same", "same")
        self.assertEqual(ctx.exception.failure_kind, "uid_collision")

    def test_startup_gates(self) -> None:
        records = [
            _record(1, "runtime_started", {"local_uid": "A"}),
            _record(2, "peer_add", {"peer": "B", "accepted": True}),
        ]
        self.assertTrue(harness.startup_gate_ready(records, "B"))
        self.assertFalse(harness.startup_gate_ready(records, "C"))

    def test_atomic_inbox_write(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "commit.inbox"
            harness.atomic_write_inbox(path, "message_from_alice")
            self.assertEqual(path.read_text(encoding="utf-8"), "message_from_alice\n")
            self.assertFalse(path.with_name("commit.inbox.tmp").exists())

    def test_matching_remote_message_visible(self) -> None:
        records = [
            _record(1, "runtime_started", {"local_uid": "A"}),
            _record(
                2,
                "presentation",
                {
                    "last_entry_kind": "message",
                    "last_entry_text": "message_from_alice",
                    "last_event_obj_id": 1,
                },
            ),
            _record(
                3,
                "message_visible",
                {
                    "event_obj_id": 42,
                    "author": "Bob",
                    "text": "message_from_bob",
                    "direction": "remote",
                },
            ),
        ]
        self.assertEqual(harness.remote_event_obj_id(records, "message_from_bob"), 42)
        self.assertEqual(
            harness.presentation_last_entry_event_id(records, "message_from_alice"), 1
        )
        self.assertIsNone(
            harness.presentation_last_entry_event_id(records, "message_from_bob")
        )

    def test_last_entry_is_not_used_for_remote_delivery(self) -> None:
        records = [
            _record(
                1,
                "presentation",
                {
                    "last_entry_kind": "message",
                    "last_entry_text": "message_from_alice",
                    "last_event_obj_id": 3090208674,
                },
            ),
            _record(
                2,
                "message_visible",
                {
                    "event_obj_id": 264338121,
                    "author": "Bob",
                    "text": "message_from_bob",
                    "direction": "remote",
                },
            ),
        ]
        self.assertEqual(harness.remote_event_obj_id(records, "message_from_bob"), 264338121)
        source = Path(harness.__file__).read_text(encoding="utf-8")
        self.assertNotIn("last_entry_text", source.split("def remote_event_obj_id")[1].split("def ")[0])

    def test_repeated_message_visible_same_event_id_accepted(self) -> None:
        records = [
            _record(
                1,
                "message_visible",
                {"event_obj_id": 7, "author": "Bob", "text": "message_from_bob"},
            ),
            _record(
                2,
                "message_visible",
                {"event_obj_id": 7, "author": "Bob", "text": "message_from_bob"},
            ),
        ]
        self.assertEqual(harness.remote_event_obj_id(records, "message_from_bob"), 7)

    def test_two_different_event_ids_rejected(self) -> None:
        records = [
            _record(
                1,
                "message_visible",
                {"event_obj_id": 1, "author": "Bob", "text": "message_from_bob"},
            ),
            _record(
                2,
                "message_visible",
                {"event_obj_id": 2, "author": "Bob", "text": "message_from_bob"},
            ),
        ]
        with self.assertRaises(harness.IntegrationFailure) as ctx:
            harness.remote_event_obj_id(records, "message_from_bob")
        self.assertEqual(ctx.exception.failure_kind, "duplicate_remote_event")

    def test_compact_result_has_no_absolute_path(self) -> None:
        payload = harness.compact_result(
            run_id="run-1",
            status="ok",
            duration_ms=10,
            failure_kind=None,
            first_error=None,
            instances=[],
        )
        dumped = json.dumps(payload)
        self.assertEqual(payload["artifact_id"], "apptraverse-integration/run-1")
        self.assertNotIn("C:", dumped)
        self.assertNotIn("stdout.log", dumped)
        self.assertFalse(harness.result_contains_forbidden_payload(payload))

    def test_no_shell_true_or_powershell_or_retry(self) -> None:
        source = Path(harness.__file__).read_text(encoding="utf-8").lower()
        self.assertIn("shell=false", source)
        self.assertNotIn("shell=true", source)
        self.assertNotIn("powershell", source)
        self.assertNotIn("retry", source)


class ProcessControlTest(unittest.TestCase):
    def test_timeout_cleanup_terminates_both_processes(self) -> None:
        terminated: list[int] = []
        alice = harness.ManagedProcess("alice", FakeProc(11), jsonl_path=Path("missing-a"))
        bob = harness.ManagedProcess("bob", FakeProc(22), jsonl_path=Path("missing-b"))
        with self.assertRaises(harness.IntegrationFailure) as ctx:
            harness.wait_startup_gate(
                [alice, bob],
                {"alice": "B", "bob": "A"},
                timeout_s=0.01,
                sleep=lambda _dt: None,
            )
        self.assertEqual(ctx.exception.failure_kind, "startup_timeout")
        harness.terminate_managed([alice, bob], terminate=terminated.append)
        self.assertEqual(sorted(terminated), [11, 22])

    def test_early_process_exit(self) -> None:
        alice = harness.ManagedProcess("alice", FakeProc(11, exit_code=1), jsonl_path=Path("a"))
        bob = harness.ManagedProcess("bob", FakeProc(22), jsonl_path=Path("b"))
        with self.assertRaises(harness.IntegrationFailure) as ctx:
            harness.wait_startup_gate(
                [alice, bob],
                {"alice": "B", "bob": "A"},
                timeout_s=1.0,
                sleep=lambda _dt: None,
            )
        self.assertEqual(ctx.exception.failure_kind, "process_exited_before_ready")


class FakeChatPopen:
    def __init__(self) -> None:
        self.calls: list[list[str]] = []
        self.live: dict[str, FakeProc] = {}
        self._next_pid = 300

    def __call__(self, argv, stdout, stderr, env, cwd, shell=False):
        self.assert_no_shell(shell)
        self.calls.append(list(argv))
        jsonl = Path(env["APPTRAVERSE_RUNTIME_JSONL"])
        instance = env["APPTRAVERSE_INSTANCE"]
        pid = self._next_pid
        self._next_pid += 1
        if "--print-aether-uid" in argv:
            uid = "UID-ALICE" if "alice" in instance else "UID-BOB"
            _write_jsonl(jsonl, [_record(1, "runtime_started", {"local_uid": uid}, instance)])
            return FakeProc(pid, exit_code=0)
        peer = argv[argv.index("--peer") + 1]
        _write_jsonl(
            jsonl,
            [
                _record(1, "runtime_started", {"local_uid": "live"}, instance),
                _record(2, "peer_add", {"peer": peer, "accepted": True}, instance),
            ],
        )
        proc = FakeProc(pid)
        self.live[instance] = proc
        self._jsonl = getattr(self, "_jsonl", {})
        self._jsonl[instance] = jsonl
        self._env = getattr(self, "_env", {})
        self._env[instance] = env
        self._argv = getattr(self, "_argv", {})
        self._argv[instance] = argv
        return proc

    @staticmethod
    def assert_no_shell(shell: bool) -> None:
        if shell:
            raise AssertionError("shell must be False")

    def complete_live(self, *, include_alice_remote: bool = True, include_bob_remote: bool = True) -> None:
        mapping = {
            "alice": ("message_from_alice", "message_from_bob", "UID-BOB", 10, 99, include_alice_remote),
            "bob": ("message_from_bob", "message_from_alice", "UID-ALICE", 11, 88, include_bob_remote),
        }
        for instance, proc in self.live.items():
            submitted, remote, peer, local_id, remote_id, include_remote = mapping[instance]
            jsonl = self._jsonl[instance]
            records = [
                _record(1, "runtime_started", {"local_uid": instance}, instance),
                _record(2, "peer_add", {"peer": peer, "accepted": True}, instance),
                _record(
                    3,
                    "text_submit",
                    {"text": submitted, "accepted": True, "event_obj_id": local_id},
                    instance,
                ),
                _record(
                    4,
                    "presentation",
                    {
                        "last_entry_kind": "message",
                        "last_entry_text": submitted,
                        "last_event_obj_id": local_id,
                    },
                    instance,
                ),
            ]
            if include_remote:
                records.append(
                    _record(
                        5,
                        "message_visible",
                        {
                            "event_obj_id": remote_id,
                            "author": "peer",
                            "text": remote,
                            "direction": "remote",
                        },
                        instance,
                    )
                )
            _write_jsonl(jsonl, records)


class FakeScenarioTest(unittest.TestCase):
    def _exe(self, root: Path) -> Path:
        exe = (
            root
            / "build"
            / "win64-vs2022-msvc-debug"
            / "Debug"
            / "win32_single_client_chat.exe"
        )
        exe.parent.mkdir(parents=True)
        exe.write_text("fake", encoding="utf-8")
        return exe

    def test_fake_scenario_ok_and_mcp_compact(self) -> None:
        popen = FakeChatPopen()
        original_wait = harness.wait_delivery_gate
        terminated: list[int] = []

        def wait_ok(alice, bob, *, timeout_s, sleep):
            popen.complete_live()
            return original_wait(alice, bob, timeout_s=timeout_s, sleep=lambda _dt: None)

        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            exe = self._exe(root)
            with mock.patch.object(harness, "wait_delivery_gate", wait_ok):
                result = harness.run_two_windows_chat(
                    source_dir=root,
                    exe=exe,
                    timeout_seconds=30,
                    startup_timeout_s=1.0,
                    delivery_timeout_s=1.0,
                    popen=popen,
                    terminate=terminated.append,
                    sleep=lambda _dt: None,
                    run_id="unit-ok",
                )
            self.assertEqual(result["status"], "ok")
            self.assertEqual(result["artifact_id"], "apptraverse-integration/unit-ok")
            dumped = json.dumps(result)
            self.assertNotIn("stdout.log", dumped)
            self.assertNotIn("C:\\", dumped)
            self.assertEqual(sorted(terminated), [302, 303])
            self.assertEqual(
                result["instances"][0]["process_completion"],
                harness.COMPLETION_HARNESS_TERMINATED,
            )
            self.assertEqual(
                result["instances"][1]["process_completion"],
                harness.COMPLETION_HARNESS_TERMINATED,
            )
            self.assertTrue(result["instances"][0]["remote_message_visible"])
            self.assertTrue(result["instances"][1]["remote_message_visible"])
            for argv in popen.calls:
                if "--peer" in argv:
                    self.assertNotIn("--exit-after-message", argv)
                    self.assertNotIn("--wait-for-message", argv)
            for proc in popen.live.values():
                self.assertIsNone(proc.poll())

    def test_live_argv_omits_exit_after_message(self) -> None:
        popen = FakeChatPopen()
        original_wait = harness.wait_delivery_gate

        def wait_ok(alice, bob, *, timeout_s, sleep):
            popen.complete_live()
            return original_wait(alice, bob, timeout_s=timeout_s, sleep=lambda _dt: None)

        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            with mock.patch.object(harness, "wait_delivery_gate", wait_ok):
                harness.run_two_windows_chat(
                    source_dir=root,
                    exe=self._exe(root),
                    timeout_seconds=30,
                    startup_timeout_s=1.0,
                    delivery_timeout_s=1.0,
                    popen=popen,
                    terminate=lambda _pid: None,
                    sleep=lambda _dt: None,
                    run_id="unit-argv",
                )
        live = [argv for argv in popen.calls if "--peer" in argv]
        self.assertEqual(len(live), 2)
        for argv in live:
            self.assertNotIn("--exit-after-message", argv)
            self.assertNotIn("--wait-for-message", argv)
            self.assertIn("--commit-inbox", argv)

    def test_one_sided_visibility_does_not_pass(self) -> None:
        popen = FakeChatPopen()
        original_wait = harness.wait_delivery_gate

        def wait_one_sided(alice, bob, *, timeout_s, sleep):
            popen.complete_live(include_alice_remote=True, include_bob_remote=False)
            return original_wait(alice, bob, timeout_s=timeout_s, sleep=lambda _dt: None)

        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            with mock.patch.object(harness, "wait_delivery_gate", wait_one_sided):
                result = harness.run_two_windows_chat(
                    source_dir=root,
                    exe=self._exe(root),
                    timeout_seconds=30,
                    startup_timeout_s=1.0,
                    delivery_timeout_s=1.0,
                    popen=popen,
                    terminate=lambda _pid: None,
                    sleep=lambda _dt: None,
                    run_id="unit-one-sided",
                )
            self.assertEqual(result["status"], "failed")
            self.assertEqual(result["failure_kind"], "message_delivery_timeout")
            by_name = {item["instance"]: item for item in result["instances"]}
            self.assertTrue(by_name["alice"]["remote_message_visible"])
            self.assertFalse(by_name["bob"]["remote_message_visible"])
            self.assertEqual(len(result["instances"]), 2)

    def test_process_exit_before_delivery_fails(self) -> None:
        popen = FakeChatPopen()
        original_wait = harness.wait_delivery_gate

        def wait_exit(alice, bob, *, timeout_s, sleep):
            popen.live["alice"].exit(1)
            return original_wait(alice, bob, timeout_s=timeout_s, sleep=lambda _dt: None)

        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            with mock.patch.object(harness, "wait_delivery_gate", wait_exit):
                result = harness.run_two_windows_chat(
                    source_dir=root,
                    exe=self._exe(root),
                    timeout_seconds=30,
                    startup_timeout_s=1.0,
                    delivery_timeout_s=1.0,
                    popen=popen,
                    terminate=lambda _pid: None,
                    sleep=lambda _dt: None,
                    run_id="unit-early-exit",
                )
            self.assertEqual(result["status"], "failed")
            self.assertEqual(result["failure_kind"], "process_exited_before_delivery")
            by_name = {item["instance"]: item for item in result["instances"]}
            self.assertEqual(by_name["alice"]["process_state"], "exited")
            self.assertEqual(by_name["alice"]["exit_code"], 1)
            self.assertEqual(len(result["instances"]), 2)

    def test_partial_summaries_survive_timeout(self) -> None:
        popen = FakeChatPopen()
        original_wait = harness.wait_delivery_gate

        def wait_timeout(alice, bob, *, timeout_s, sleep):
            return original_wait(alice, bob, timeout_s=timeout_s, sleep=lambda _dt: None)

        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            with mock.patch.object(harness, "wait_delivery_gate", wait_timeout):
                result = harness.run_two_windows_chat(
                    source_dir=root,
                    exe=self._exe(root),
                    timeout_seconds=30,
                    startup_timeout_s=1.0,
                    delivery_timeout_s=1.0,
                    popen=popen,
                    terminate=lambda _pid: None,
                    sleep=lambda _dt: None,
                    run_id="unit-timeout",
                )
            self.assertEqual(result["status"], "failed")
            self.assertEqual(result["failure_kind"], "message_delivery_timeout")
            self.assertEqual(len(result["instances"]), 2)
            by_name = {item["instance"]: item for item in result["instances"]}
            self.assertEqual(by_name["alice"]["local_uid"], "UID-ALICE")
            self.assertEqual(by_name["bob"]["local_uid"], "UID-BOB")
            self.assertFalse(by_name["alice"]["remote_message_visible"])
            self.assertEqual(by_name["alice"]["process_state"], "running")

    def test_duplicate_remote_event_fails_during_gate(self) -> None:
        popen = FakeChatPopen()
        original_wait = harness.wait_delivery_gate

        def wait_dup(alice, bob, *, timeout_s, sleep):
            popen.complete_live(include_alice_remote=True, include_bob_remote=True)
            _write_jsonl(
                popen._jsonl["alice"],
                [
                    _record(1, "runtime_started", {"local_uid": "alice"}, "alice"),
                    _record(2, "peer_add", {"peer": "UID-BOB", "accepted": True}, "alice"),
                    _record(
                        3,
                        "text_submit",
                        {"text": "message_from_alice", "accepted": True, "event_obj_id": 10},
                        "alice",
                    ),
                    _record(
                        4,
                        "message_visible",
                        {"event_obj_id": 1, "text": "message_from_bob"},
                        "alice",
                    ),
                    _record(
                        5,
                        "message_visible",
                        {"event_obj_id": 2, "text": "message_from_bob"},
                        "alice",
                    ),
                ],
            )
            return original_wait(alice, bob, timeout_s=timeout_s, sleep=lambda _dt: None)

        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            with mock.patch.object(harness, "wait_delivery_gate", wait_dup):
                result = harness.run_two_windows_chat(
                    source_dir=root,
                    exe=self._exe(root),
                    timeout_seconds=30,
                    startup_timeout_s=1.0,
                    delivery_timeout_s=1.0,
                    popen=popen,
                    terminate=lambda _pid: None,
                    sleep=lambda _dt: None,
                    run_id="unit-dup",
                )
            self.assertEqual(result["status"], "failed")
            self.assertEqual(result["failure_kind"], "duplicate_remote_event")
            self.assertEqual(len(result["instances"]), 2)

    def test_alice_stays_alive_until_bob_delivery(self) -> None:
        popen = FakeChatPopen()
        original_wait = harness.wait_delivery_gate
        alice_alive_after_alice_visible = []

        def wait_staggered(alice, bob, *, timeout_s, sleep):
            popen.complete_live(include_alice_remote=True, include_bob_remote=False)
            alice_alive_after_alice_visible.append(alice.poll() is None and bob.poll() is None)
            popen.complete_live(include_alice_remote=True, include_bob_remote=True)
            return original_wait(alice, bob, timeout_s=timeout_s, sleep=lambda _dt: None)

        terminated: list[int] = []
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            with mock.patch.object(harness, "wait_delivery_gate", wait_staggered):
                result = harness.run_two_windows_chat(
                    source_dir=root,
                    exe=self._exe(root),
                    timeout_seconds=30,
                    startup_timeout_s=1.0,
                    delivery_timeout_s=1.0,
                    popen=popen,
                    terminate=terminated.append,
                    sleep=lambda _dt: None,
                    run_id="unit-stagger",
                )
            self.assertEqual(result["status"], "ok")
            self.assertEqual(alice_alive_after_alice_visible, [True])
            self.assertTrue(set(terminated) >= {popen.live["alice"].pid, popen.live["bob"].pid})


class ExeAndMcpTest(unittest.TestCase):
    def test_executable_path_restriction(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            outside = root / "other" / "win32_single_client_chat.exe"
            outside.parent.mkdir(parents=True)
            outside.write_text("x", encoding="utf-8")
            with self.assertRaises(harness.IntegrationFailure) as ctx:
                harness.validate_mcp_exe(root, str(outside))
            self.assertEqual(ctx.exception.failure_kind, "executable_not_found")

            inside = (
                root
                / "build"
                / "win64-vs2022-msvc-debug"
                / "Debug"
                / "win32_single_client_chat.exe"
            )
            inside.parent.mkdir(parents=True)
            inside.write_text("x", encoding="utf-8")
            resolved = harness.validate_mcp_exe(root, str(inside))
            self.assertEqual(resolved.name.lower(), "win32_single_client_chat.exe")

    def test_mcp_timeout_bound(self) -> None:
        dumped = mcp_mod.apptraverse_two_windows_chat_run("win32_single_client_chat.exe", 999)
        self.assertEqual(dumped["failure_kind"], "invalid_timeout")
        self.assertEqual(dumped["status"], "failed")
        self.assertNotIn("stdout.log", json.dumps(dumped))

    def test_mcp_result_does_not_contain_full_logs(self) -> None:
        fake = harness.compact_result(
            run_id="mcp-run",
            status="ok",
            duration_ms=12,
            failure_kind=None,
            first_error=None,
            instances=[],
        )
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            exe = (
                root
                / "build"
                / "win64-vs2022-msvc-debug"
                / "Debug"
                / "win32_single_client_chat.exe"
            )
            exe.parent.mkdir(parents=True)
            exe.write_text("x", encoding="utf-8")
            with mock.patch.object(mcp_mod, "repo_root", return_value=root):
                with mock.patch.object(
                    mcp_mod, "run_two_windows_chat", return_value=fake
                ):
                    dumped = mcp_mod.apptraverse_two_windows_chat_run(str(exe), 30)
            self.assertEqual(dumped["status"], "ok")
            self.assertNotIn("stdout.log", json.dumps(dumped))
            self.assertNotIn("stderr.log", json.dumps(dumped))

    def test_mcp_registers_six_tools(self) -> None:
        self.assertEqual(len(mcp_mod.TOOL_NAMES), 6)
        self.assertIn("apptraverse_two_windows_chat_run", mcp_mod.TOOL_NAMES)


if __name__ == "__main__":
    unittest.main()
