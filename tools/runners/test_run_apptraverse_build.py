#!/usr/bin/env python3
"""Unit tests for runner decision logic. No compilation."""

from __future__ import annotations

import subprocess
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
            runner.VS2022_PROFILE, ["apptraverse_chat_component_test"]
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
                "apptraverse_chat_component_test",
            ],
        )
        self.assertFalse(runner.command_is_destructive(argv))

    def test_no_clean_or_rebuild_flags(self) -> None:
        commands = [
            runner.cmake_configure_argv(runner.NINJA_PROFILE),
            runner.cmake_configure_argv(runner.VS2022_PROFILE),
            runner.cmake_build_argv(
                runner.NINJA_PROFILE, ["apptraverse_chat_component_test"]
            ),
            runner.cmake_build_argv(
                runner.VS2022_PROFILE, ["apptraverse_chat_component_test"]
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
        fake_proc.communicate.side_effect = subprocess.TimeoutExpired(
            cmd=["cmake"], timeout=1
        )
        with mock.patch("subprocess.Popen", return_value=fake_proc):
            with mock.patch.object(runner, "terminate_process_tree"):
                with self.assertRaises(runner.CommandTimeout):
                    runner.run_command(["cmake", "--version"], Path("."))


if __name__ == "__main__":
    unittest.main()
