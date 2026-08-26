#!/usr/bin/env python3
"""Unit tests for runner decision logic. No compilation."""

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

from tools.runners import run_apptraverse_build as runner

MSVC_NINJA_CACHE = """\
CMAKE_BUILD_TYPE:STRING=Debug
CMAKE_GENERATOR:INTERNAL=Ninja
CMAKE_CXX_COMPILER:FILEPATH=C:/Program Files/Microsoft Visual Studio/18/Community/VC/Tools/MSVC/14.50.35717/bin/Hostx64/x64/cl.exe
CMAKE_CXX_COMPILER_ID:STRING=MSVC
"""

MINGW_CACHE = """\
CMAKE_BUILD_TYPE:STRING=Debug
CMAKE_GENERATOR:INTERNAL=Ninja
CMAKE_CXX_COMPILER:FILEPATH=C:/msys64/ucrt64/bin/c++.exe
CMAKE_CXX_COMPILER_ID:STRING=GNU
"""

VS_GENERATOR_CACHE = """\
CMAKE_GENERATOR:INTERNAL=Visual Studio 17 2022
CMAKE_GENERATOR_PLATFORM:INTERNAL=x64
CMAKE_CXX_COMPILER:FILEPATH=C:/Program Files/Microsoft Visual Studio/18/Community/VC/Tools/MSVC/14.50.35717/bin/Hostx64/x64/cl.exe
CMAKE_CXX_COMPILER_ID:STRING=MSVC
"""

CAPABILITIES_WITH_VS = """\
{"generators":[{"name":"Ninja"},{"name":"Visual Studio 17 2022"},{"name":"Unix Makefiles"}]}
"""

CAPABILITIES_WITHOUT_VS = """\
{"generators":[{"name":"Ninja"},{"name":"Unix Makefiles"}]}
"""


def _which_cmake_only(name: str):
    if name == "cmake":
        return "C:/fake/cmake.exe"
    return None


def _which_cmake_and_cl(name: str):
    if name in {"cmake", "cl"}:
        return f"C:/fake/{name}.exe"
    return None


class UnsupportedProfileTest(unittest.TestCase):
    def test_rejects_linux_profile(self) -> None:
        status, kind, _reason = runner.preflight(
            "linux-x64-ninja-clang-debug",
            Path("."),
            platform="win32",
            which=lambda _name: "fake",
        )
        self.assertEqual(status, runner.STATUS_BLOCKED)
        self.assertEqual(kind, "unsupported_profile")


class NinjaPreflightTest(unittest.TestCase):
    def test_ninja_profile_reports_ninja_missing(self) -> None:
        status, kind, _reason = runner.preflight(
            runner.NINJA_PROFILE,
            Path("."),
            platform="win32",
            which=_which_cmake_and_cl,
        )
        self.assertEqual(status, runner.STATUS_BLOCKED)
        self.assertEqual(kind, "ninja_missing")

    def test_non_windows_is_unsupported_platform(self) -> None:
        status, kind, reason = runner.preflight(
            runner.NINJA_PROFILE,
            Path("."),
            platform="linux",
            which=lambda _name: "fake",
        )
        self.assertEqual(status, runner.STATUS_BLOCKED)
        self.assertEqual(kind, "unsupported_platform")
        self.assertEqual(reason, "linux")


class VisualStudioPreflightTest(unittest.TestCase):
    def test_vs_profile_does_not_require_ninja(self) -> None:
        status, kind, _reason = runner.preflight(
            runner.VS2022_PROFILE,
            Path("."),
            platform="win32",
            which=_which_cmake_only,
            generators=[runner.VS2022_GENERATOR],
        )
        self.assertEqual(kind, None)
        self.assertEqual(status, runner.STATUS_OK)

    def test_vs_generator_missing(self) -> None:
        status, kind, _reason = runner.preflight(
            runner.VS2022_PROFILE,
            Path("."),
            platform="win32",
            which=_which_cmake_only,
            generators=["Ninja"],
        )
        self.assertEqual(status, runner.STATUS_BLOCKED)
        self.assertEqual(kind, "visual_studio_generator_missing")

    def test_list_cmake_generators_detects_vs(self) -> None:
        names = runner.list_cmake_generators(capabilities_json=CAPABILITIES_WITH_VS)
        self.assertIn(runner.VS2022_GENERATOR, names)
        missing = runner.list_cmake_generators(capabilities_json=CAPABILITIES_WITHOUT_VS)
        self.assertNotIn(runner.VS2022_GENERATOR, missing)


class CacheParserTest(unittest.TestCase):
    def test_accepts_ninja_msvc_cache(self) -> None:
        status, detail = runner.inspect_cache(MSVC_NINJA_CACHE, "Ninja")
        self.assertEqual(status, "ok")
        self.assertIn("ninja", detail)

    def test_accepts_visual_studio_cache(self) -> None:
        status, detail = runner.inspect_cache(
            VS_GENERATOR_CACHE, runner.VS2022_GENERATOR
        )
        self.assertEqual(status, "ok")
        self.assertIn("vs2022", detail)

    def test_cross_profile_ninja_cache_conflicts_with_vs(self) -> None:
        status, detail = runner.inspect_cache(
            MSVC_NINJA_CACHE, runner.VS2022_GENERATOR
        )
        self.assertEqual(status, "conflict")
        self.assertIn("Ninja", detail)

    def test_cross_profile_vs_cache_conflicts_with_ninja(self) -> None:
        status, detail = runner.inspect_cache(VS_GENERATOR_CACHE, "Ninja")
        self.assertEqual(status, "conflict")
        self.assertIn("Visual Studio", detail)

    def test_rejects_mingw_on_ninja_profile(self) -> None:
        status, _detail = runner.inspect_cache(MINGW_CACHE, "Ninja")
        self.assertEqual(status, "conflict")


class ConfigureDecisionTest(unittest.TestCase):
    def test_valid_vs_cache_already_configured(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            status, action, reason = runner.decide_configure_action(
                Path(tmp), VS_GENERATOR_CACHE, runner.VS2022_GENERATOR
            )
            self.assertEqual(status, runner.STATUS_OK)
            self.assertEqual(action, "already_configured")
            self.assertIsNone(reason)

    def test_cross_profile_is_build_profile_conflict(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            build_dir = Path(tmp)
            status, action, reason = runner.decide_configure_action(
                build_dir, MSVC_NINJA_CACHE, runner.VS2022_GENERATOR
            )
            self.assertEqual(status, runner.STATUS_BLOCKED)
            self.assertEqual(action, "build_profile_conflict")
            self.assertIsNotNone(reason)
            self.assertEqual(list(build_dir.iterdir()), [])


class CommandGenerationTest(unittest.TestCase):
    def test_vs_build_uses_debug_configuration(self) -> None:
        argv = runner.cmake_build_argv(
            runner.VS2022_PROFILE, ["apptraverse_event_sourced_core_test"]
        )
        self.assertEqual(
            argv,
            [
                "cmake",
                "--build",
                "--preset",
                runner.VS2022_PROFILE,
                "--config",
                "Debug",
                "--target",
                "apptraverse_event_sourced_core_test",
            ],
        )
        self.assertFalse(runner.command_is_destructive(argv))

    def test_no_clean_or_rebuild_flags(self) -> None:
        commands = [
            runner.cmake_configure_argv(runner.NINJA_PROFILE),
            runner.cmake_configure_argv(runner.VS2022_PROFILE),
            runner.cmake_build_argv(
                runner.NINJA_PROFILE, ["apptraverse_event_sourced_core_test"]
            ),
            runner.cmake_build_argv(
                runner.VS2022_PROFILE, ["apptraverse_event_sourced_core_test"]
            ),
        ]
        for argv in commands:
            joined = " ".join(argv).lower()
            self.assertNotIn("--clean-first", joined)
            self.assertNotIn("rebuild", joined)
            self.assertNotIn("/t:rebuild", joined)
            self.assertFalse(runner.command_is_destructive(argv))
            self.assertNotIn("clean", argv)

    def test_run_command_refuses_clean(self) -> None:
        with self.assertRaises(ValueError):
            runner.run_command(
                ["cmake", "--build", "--preset", "x", "--clean-first"],
                Path("."),
            )


class TimeoutTest(unittest.TestCase):
    def test_timeout_is_command_timeout(self) -> None:
        fake_proc = mock.Mock()
        fake_proc.pid = 4242
        fake_proc.wait.side_effect = subprocess.TimeoutExpired(
            cmd=["cmake"], timeout=1
        )
        with tempfile.TemporaryDirectory() as tmp:
            stdout_path = Path(tmp) / "stdout.log"
            stderr_path = Path(tmp) / "stderr.log"
            with mock.patch("subprocess.Popen", return_value=fake_proc):
                with mock.patch.object(runner, "terminate_process_tree"):
                    with self.assertRaises(runner.CommandTimeout):
                        runner.execute_external(
                            ["cmake", "--version"],
                            Path("."),
                            stdout_path,
                            stderr_path,
                            timeout_sec=1,
                        )
            self.assertTrue(stdout_path.exists())
            self.assertTrue(stderr_path.exists())


C1083_LOG = """\
warning C4100: unreferenced formal parameter
C:\\src\\mstream.h(49,1): error C1083: Cannot open include file: 'aether-miscpp/reflect/domain_visitor.h': No such file or directory [aether.vcxproj]
"""

LNK_LOG = """\
creating library
error LNK2019: unresolved external symbol foo referenced in function bar
"""

CMAKE_LOG = """\
-- Configuring incomplete, errors occurred!
CMake Error at CMakeLists.txt:10 (message):
  missing dependency
"""


class ResultContractTest(unittest.TestCase):
    def test_success_result_serialization(self) -> None:
        result = runner.BuildResult(
            status=runner.STATUS_OK,
            stage="configure",
            profile=runner.VS2022_PROFILE,
            action="already_configured",
            duration_ms=12,
            exit_code=0,
        )
        payload = result.to_public_dict()
        self.assertEqual(payload["schema_version"], runner.RESULT_SCHEMA_VERSION)
        self.assertEqual(payload["status"], "ok")
        self.assertIsNone(payload["failure_kind"])
        self.assertEqual(payload["targets"], [])

    def test_blocked_result_serialization(self) -> None:
        result = runner.BuildResult(
            status=runner.STATUS_BLOCKED,
            stage="preflight",
            profile=runner.NINJA_PROFILE,
            failure_kind="ninja_missing",
            first_error="ninja not on PATH",
            exit_code=2,
        )
        payload = result.to_public_dict()
        self.assertEqual(payload["status"], "blocked")
        self.assertEqual(payload["failure_kind"], "ninja_missing")

    def test_failed_compile_result_serialization(self) -> None:
        result = runner.BuildResult(
            run_id="20260818-000000-abc",
            artifact_id="apptraverse-build/20260818-000000-abc",
            status=runner.STATUS_FAILED,
            stage="build",
            profile=runner.VS2022_PROFILE,
            targets=["apptraverse_event_sourced_core_test"],
            duration_ms=251000,
            exit_code=1,
            failure_kind="compile_failed",
            first_error="error C1083: Cannot open include file: 'aether-miscpp/reflect/domain_visitor.h'",
        )
        payload = result.to_public_dict()
        self.assertEqual(payload["status"], "failed")
        self.assertEqual(payload["failure_kind"], "compile_failed")
        self.assertIn("C1083", payload["first_error"])
        self.assertTrue(payload["artifact_id"].startswith("apptraverse-build/"))
        self.assertNotIn(":\\", json.dumps(payload))

    def test_json_emits_one_object(self) -> None:
        result = runner.BuildResult(
            status=runner.STATUS_FAILED,
            stage="build",
            profile=runner.VS2022_PROFILE,
            failure_kind="compile_failed",
            artifact_id="apptraverse-build/run1",
            first_error="error C1083: missing",
        )
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            runner.emit_result(result, json_mode=True)
        text = buf.getvalue()
        self.assertEqual(text.count("\n"), 1)
        obj = json.loads(text)
        self.assertEqual(obj["artifact_id"], "apptraverse-build/run1")
        self.assertNotIn("C:\\", text)

    def test_stdout_stderr_stored_in_artifacts(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            stdout_path = Path(tmp) / "stdout.log"
            stderr_path = Path(tmp) / "stderr.log"
            execution = runner.execute_external(
                [
                    sys.executable,
                    "-c",
                    "import sys; sys.stdout.write('OUT_LINE\\n'); sys.stderr.write('ERR_LINE\\n')",
                ],
                Path(tmp),
                stdout_path,
                stderr_path,
            )
            self.assertEqual(execution.returncode, 0)
            self.assertIn("OUT_LINE", stdout_path.read_text(encoding="utf-8"))
            self.assertIn("ERR_LINE", stderr_path.read_text(encoding="utf-8"))

    def test_public_result_has_artifact_id_not_path(self) -> None:
        result = runner.BuildResult(
            artifact_id="apptraverse-build/xyz",
            status="failed",
            stage="build",
            profile=runner.VS2022_PROFILE,
        )
        dumped = json.dumps(result.to_public_dict())
        self.assertIn("apptraverse-build/xyz", dumped)
        self.assertNotRegex(dumped, r"[A-Za-z]:\\\\")
        self.assertNotIn(str(Path.cwd()), dumped)

    def test_msvc_c1083_first_error(self) -> None:
        first, excerpt = runner.extract_first_error(C1083_LOG)
        self.assertIn("C1083", first)
        self.assertIn("aether-miscpp/reflect/domain_visitor.h", first)
        self.assertLessEqual(len(excerpt), runner.MAX_EXCERPT_LINES)

    def test_lnk_error_extraction(self) -> None:
        first, _excerpt = runner.extract_first_error(LNK_LOG)
        self.assertIn("LNK2019", first)

    def test_cmake_error_extraction(self) -> None:
        first, _excerpt = runner.extract_first_error(CMAKE_LOG)
        self.assertIn("CMake Error", first)

    def test_excerpt_bounded_to_40_lines(self) -> None:
        lines = [f"warn {i}" for i in range(30)]
        lines.append("error C1083: missing header")
        lines.extend(f"after {i}" for i in range(30))
        _first, excerpt = runner.extract_first_error("\n".join(lines))
        self.assertLessEqual(len(excerpt), 40)

    def test_first_error_bounded_to_1000_chars(self) -> None:
        huge = "error C1083: " + ("x" * 5000)
        first, _excerpt = runner.extract_first_error(huge)
        self.assertLessEqual(len(first), 1000)

    def test_timeout_classified_command_timeout(self) -> None:
        exc = runner.CommandTimeout(
            ["cmake"], 1, artifact_id="apptraverse-build/t", first_error="command_timeout"
        )
        result = runner.BuildResult(
            status=runner.STATUS_BLOCKED,
            stage="build",
            failure_kind="command_timeout",
            artifact_id=exc.artifact_id,
            first_error=exc.first_error,
        )
        self.assertEqual(result.failure_kind, "command_timeout")
        self.assertTrue(result.artifact_id.startswith("apptraverse-build/"))


if __name__ == "__main__":
    unittest.main()
