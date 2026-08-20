#!/usr/bin/env python3
"""Unit tests for POSIX platform jobs and process control. No real compiler."""

from __future__ import annotations

import contextlib
import io
import json
import stat
import sys
import tempfile
import time
import unittest
from pathlib import Path

from tools.runners import run_apptraverse_platform as platform_runner
from tools.runners import run_apptraverse_platform_job as jobs


class FakeProc:
    def __init__(self, pid: int = 4242) -> None:
        self.pid = pid


class PlatformJobTest(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = tempfile.TemporaryDirectory()
        self.source = Path(self.tmp.name)
        runner_dir = self.source / "tools" / "runners"
        runner_dir.mkdir(parents=True)
        (runner_dir / "run_apptraverse_platform.py").write_text("# stub\n", encoding="utf-8")
        (runner_dir / "run_apptraverse_platform_job.py").write_text("# stub\n", encoding="utf-8")

    def tearDown(self) -> None:
        self.tmp.cleanup()

    def test_start_returns_quickly_running(self) -> None:
        started = time.perf_counter()
        result = jobs.start_job(
            self.source,
            platform_runner.LINUX_PROFILE,
            "preflight",
            [],
            popen=lambda *a, **k: FakeProc(99),
        )
        elapsed = time.perf_counter() - started
        self.assertLess(elapsed, 5)
        self.assertEqual(result.state, jobs.STATE_RUNNING)
        self.assertEqual(result.pid, 99)
        self.assertTrue(result.artifact_id.startswith("apptraverse-platform/"))

    def test_start_build_defaults_linux_target(self) -> None:
        result = jobs.start_job(
            self.source,
            platform_runner.LINUX_PROFILE,
            "build",
            [],
            popen=lambda *a, **k: FakeProc(),
        )
        request = json.loads(
            (jobs.job_dir_for(self.source, result.job_id) / "request.json").read_text(
                encoding="utf-8"
            )
        )
        self.assertEqual(request["targets"], ["linux_single_client_chat"])

    def test_unknown_profile_fails_without_spawn(self) -> None:
        called = []
        result = jobs.start_job(
            self.source,
            "not-a-profile",
            "preflight",
            [],
            popen=lambda *a, **k: called.append(1) or FakeProc(),
        )
        self.assertEqual(result.state, jobs.STATE_FAILED)
        self.assertEqual(result.failure_kind, "unsupported_profile")
        self.assertEqual(called, [])

    def test_worker_argv_uses_platform_runner_json(self) -> None:
        argv = jobs.canonical_runner_argv(
            self.source,
            platform_runner.LINUX_PROFILE,
            "build",
            ["linux_single_client_chat"],
        )
        self.assertIn("run_apptraverse_platform.py", " ".join(argv))
        self.assertIn("--json", argv)
        self.assertNotIn("--clean-first", argv)
        self.assertFalse(platform_runner.command_is_destructive(argv))

    def test_popen_never_uses_shell(self) -> None:
        seen = {}

        def fake_popen(argv, **kwargs):
            seen["argv"] = argv
            seen["kwargs"] = kwargs
            return FakeProc()

        jobs.start_job(
            self.source,
            platform_runner.MACOS_PROFILE,
            "preflight",
            [],
            popen=fake_popen,
        )
        self.assertFalse(seen["kwargs"].get("shell", False))
        self.assertTrue(seen["kwargs"].get("start_new_session"))

    def test_status_completed_compacts_platform_result(self) -> None:
        started = jobs.start_job(
            self.source,
            platform_runner.LINUX_PROFILE,
            "build",
            ["linux_single_client_chat"],
            popen=lambda *a, **k: FakeProc(),
        )
        jobs.atomic_write_json(
            jobs.job_dir_for(self.source, started.job_id) / "final.json",
            {
                "job_id": started.job_id,
                "state": "completed",
                "profile": platform_runner.LINUX_PROFILE,
                "stage": "build",
                "targets": ["linux_single_client_chat"],
                "platform_result": {
                    "schema_version": platform_runner.RESULT_SCHEMA_VERSION,
                    "status": "ok",
                    "stage": "build",
                    "artifact_id": "apptraverse-platform/r-1",
                    "huge_log": "x" * 5000,
                },
            },
        )
        status = jobs.status_job(self.source, started.job_id)
        self.assertEqual(status.state, jobs.STATE_COMPLETED)
        self.assertNotIn("huge_log", status.platform_result or {})
        self.assertEqual(
            status.platform_result["artifact_id"], "apptraverse-platform/r-1"
        )

    def test_json_emits_one_object(self) -> None:
        result = jobs.JobResult(
            operation="status",
            job_id="j-abc",
            artifact_id="apptraverse-platform/j-abc",
            state=jobs.STATE_RUNNING,
        )
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            jobs.emit_job_result(result, json_mode=True)
        text = buf.getvalue()
        self.assertEqual(text.count("\n"), 1)
        self.assertEqual(json.loads(text)["job_id"], "j-abc")

    def test_no_cmake_construction_in_job_module(self) -> None:
        source = Path(jobs.__file__).read_text(encoding="utf-8")
        self.assertNotIn("def cmake_build_argv", source)
        self.assertNotIn("def cmake_configure_argv", source)
        self.assertNotIn("PROFILES = {", source)


class ProcessControlTest(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = tempfile.TemporaryDirectory()
        self.source = Path(self.tmp.name)

    def tearDown(self) -> None:
        self.tmp.cleanup()

    def _install_fake_exe(self) -> Path:
        exe = platform_runner.exe_path_for(self.source, platform_runner.LINUX_PROFILE)
        exe.parent.mkdir(parents=True, exist_ok=True)
        exe.write_text(
            "#!/usr/bin/env python3\nimport sys, time\n"
            "assert sys.argv[1] == '--state-dir'\n"
            "time.sleep(30)\n",
            encoding="utf-8",
        )
        exe.chmod(exe.stat().st_mode | stat.S_IEXEC)
        return exe

    def test_missing_state_dir_is_typed_failure(self) -> None:
        if not sys.platform.startswith("linux"):
            self.skipTest("linux-only process start")
        result = jobs.start_process(
            self.source, platform_runner.LINUX_PROFILE, "  "
        )
        self.assertEqual(result.failure_kind, "missing_state_dir")
        self.assertEqual(result.state, jobs.STATE_FAILED)

    def test_macos_process_on_linux_is_wrong_host_os(self) -> None:
        if not sys.platform.startswith("linux"):
            self.skipTest("linux-only host-os assertion")
        result = jobs.start_process(
            self.source, platform_runner.MACOS_PROFILE, "/tmp/state"
        )
        self.assertEqual(result.failure_kind, "wrong_host_os")
        self.assertIsNone(result.process_id)

    def test_start_status_stop_fake_process(self) -> None:
        if not sys.platform.startswith("linux"):
            self.skipTest("posix process control")
        self._install_fake_exe()
        state_dir = str(self.source / "state")
        started = jobs.start_process(
            self.source, platform_runner.LINUX_PROFILE, state_dir
        )
        self.assertEqual(started.state, jobs.STATE_RUNNING)
        self.assertTrue(started.artifact_id.startswith("apptraverse-platform/"))
        request = json.loads(
            (
                jobs.process_dir_for(self.source, started.process_id) / "request.json"
            ).read_text(encoding="utf-8")
        )
        self.assertEqual(request["argv"][1], "--state-dir")
        self.assertFalse(any("sh" == part for part in request["argv"]))
        status = jobs.status_process(self.source, started.process_id)
        self.assertEqual(status.state, jobs.STATE_RUNNING)
        dumped = json.dumps(status.to_public_dict())
        self.assertNotIn("FULL STDOUT", dumped)
        stopped = jobs.stop_process(self.source, started.process_id)
        self.assertEqual(stopped.state, jobs.STATE_STOPPED)
        time.sleep(0.1)
        self.assertFalse(jobs.pid_is_alive(started.pid))

    def test_crash_excerpt_is_bounded(self) -> None:
        if not sys.platform.startswith("linux"):
            self.skipTest("posix process control")
        exe = platform_runner.exe_path_for(self.source, platform_runner.LINUX_PROFILE)
        exe.parent.mkdir(parents=True, exist_ok=True)
        lines = "\n".join(f"crash-line-{i}" for i in range(80))
        exe.write_text(
            "#!/usr/bin/env python3\nimport sys\n"
            f"sys.stderr.write({lines!r} + '\\n')\n"
            "raise SystemExit(1)\n",
            encoding="utf-8",
        )
        exe.chmod(exe.stat().st_mode | stat.S_IEXEC)
        started = jobs.start_process(
            self.source, platform_runner.LINUX_PROFILE, str(self.source / "state")
        )
        deadline = time.monotonic() + 5
        status = started
        while time.monotonic() < deadline:
            status = jobs.status_process(self.source, started.process_id)
            if status.state != jobs.STATE_RUNNING:
                break
            time.sleep(0.05)
        self.assertEqual(status.state, jobs.STATE_FAILED)
        self.assertEqual(status.failure_kind, "process_exited")
        excerpt_path = (
            jobs.process_dir_for(self.source, started.process_id) / "failure_excerpt.txt"
        )
        excerpt = excerpt_path.read_text(encoding="utf-8")
        self.assertLessEqual(len(excerpt.splitlines()), 40)
        self.assertLessEqual(len(excerpt), 4000)
        dumped = json.dumps(status.to_public_dict())
        self.assertNotIn("stdout.log", dumped)

    def test_process_json_one_object(self) -> None:
        result = jobs.ProcessResult(
            operation="status",
            process_id="p-1",
            artifact_id="apptraverse-platform/p-1",
            state=jobs.STATE_RUNNING,
        )
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            jobs.emit_process_result(result, json_mode=True)
        text = buf.getvalue()
        self.assertEqual(text.count("\n"), 1)
        self.assertEqual(json.loads(text)["process_id"], "p-1")


if __name__ == "__main__":
    unittest.main()
