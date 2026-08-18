#!/usr/bin/env python3
"""Unit tests for runner decision logic. No compilation."""

from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

from tools.runners import run_apptraverse_build as runner

MSVC_CACHE = """\
CMAKE_BUILD_TYPE:STRING=Debug
CMAKE_GENERATOR:INTERNAL=Ninja
CMAKE_CXX_COMPILER:FILEPATH=C:/Program Files/Microsoft Visual Studio/18/Community/VC/Tools/MSVC/14.50.35717/bin/Hostx64/x64/cl.exe
"""

MINGW_CACHE = """\
CMAKE_BUILD_TYPE:STRING=Debug
CMAKE_GENERATOR:INTERNAL=Ninja
CMAKE_CXX_COMPILER:FILEPATH=C:/msys64/ucrt64/bin/c++.exe
"""

VS_GENERATOR_CACHE = """\
CMAKE_GENERATOR:INTERNAL=Visual Studio 17 2022
CMAKE_CXX_COMPILER:FILEPATH=C:/Program Files/Microsoft Visual Studio/18/Community/VC/Tools/MSVC/14.50.35717/bin/Hostx64/x64/cl.exe
"""


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

    def test_rejects_macos_profile(self) -> None:
        status, kind, _reason = runner.preflight(
            "macos-x64-ninja-appleclang-debug",
            Path("."),
            platform="win32",
            which=lambda _name: "fake",
        )
        self.assertEqual(kind, "unsupported_profile")


class PlatformPreflightTest(unittest.TestCase):
    def test_non_windows_is_blocked(self) -> None:
        status, kind, reason = runner.preflight(
            runner.SUPPORTED_PROFILE,
            Path("."),
            platform="linux",
            which=lambda _name: "fake",
        )
        self.assertEqual(status, runner.STATUS_BLOCKED)
        self.assertEqual(kind, "os_not_windows")
        self.assertEqual(reason, "linux")


class CacheParserTest(unittest.TestCase):
    def test_recognizes_ninja(self) -> None:
        self.assertEqual(runner.parse_cache_generator(MSVC_CACHE), "Ninja")
        status, detail = runner.inspect_cache(MSVC_CACHE)
        self.assertEqual(status, "ok")
        self.assertIn("ninja", detail)

    def test_rejects_other_generator(self) -> None:
        self.assertEqual(
            runner.parse_cache_generator(VS_GENERATOR_CACHE),
            "Visual Studio 17 2022",
        )
        status, detail = runner.inspect_cache(VS_GENERATOR_CACHE)
        self.assertEqual(status, "conflict")
        self.assertIn("Visual Studio", detail)

    def test_recognizes_msvc_compiler(self) -> None:
        compiler = runner.parse_cache_cxx_compiler(MSVC_CACHE)
        self.assertTrue(compiler and runner.compiler_is_msvc_cl(compiler))

    def test_rejects_mingw_compiler(self) -> None:
        compiler = runner.parse_cache_cxx_compiler(MINGW_CACHE)
        self.assertFalse(runner.compiler_is_msvc_cl(compiler or ""))
        status, _detail = runner.inspect_cache(MINGW_CACHE)
        self.assertEqual(status, "conflict")


class ConfigureDecisionTest(unittest.TestCase):
    def test_missing_dir_configures(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            missing = Path(tmp) / "build" / "win64-ninja-msvc-debug"
            status, action, reason = runner.decide_configure_action(missing, None)
            self.assertEqual(status, runner.STATUS_OK)
            self.assertEqual(action, "configure")
            self.assertIsNone(reason)

    def test_valid_cache_is_already_configured(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            build_dir = Path(tmp)
            status, action, reason = runner.decide_configure_action(
                build_dir, MSVC_CACHE
            )
            self.assertEqual(status, runner.STATUS_OK)
            self.assertEqual(action, "already_configured")
            self.assertIsNone(reason)

    def test_conflict_does_not_reconfigure(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            build_dir = Path(tmp)
            status, action, reason = runner.decide_configure_action(
                build_dir, MINGW_CACHE
            )
            self.assertEqual(status, runner.STATUS_BLOCKED)
            self.assertEqual(action, "build_profile_conflict")
            self.assertIsNotNone(reason)
            self.assertTrue(build_dir.exists())
            self.assertEqual(list(build_dir.iterdir()), [])


class CommandGenerationTest(unittest.TestCase):
    def test_configure_uses_preset_only(self) -> None:
        argv = runner.cmake_configure_argv(runner.SUPPORTED_PROFILE)
        self.assertEqual(argv, ["cmake", "--preset", runner.SUPPORTED_PROFILE])
        self.assertFalse(runner.command_is_destructive(argv))

    def test_build_is_incremental_and_named(self) -> None:
        argv = runner.cmake_build_argv(
            runner.SUPPORTED_PROFILE, ["apptraverse_chat_component_test"]
        )
        self.assertEqual(
            argv,
            [
                "cmake",
                "--build",
                "--preset",
                runner.SUPPORTED_PROFILE,
                "--target",
                "apptraverse_chat_component_test",
            ],
        )
        self.assertFalse(runner.command_is_destructive(argv))

    def test_no_clean_or_rebuild_flags(self) -> None:
        configure_argv = runner.cmake_configure_argv(runner.SUPPORTED_PROFILE)
        build_argv = runner.cmake_build_argv(
            runner.SUPPORTED_PROFILE, ["apptraverse_chat_component_test"]
        )
        for argv in (configure_argv, build_argv):
            joined = " ".join(argv).lower()
            self.assertNotIn("--clean-first", joined)
            self.assertNotIn("clean", joined)
            self.assertNotIn("rebuild", joined)
            self.assertFalse(runner.command_is_destructive(argv))

    def test_run_command_refuses_clean(self) -> None:
        with self.assertRaises(ValueError):
            runner.run_command(
                ["cmake", "--build", "--preset", "x", "--clean-first"],
                Path("."),
            )


if __name__ == "__main__":
    unittest.main()
