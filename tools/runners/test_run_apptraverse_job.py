#!/usr/bin/env python3
"""Unit tests for background job controller. No real compiler."""

from __future__ import annotations

import contextlib
import io
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from tools.runners import run_apptraverse_build as build_runner
from tools.runners import run_apptraverse_job as jobs


class FakeProc:
    def __init__(self, pid: int = 4242) -> None:
        self.pid = pid


class JobControllerTest(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = tempfile.TemporaryDirectory()
        self.source = Path(self.tmp.name)
        runner_dir = self.source / "tools" / "runners"
        runner_dir.mkdir(parents=True)
        (runner_dir / "run_apptraverse_build.py").write_text("# stub\n", encoding="utf-8")
        (runner_dir / "run_apptraverse_job.py").write_text("# stub\n", encoding="utf-8")

    def tearDown(self) -> None:
        self.tmp.cleanup()

    def test_start_creates_job_dir_and_request(self) -> None:
        result = jobs.start_job(
            self.source,
            build_runner.VS2022_PROFILE,
            "build",
            ["apptraverse_chat_component_test"],
            popen=lambda *a, **k: FakeProc(),
        )
        job_dir = jobs.job_dir_for(self.source, result.job_id)
        self.assertTrue(job_dir.is_dir())
        request = json.loads((job_dir / "request.json").read_text(encoding="utf-8"))
        self.assertEqual(request["profile"], build_runner.VS2022_PROFILE)
        self.assertEqual(request["stage"], "build")
        self.assertEqual(request["targets"], ["apptraverse_chat_component_test"])

    def test_start_returns_quickly_running(self) -> None:
        started = jobs.time.perf_counter()
        result = jobs.start_job(
            self.source,
            build_runner.VS2022_PROFILE,
            "build",
            ["apptraverse_chat_component_test"],
            popen=lambda *a, **k: FakeProc(99),
        )
        elapsed = jobs.time.perf_counter() - started
        self.assertLess(elapsed, 5)
        self.assertEqual(result.state, jobs.STATE_RUNNING)
        self.assertEqual(result.pid, 99)

    def test_worker_command_uses_canonical_runner_json(self) -> None:
        argv = jobs.canonical_runner_argv(
            self.source,
            build_runner.VS2022_PROFILE,
            "build",
            ["apptraverse_chat_component_test"],
        )
        joined = " ".join(argv)
        self.assertIn("run_apptraverse_build.py", joined)
        self.assertIn("--json", argv)
        self.assertNotIn("--clean-first", argv)
        self.assertNotIn("clean", argv)
        self.assertNotIn("rebuild", argv)
        self.assertFalse(build_runner.command_is_destructive(argv))

    def test_request_preserves_profile_stage_targets(self) -> None:
        result = jobs.start_job(
            self.source,
            build_runner.NINJA_PROFILE,
            "preflight",
            [],
            popen=lambda *a, **k: FakeProc(),
        )
        request = json.loads(
            (jobs.job_dir_for(self.source, result.job_id) / "request.json").read_text(
                encoding="utf-8"
            )
        )
        self.assertEqual(request["profile"], build_runner.NINJA_PROFILE)
        self.assertEqual(request["stage"], "preflight")
        self.assertEqual(request["targets"], [])

    def test_public_result_has_artifact_id_not_path(self) -> None:
        result = jobs.start_job(
            self.source,
            build_runner.VS2022_PROFILE,
            "build",
            ["t"],
            popen=lambda *a, **k: FakeProc(),
        )
        dumped = json.dumps(result.to_public_dict())
        self.assertTrue(result.artifact_id.startswith("apptraverse-jobs/"))
        self.assertNotIn(str(self.source), dumped)
        self.assertNotRegex(dumped, r"[A-Za-z]:\\\\")

    def test_status_running_for_live_worker(self) -> None:
        started = jobs.start_job(
            self.source,
            build_runner.VS2022_PROFILE,
            "build",
            ["t"],
            popen=lambda *a, **k: FakeProc(777),
        )
        with mock.patch.object(jobs, "pid_is_alive", return_value=True):
            status = jobs.status_job(self.source, started.job_id)
        self.assertEqual(status.state, jobs.STATE_RUNNING)
        self.assertEqual(status.pid, 777)

    def test_status_completed_with_final_json(self) -> None:
        started = jobs.start_job(
            self.source,
            build_runner.VS2022_PROFILE,
            "build",
            ["apptraverse_chat_component_test"],
            popen=lambda *a, **k: FakeProc(),
        )
        jobs.atomic_write_json(
            jobs.job_dir_for(self.source, started.job_id) / "final.json",
            {
                "job_id": started.job_id,
                "state": "completed",
                "profile": build_runner.VS2022_PROFILE,
                "stage": "build",
                "targets": ["apptraverse_chat_component_test"],
                "build_result": {
                    "schema_version": build_runner.RESULT_SCHEMA_VERSION,
                    "status": "ok",
                    "stage": "build",
                    "artifact_id": "apptraverse-build/run1",
                    "huge_log": "x" * 5000,
                },
            },
        )
        status = jobs.status_job(self.source, started.job_id)
        self.assertEqual(status.state, jobs.STATE_COMPLETED)
        self.assertNotIn("huge_log", status.build_result or {})
        self.assertEqual(status.build_result["artifact_id"], "apptraverse-build/run1")

    def test_compile_failed_is_job_completed(self) -> None:
        started = jobs.start_job(
            self.source,
            build_runner.VS2022_PROFILE,
            "build",
            ["apptraverse_chat_component_test"],
            popen=lambda *a, **k: FakeProc(),
        )
        jobs.atomic_write_json(
            jobs.job_dir_for(self.source, started.job_id) / "final.json",
            {
                "build_result": {
                    "status": "failed",
                    "failure_kind": "compile_failed",
                    "first_error": "error C1083: aether-miscpp/reflect/domain_visitor.h",
                    "artifact_id": "apptraverse-build/abc",
                },
                "profile": build_runner.VS2022_PROFILE,
                "stage": "build",
                "targets": ["apptraverse_chat_component_test"],
            },
        )
        status = jobs.status_job(self.source, started.job_id)
        self.assertEqual(status.state, jobs.STATE_COMPLETED)
        self.assertEqual(status.build_result["status"], "failed")
        self.assertEqual(status.build_result["failure_kind"], "compile_failed")

    def test_dead_worker_without_final_result(self) -> None:
        started = jobs.start_job(
            self.source,
            build_runner.VS2022_PROFILE,
            "build",
            ["t"],
            popen=lambda *a, **k: FakeProc(1),
        )
        with mock.patch.object(jobs, "pid_is_alive", return_value=False):
            status = jobs.status_job(self.source, started.job_id)
        self.assertEqual(status.state, jobs.STATE_FAILED)
        self.assertEqual(status.failure_kind, "worker_terminated_without_result")

    def test_unknown_job_not_found(self) -> None:
        status = jobs.status_job(self.source, "no-such-job")
        self.assertEqual(status.state, jobs.STATE_NOT_FOUND)
        self.assertEqual(status.failure_kind, "job_not_found")

    def test_cancel_uses_platform_helper(self) -> None:
        started = jobs.start_job(
            self.source,
            build_runner.VS2022_PROFILE,
            "build",
            ["t"],
            popen=lambda *a, **k: FakeProc(888),
        )
        killed = []
        with mock.patch.object(jobs, "pid_is_alive", return_value=True):
            result = jobs.cancel_job(
                self.source, started.job_id, terminate=killed.append
            )
        self.assertEqual(killed, [888])
        self.assertEqual(result.state, jobs.STATE_CANCELLED)
        self.assertTrue(
            (jobs.job_dir_for(self.source, started.job_id) / "cancelled.json").is_file()
        )

    def test_cancel_after_completion_does_not_kill(self) -> None:
        started = jobs.start_job(
            self.source,
            build_runner.VS2022_PROFILE,
            "build",
            ["t"],
            popen=lambda *a, **k: FakeProc(9),
        )
        jobs.atomic_write_json(
            jobs.job_dir_for(self.source, started.job_id) / "final.json",
            {"build_result": {"status": "ok"}, "profile": "p", "stage": "build"},
        )
        killed = []
        result = jobs.cancel_job(self.source, started.job_id, terminate=killed.append)
        self.assertEqual(killed, [])
        self.assertEqual(result.state, jobs.STATE_COMPLETED)

    def test_json_emits_one_object(self) -> None:
        result = jobs.JobResult(
            operation="status",
            job_id="abc",
            artifact_id="apptraverse-jobs/abc",
            state=jobs.STATE_RUNNING,
        )
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            jobs.emit_job_result(result, json_mode=True)
        text = buf.getvalue()
        self.assertEqual(text.count("\n"), 1)
        obj = json.loads(text)
        self.assertEqual(obj["job_id"], "abc")

    def test_atomic_write_replaces(self) -> None:
        path = self.source / "out.json"
        jobs.atomic_write_json(path, {"ok": True})
        self.assertTrue(path.is_file())
        self.assertFalse(path.with_name("out.json.tmp").exists())
        self.assertEqual(json.loads(path.read_text(encoding="utf-8"))["ok"], True)

    def test_windows_launch_uses_cmd_start(self) -> None:
        argv = jobs.worker_launch_argv(["python.exe", "run_apptraverse_job.py", "_worker"])
        if jobs.sys.platform.startswith("win"):
            self.assertEqual(argv[:4], ["cmd.exe", "/c", "start", ""])
            self.assertIn("_worker", argv)
        else:
            self.assertEqual(argv[0], "python.exe")

    def test_pid_is_alive_other_process(self) -> None:
        proc = subprocess.Popen(
            [sys.executable, "-c", "import time; time.sleep(30)"],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        try:
            self.assertTrue(jobs.pid_is_alive(proc.pid))
        finally:
            proc.kill()
            proc.wait(timeout=5)
        self.assertFalse(jobs.pid_is_alive(proc.pid))

    def test_cancel_taskkill_argv(self) -> None:
        with mock.patch.object(jobs.subprocess, "run") as run:
            jobs.terminate_process_tree(1234)
        if not jobs.sys.platform.startswith("win"):
            return
        run.assert_called_once()
        called = run.call_args
        self.assertEqual(
            called.args[0],
            ["taskkill.exe", "/PID", "1234", "/T", "/F"],
        )
        self.assertFalse(called.kwargs.get("shell"))

    def test_no_build_logic_duplicated(self) -> None:
        source = Path(jobs.__file__).read_text(encoding="utf-8")
        self.assertNotIn("PROFILES = {", source)
        self.assertNotIn("def cmake_build_argv", source)
        self.assertNotIn("def extract_first_error", source)
        self.assertIn("run_apptraverse_build.py", source)


if __name__ == "__main__":
    unittest.main()
