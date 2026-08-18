#!/usr/bin/env python3
"""Unit tests and stdio smoke for the App Traverse MCP wrapper."""

from __future__ import annotations

import asyncio
import contextlib
import io
import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from tools.mcp import apptraverse_mcp as mcp_mod
from tools.mcp import setup_apptraverse_mcp as setup_mod
from tools.runners.run_apptraverse_job import JOB_SCHEMA_VERSION, JobResult


def _venv_python() -> Path:
    root = mcp_mod.repo_root()
    venv_dir = root / ".venv-apptraverse-mcp"
    if os.name == "nt":
        return venv_dir / "Scripts" / "python.exe"
    return venv_dir / "bin" / "python"


def _is_venv_python() -> bool:
    try:
        return Path(sys.executable).resolve() == _venv_python().resolve()
    except OSError:
        return False


def _run_in_venv(source: str, timeout: int = 90) -> subprocess.CompletedProcess[str]:
    python = _venv_python()
    return subprocess.run(
        [str(python), "-c", source],
        cwd=str(mcp_mod.repo_root()),
        capture_output=True,
        text=True,
        timeout=timeout,
        shell=False,
    )


def _stdio_transport(command: str, args: list[str], cwd: str):
    from mcp.client.stdio import StdioServerParameters, stdio_client

    return stdio_client(StdioServerParameters(command=command, args=args, cwd=cwd))


def _payload_from_call(result) -> dict:
    if getattr(result, "is_error", False):
        raise AssertionError(f"tool returned MCP error: {result}")
    structured = getattr(result, "structured_content", None)
    if isinstance(structured, dict):
        return structured
    for block in getattr(result, "content", None) or []:
        text = getattr(block, "text", None)
        if not text:
            continue
        loaded = json.loads(text)
        if isinstance(loaded, dict):
            return loaded
    raise AssertionError(f"missing structured tool payload: {result}")


class McpWrapperTest(unittest.TestCase):
    def test_start_delegates_to_start_job(self) -> None:
        fake = JobResult(
            schema_version=JOB_SCHEMA_VERSION,
            operation="start",
            job_id="job-start",
            state="running",
            profile="win64-vs2022-msvc-debug",
            stage="preflight",
            targets=["t"],
        )
        with mock.patch.object(mcp_mod, "start_job", return_value=fake) as start:
            dumped = mcp_mod.apptraverse_build_start(
                "win64-vs2022-msvc-debug", "preflight", ["t"]
            )
        start.assert_called_once_with(
            mcp_mod.repo_root(),
            "win64-vs2022-msvc-debug",
            "preflight",
            ["t"],
        )
        self.assertEqual(dumped["job_id"], "job-start")
        self.assertEqual(dumped["operation"], "start")

    def test_status_delegates_to_status_job(self) -> None:
        fake = JobResult(operation="status", job_id="job-status", state="running")
        with mock.patch.object(mcp_mod, "status_job", return_value=fake) as status:
            dumped = mcp_mod.apptraverse_build_status("job-status")
        status.assert_called_once_with(mcp_mod.repo_root(), "job-status")
        self.assertEqual(dumped["job_id"], "job-status")

    def test_cancel_delegates_to_cancel_job(self) -> None:
        fake = JobResult(operation="cancel", job_id="job-cancel", state="cancelled")
        with mock.patch.object(mcp_mod, "cancel_job", return_value=fake) as cancel:
            dumped = mcp_mod.apptraverse_build_cancel("job-cancel")
        cancel.assert_called_once_with(mcp_mod.repo_root(), "job-cancel")
        self.assertEqual(dumped["state"], "cancelled")

    def test_mcp_layer_has_no_build_command_construction(self) -> None:
        source = Path(mcp_mod.__file__).read_text(encoding="utf-8").lower()
        self.assertNotIn("cmake", source)
        self.assertNotIn("msbuild", source)
        self.assertNotIn("ninja", source)
        self.assertNotIn("cl.exe", source)
        self.assertNotIn("profiles =", source)
        self.assertNotIn("win64-ninja-msvc-debug", source)
        self.assertNotIn("win64-vs2022-msvc-debug", source)

    def test_compact_result_preserved(self) -> None:
        fake = JobResult(
            schema_version=JOB_SCHEMA_VERSION,
            operation="start",
            job_id="compact-1",
            artifact_id="apptraverse-jobs/compact-1",
            state="running",
            profile="win64-vs2022-msvc-debug",
            stage="preflight",
            targets=[],
        )
        with mock.patch.object(mcp_mod, "start_job", return_value=fake):
            dumped = mcp_mod.apptraverse_build_start(
                "win64-vs2022-msvc-debug", "preflight"
            )
        self.assertEqual(dumped, fake.to_public_dict())
        self.assertEqual(dumped["schema_version"], JOB_SCHEMA_VERSION)
        self.assertNotIn("stdout", dumped)
        self.assertNotIn("stderr", dumped)

    def test_compile_failed_is_not_mcp_tool_failure(self) -> None:
        fake = JobResult(
            operation="status",
            job_id="job-compile",
            state="completed",
            build_result={
                "status": "failed",
                "failure_kind": "compile_failed",
                "first_error": "error C1083: domain_visitor.h",
            },
        )
        with mock.patch.object(mcp_mod, "status_job", return_value=fake):
            dumped = mcp_mod.apptraverse_build_status("job-compile")
        self.assertEqual(dumped["state"], "completed")
        self.assertEqual(dumped["build_result"]["failure_kind"], "compile_failed")
        self.assertEqual(dumped["schema_version"], JOB_SCHEMA_VERSION)

    def test_valid_failure_artifact_returns_bounded_excerpt(self) -> None:
        run_id = "mcp-unit-excerpt-s023"
        run_dir = (
            mcp_mod.repo_root() / ".artifacts" / "apptraverse-build" / run_id
        )
        run_dir.mkdir(parents=True, exist_ok=True)
        try:
            (run_dir / "result.json").write_text(
                json.dumps(
                    {
                        "failure_kind": "compile_failed",
                        "first_error": "error C1083: domain_visitor.h",
                    }
                )
                + "\n",
                encoding="utf-8",
            )
            (run_dir / "failure_excerpt.txt").write_text(
                "\n".join(f"line-{i}" for i in range(80)) + "\n",
                encoding="utf-8",
            )
            (run_dir / "stdout.log").write_text("FULL STDOUT LOG\n" * 200, encoding="utf-8")
            (run_dir / "stderr.log").write_text("FULL STDERR LOG\n" * 200, encoding="utf-8")
            dumped = mcp_mod.apptraverse_build_failure_excerpt(
                f"apptraverse-build/{run_id}"
            )
        finally:
            shutil.rmtree(run_dir, ignore_errors=True)
        self.assertEqual(
            set(dumped),
            {"artifact_id", "failure_kind", "first_error", "excerpt"},
        )
        self.assertEqual(dumped["failure_kind"], "compile_failed")
        self.assertEqual(dumped["first_error"], "error C1083: domain_visitor.h")
        excerpt_lines = dumped["excerpt"].splitlines()
        self.assertLessEqual(len(excerpt_lines), 40)
        self.assertLessEqual(len(dumped["excerpt"]), 4000)
        self.assertNotIn("FULL STDOUT LOG", dumped["excerpt"])
        self.assertNotIn("FULL STDERR LOG", json.dumps(dumped))

    def test_rejects_traversal_absolute_and_wrong_prefix_artifact_ids(self) -> None:
        cases = [
            r"C:\Windows\system32\config",
            "/etc/passwd",
            "apptraverse-build/../secret",
            "apptraverse-build/foo/../../etc",
            "apptraverse-jobs/abc",
            "apptraverse-build/",
            "..",
            "stdout.log",
        ]
        for artifact_id in cases:
            dumped = mcp_mod.apptraverse_build_failure_excerpt(artifact_id)
            self.assertEqual(dumped["failure_kind"], "invalid_artifact_id", artifact_id)
            self.assertEqual(dumped["excerpt"], "")

    def test_helpers_do_not_print_to_stdout(self) -> None:
        fake = JobResult(operation="start", job_id="quiet", state="running")
        buf = io.StringIO()
        with mock.patch.object(mcp_mod, "start_job", return_value=fake):
            with mock.patch.object(mcp_mod, "status_job", return_value=fake):
                with mock.patch.object(mcp_mod, "cancel_job", return_value=fake):
                    with contextlib.redirect_stdout(buf):
                        mcp_mod.apptraverse_build_start("p", "preflight")
                        mcp_mod.apptraverse_build_status("quiet")
                        mcp_mod.apptraverse_build_cancel("quiet")
                        mcp_mod.apptraverse_build_failure_excerpt("nope")
        self.assertEqual(buf.getvalue(), "")

    def test_server_exposes_exactly_four_tools(self) -> None:
        self.assertEqual(len(mcp_mod.TOOL_NAMES), 4)
        self.assertEqual(
            list(mcp_mod.TOOL_NAMES),
            [
                "apptraverse_build_start",
                "apptraverse_build_status",
                "apptraverse_build_cancel",
                "apptraverse_build_failure_excerpt",
            ],
        )
        probe = (
            "import json\n"
            "from tools.mcp.apptraverse_mcp import (\n"
            "    TOOL_NAMES, create_mcp_server, registered_tool_names,\n"
            ")\n"
            "names = registered_tool_names(create_mcp_server())\n"
            "print(json.dumps({'names': names, 'expected': list(TOOL_NAMES)}))\n"
        )
        proc = _run_in_venv(probe)
        self.assertEqual(proc.returncode, 0, proc.stderr + proc.stdout)
        payload = json.loads(proc.stdout.strip().splitlines()[-1])
        self.assertEqual(sorted(payload["names"]), sorted(payload["expected"]))
        self.assertEqual(len(payload["names"]), 4)


def run_stdio_smoke() -> dict:
    from mcp import Client

    python = _venv_python()
    server = mcp_mod.repo_root() / "tools" / "mcp" / "apptraverse_mcp.py"

    async def _run() -> dict:
        tmp = tempfile.TemporaryDirectory()
        try:
            params = _stdio_transport(str(python), [str(server)], tmp.name)
            async with Client(params) as client:
                listed = await client.list_tools()
                names = [tool.name for tool in listed.tools]
                if sorted(names) != sorted(mcp_mod.TOOL_NAMES):
                    raise AssertionError(f"tools={names}")
                started = _payload_from_call(
                    await client.call_tool(
                        "apptraverse_build_start",
                        {
                            "profile": "win64-vs2022-msvc-debug",
                            "stage": "preflight",
                            "targets": [],
                        },
                    )
                )
                job_id = started.get("job_id")
                if not job_id:
                    raise AssertionError(started)
                deadline = asyncio.get_running_loop().time() + 60
                status = started
                while asyncio.get_running_loop().time() < deadline:
                    status = _payload_from_call(
                        await client.call_tool(
                            "apptraverse_build_status",
                            {"job_id": job_id},
                        )
                    )
                    if status.get("state") in {"completed", "failed", "cancelled"}:
                        break
                    await asyncio.sleep(0.2)
                status["_listed_tools"] = names
                return status
        finally:
            tmp.cleanup()

    return asyncio.run(_run())


class McpSetupTest(unittest.TestCase):
    def _temp_home(self) -> tempfile.TemporaryDirectory:
        return tempfile.TemporaryDirectory()

    def test_user_mcp_config_path_windows(self) -> None:
        home = Path("C:/Users/test-user")
        path = setup_mod.user_mcp_config_path(home)
        self.assertEqual(path, Path("C:/Users/test-user/.cursor/mcp.json"))

    def test_user_mcp_config_path_posix(self) -> None:
        home = Path("/home/test-user")
        path = setup_mod.user_mcp_config_path(home)
        self.assertEqual(path, home / ".cursor" / "mcp.json")

    def test_merge_empty_user_config(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            home = Path(tmp)
            config_path = home / ".cursor" / "mcp.json"
            root = Path(tmp) / "checkout"
            root.mkdir()
            command = root / ".venv-apptraverse-mcp" / "Scripts" / "python.exe"
            command.parent.mkdir(parents=True)
            command.write_text("", encoding="utf-8")
            server = root / "tools" / "mcp" / "apptraverse_mcp.py"
            server.parent.mkdir(parents=True)
            server.write_text("# stub\n", encoding="utf-8")
            written = setup_mod.merge_user_mcp_config(
                root, command, server, config_path=config_path
            )
            payload = json.loads(written.read_text(encoding="utf-8"))
            self.assertEqual(set(payload), {"mcpServers"})
            self.assertEqual(set(payload["mcpServers"]), {setup_mod.SERVER_KEY})

    def test_merge_preserves_unrelated_servers(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            home = Path(tmp)
            config_path = home / ".cursor" / "mcp.json"
            config_path.parent.mkdir(parents=True)
            config_path.write_text(
                json.dumps(
                    {
                        "mcpServers": {
                            "other-server": {
                                "type": "stdio",
                                "command": "other",
                                "args": [],
                            }
                        }
                    }
                )
                + "\n",
                encoding="utf-8",
            )
            root = Path(tmp) / "checkout"
            root.mkdir()
            command = root / "python"
            command.write_text("", encoding="utf-8")
            server = root / "tools" / "mcp" / "apptraverse_mcp.py"
            server.parent.mkdir(parents=True)
            server.write_text("# stub\n", encoding="utf-8")
            setup_mod.merge_user_mcp_config(
                root, command, server, config_path=config_path
            )
            payload = json.loads(config_path.read_text(encoding="utf-8"))
            self.assertEqual(
                payload["mcpServers"]["other-server"],
                {"type": "stdio", "command": "other", "args": []},
            )
            self.assertIn(setup_mod.SERVER_KEY, payload["mcpServers"])

    def test_merge_updates_apptraverse_entry_only(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            home = Path(tmp)
            config_path = home / ".cursor" / "mcp.json"
            config_path.parent.mkdir(parents=True)
            config_path.write_text(
                json.dumps(
                    {
                        "mcpServers": {
                            "other-server": {
                                "type": "stdio",
                                "command": "other",
                                "args": [],
                            },
                            setup_mod.SERVER_KEY: {
                                "type": "stdio",
                                "command": "old-python",
                                "args": ["old-server.py"],
                            },
                        }
                    }
                )
                + "\n",
                encoding="utf-8",
            )
            root = Path(tmp) / "checkout"
            root.mkdir()
            command = root / "python"
            command.write_text("", encoding="utf-8")
            server = root / "tools" / "mcp" / "apptraverse_mcp.py"
            server.parent.mkdir(parents=True)
            server.write_text("# stub\n", encoding="utf-8")
            setup_mod.merge_user_mcp_config(
                root, command, server, config_path=config_path
            )
            payload = json.loads(config_path.read_text(encoding="utf-8"))
            self.assertEqual(
                payload["mcpServers"]["other-server"],
                {"type": "stdio", "command": "other", "args": []},
            )
            entry = payload["mcpServers"][setup_mod.SERVER_KEY]
            self.assertEqual(entry["command"], str(command.resolve()))
            self.assertEqual(entry["args"], [str(server.resolve())])
            self.assertNotEqual(entry["command"], "old-python")

    def test_merge_is_idempotent(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            config_path = Path(tmp) / ".cursor" / "mcp.json"
            root = Path(tmp) / "checkout"
            root.mkdir()
            command = root / "python"
            command.write_text("", encoding="utf-8")
            server = root / "tools" / "mcp" / "apptraverse_mcp.py"
            server.parent.mkdir(parents=True)
            server.write_text("# stub\n", encoding="utf-8")
            first = setup_mod.merge_user_mcp_config(
                root, command, server, config_path=config_path
            )
            first_text = first.read_text(encoding="utf-8")
            second = setup_mod.merge_user_mcp_config(
                root, command, server, config_path=config_path
            )
            self.assertEqual(first_text, second.read_text(encoding="utf-8"))

    def test_generated_paths_point_to_checkout(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            config_path = Path(tmp) / ".cursor" / "mcp.json"
            root = Path(tmp) / "checkout"
            root.mkdir()
            command = root / ".venv-apptraverse-mcp" / "bin" / "python"
            command.parent.mkdir(parents=True)
            command.write_text("", encoding="utf-8")
            server = root / "tools" / "mcp" / "apptraverse_mcp.py"
            server.parent.mkdir(parents=True)
            server.write_text("# stub\n", encoding="utf-8")
            setup_mod.merge_user_mcp_config(
                root, command, server, config_path=config_path
            )
            entry = json.loads(config_path.read_text(encoding="utf-8"))["mcpServers"][
                setup_mod.SERVER_KEY
            ]
            self.assertTrue(entry["command"].startswith(str(root.resolve())))
            self.assertTrue(entry["args"][0].startswith(str(root.resolve())))

    def test_does_not_write_project_local_config(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            config_path = Path(tmp) / "home" / ".cursor" / "mcp.json"
            root = Path(tmp) / "checkout"
            root.mkdir()
            command = root / "python"
            command.write_text("", encoding="utf-8")
            server = root / "tools" / "mcp" / "apptraverse_mcp.py"
            server.parent.mkdir(parents=True)
            server.write_text("# stub\n", encoding="utf-8")
            setup_mod.merge_user_mcp_config(
                root, command, server, config_path=config_path
            )
            self.assertFalse((root / ".cursor" / "mcp.json").exists())

    def test_setup_subprocess_never_uses_shell(self) -> None:
        with mock.patch.object(setup_mod.subprocess, "run") as run:
            run.return_value = subprocess.CompletedProcess([], 0)
            setup_mod.pip_install(Path("python"), Path("requirements.txt"))
            setup_mod.verify_mcp_sdk(Path("python"))
        self.assertEqual(run.call_count, 2)
        for call in run.call_args_list:
            self.assertFalse(call.kwargs.get("shell", False))


class McpStdioSmokeTest(unittest.TestCase):
    def test_stdio_preflight_job(self) -> None:
        python = _venv_python()
        server = mcp_mod.repo_root() / "tools" / "mcp" / "apptraverse_mcp.py"
        self.assertTrue(python.is_file(), f"missing venv python: {python}")
        self.assertTrue(server.is_file(), f"missing server: {server}")
        if _is_venv_python():
            status = run_stdio_smoke()
        else:
            probe = (
                "import json\n"
                "from tools.mcp.test_apptraverse_mcp import run_stdio_smoke\n"
                "print(json.dumps(run_stdio_smoke()))\n"
            )
            proc = _run_in_venv(probe, timeout=90)
            self.assertEqual(proc.returncode, 0, proc.stderr + proc.stdout)
            status = json.loads(proc.stdout.strip().splitlines()[-1])
        self.assertEqual(status.get("state"), "completed", status)
        self.assertEqual(status.get("schema_version"), JOB_SCHEMA_VERSION)
        self.assertEqual(
            sorted(status.get("_listed_tools") or []),
            sorted(mcp_mod.TOOL_NAMES),
        )
        dumped = json.dumps(status)
        self.assertNotIn("FULL STDOUT", dumped)
        self.assertNotIn("fatal error C", dumped)
        self.assertNotIn("stdout.log", dumped)
        build = status.get("build_result") or {}
        self.assertIn(build.get("status"), {"ok", "blocked", "failed"})
        self.assertNotEqual(build.get("stage"), "build")


if __name__ == "__main__":
    unittest.main()
