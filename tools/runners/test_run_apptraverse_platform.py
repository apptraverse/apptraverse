#!/usr/bin/env python3
"""Unit tests for POSIX platform runner decision logic. No compilation."""

from __future__ import annotations

import contextlib
import io
import json
import tempfile
import unittest
from pathlib import Path

from tools.runners import run_apptraverse_platform as runner


def _which_linux(name: str):
    if name in {"cmake", "ninja", "g++", "c++", "pkg-config"}:
        return f"/usr/bin/{name}"
    return None


def _which_macos(name: str):
    if name in {"cmake", "ninja", "clang++", "c++"}:
        return f"/usr/bin/{name}"
    return None


class ProfileSchemaTest(unittest.TestCase):
    def test_known_profiles_are_fixed(self) -> None:
        self.assertEqual(
            set(runner.PROFILES),
            {runner.LINUX_PROFILE, runner.MACOS_PROFILE},
        )

    def test_linux_configure_argv_uses_ninja_not_clang_preset(self) -> None:
        argv = runner.cmake_configure_argv(runner.LINUX_PROFILE, Path("/repo"))
        self.assertEqual(argv[0], "cmake")
        self.assertNotIn("--preset", argv)
        self.assertNotIn("clang++", " ".join(argv))
        self.assertIn("-G", argv)
        self.assertIn("Ninja", argv)
        self.assertIn("build/linux-x64-debug", argv)
        self.assertFalse(runner.command_is_destructive(argv))

    def test_macos_configure_argv_is_defined_without_running(self) -> None:
        argv = runner.cmake_configure_argv(runner.MACOS_PROFILE, Path("/repo"))
        self.assertEqual(argv[0], "cmake")
        self.assertNotIn("single_client_chat", " ".join(argv))
        self.assertIn("build/macos-x64-debug", argv)
        self.assertIn("Ninja", argv)
        self.assertIn("-DCMAKE_OSX_ARCHITECTURES=x86_64", argv)
        self.assertFalse(runner.command_is_destructive(argv))

    def test_linux_build_argv_targets_chat_without_clean(self) -> None:
        argv = runner.cmake_build_argv(
            runner.LINUX_PROFILE, ["apptraverse_event_sourced_core_test"]
        )
        self.assertEqual(
            argv[:5],
            ["cmake", "--build", "build/linux-x64-debug", "--target", "apptraverse_event_sourced_core_test"],
        )
        self.assertNotIn("--clean-first", argv)
        self.assertNotIn("clean", argv)
        self.assertNotIn("rebuild", argv)
        self.assertNotIn("--parallel", argv)
        self.assertFalse(runner.command_is_destructive(argv))

    def test_macos_build_argv_is_defined(self) -> None:
        argv = runner.cmake_build_argv(
            runner.MACOS_PROFILE, ["apptraverse_event_sourced_core_test"]
        )
        self.assertIn("build/macos-x64-debug", argv)
        self.assertIn("apptraverse_event_sourced_core_test", argv)
        self.assertFalse(runner.command_is_destructive(argv))

    def test_process_argv_linux_is_exe_only(self) -> None:
        argv = runner.process_argv(
            Path("/repo"), runner.LINUX_PROFILE, "/tmp/unused"
        )
        self.assertEqual(len(argv), 1)
        self.assertTrue(argv[0].endswith("apptraverse_event_sourced_core_test"))

    def test_process_argv_macos_is_exe_only(self) -> None:
        argv = runner.process_argv(Path("/repo"), runner.MACOS_PROFILE, "/tmp/ignored")
        self.assertEqual(len(argv), 1)
        self.assertTrue(argv[0].endswith("apptraverse_event_sourced_core_test"))

    def test_user_config_flag_when_header_exists(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            source = Path(tmp)
            header = source / runner.USER_CONFIG_REL
            header.parent.mkdir(parents=True)
            header.write_text("/* test */\n", encoding="utf-8")
            argv = runner.cmake_configure_argv(runner.LINUX_PROFILE, source)
        self.assertTrue(any(part.startswith("-DUSER_CONFIG=") for part in argv))


class PreflightTest(unittest.TestCase):
    def test_linux_preflight_ok(self) -> None:
        status, kind, reason = runner.preflight(
            runner.LINUX_PROFILE,
            Path("."),
            platform="linux",
            which=_which_linux,
            gtk3_ok=True,
        )
        self.assertEqual(status, runner.STATUS_OK)
        self.assertIsNone(kind)
        self.assertIsNone(reason)

    def test_macos_on_linux_is_wrong_host_os(self) -> None:
        status, kind, reason = runner.preflight(
            runner.MACOS_PROFILE,
            Path("."),
            platform="linux",
            which=_which_macos,
            gtk3_ok=True,
        )
        self.assertEqual(status, runner.STATUS_BLOCKED)
        self.assertEqual(kind, "wrong_host_os")
        self.assertEqual(reason, "linux")

    def test_linux_on_darwin_is_wrong_host_os(self) -> None:
        status, kind, reason = runner.preflight(
            runner.LINUX_PROFILE,
            Path("."),
            platform="darwin",
            which=_which_linux,
            gtk3_ok=True,
        )
        self.assertEqual(kind, "wrong_host_os")
        self.assertEqual(reason, "darwin")

    def test_macos_preflight_ok_on_darwin(self) -> None:
        status, kind, _reason = runner.preflight(
            runner.MACOS_PROFILE,
            Path("."),
            platform="darwin",
            which=_which_macos,
            macports_clang20_ok=True,
        )
        self.assertEqual(status, runner.STATUS_OK)
        self.assertIsNone(kind)

    def test_unknown_profile_rejected(self) -> None:
        status, kind, reason = runner.preflight(
            "win64-ninja-msvc-debug",
            Path("."),
            platform="linux",
            which=_which_linux,
            gtk3_ok=True,
        )
        self.assertEqual(status, runner.STATUS_BLOCKED)
        self.assertEqual(kind, "unsupported_profile")
        self.assertEqual(reason, "win64-ninja-msvc-debug")

    def test_missing_ninja(self) -> None:
        status, kind, _reason = runner.preflight(
            runner.LINUX_PROFILE,
            Path("."),
            platform="linux",
            which=lambda name: "/usr/bin/cmake" if name in {"cmake", "g++", "pkg-config"} else None,
            gtk3_ok=True,
        )
        self.assertEqual(kind, "ninja_missing")


class ConfigureDecisionTest(unittest.TestCase):
    def test_missing_build_dir_configures(self) -> None:
        status, action, reason = runner.decide_configure_action(
            Path("/missing-build"), None, "Ninja"
        )
        self.assertEqual(status, runner.STATUS_OK)
        self.assertEqual(action, "configure")
        self.assertIsNone(reason)

    def test_matching_cache_is_already_configured(self) -> None:
        cache = "CMAKE_GENERATOR:INTERNAL=Ninja\n"
        status, action, reason = runner.decide_configure_action(
            Path("."), cache, "Ninja"
        )
        self.assertEqual(status, runner.STATUS_OK)
        self.assertEqual(action, "already_configured")
        self.assertIsNone(reason)

    def test_generator_conflict_is_blocked(self) -> None:
        cache = "CMAKE_GENERATOR:INTERNAL=Unix Makefiles\n"
        status, action, reason = runner.decide_configure_action(
            Path("."), cache, "Ninja"
        )
        self.assertEqual(status, runner.STATUS_BLOCKED)
        self.assertEqual(action, "build_profile_conflict")
        self.assertIn("Unix Makefiles", reason or "")


class ExcerptAndJsonTest(unittest.TestCase):
    def test_excerpt_bounded_to_40_lines_and_4000_chars(self) -> None:
        lines = [f"warn {i}" for i in range(30)]
        lines.append("error: missing header")
        lines.extend(f"after {i}" for i in range(80))
        first, excerpt = runner.extract_first_error("\n".join(lines))
        self.assertIn("error:", first)
        self.assertLessEqual(len(excerpt), 40)
        self.assertLessEqual(len("\n".join(excerpt)), 4000)

    def test_bound_excerpt_text_clips_chars(self) -> None:
        huge = "x" * 8000
        clipped = runner.bound_excerpt_text(huge)
        self.assertLessEqual(len(clipped), 4000)

    def test_json_emits_one_object(self) -> None:
        result = runner.PlatformResult(
            artifact_id="apptraverse-platform/r-1",
            status="ok",
            stage="preflight",
            profile=runner.LINUX_PROFILE,
        )
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            runner.emit_result(result, json_mode=True)
        text = buf.getvalue()
        self.assertEqual(text.count("\n"), 1)
        obj = json.loads(text)
        self.assertEqual(obj["artifact_id"], "apptraverse-platform/r-1")
        dumped = json.dumps(obj)
        self.assertNotIn("stdout", dumped)
        self.assertNotIn("stderr", dumped)

    def test_default_linux_target(self) -> None:
        self.assertEqual(
            runner.default_targets(runner.LINUX_PROFILE, []),
            ["apptraverse_event_sourced_core_test"],
        )

    def test_default_macos_target(self) -> None:
        self.assertEqual(
            runner.default_targets(runner.MACOS_PROFILE, []),
            ["apptraverse_event_sourced_core_test"],
        )

    def test_destructive_commands_refused(self) -> None:
        self.assertTrue(runner.command_is_destructive(["cmake", "--build", "x", "--clean-first"]))
        self.assertTrue(runner.command_is_destructive(["ninja", "clean"]))
        self.assertFalse(runner.command_is_destructive(["cmake", "--build", "x", "--target", "t"]))


if __name__ == "__main__":
    unittest.main()
