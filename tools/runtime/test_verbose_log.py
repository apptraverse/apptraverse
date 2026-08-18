"""Logical tests for quiet-by-default Windows diagnostic logging."""

from __future__ import annotations

import unittest

from tools.runtime.verbose_log import runtime_jsonl_enabled, verbose_log_enabled


class VerboseLogPolicyTest(unittest.TestCase):
    def test_default_is_off(self) -> None:
        self.assertFalse(verbose_log_enabled({}))

    def test_one_is_on(self) -> None:
        self.assertTrue(verbose_log_enabled({"APPTRAVERSE_VERBOSE_LOG": "1"}))

    def test_true_aliases_are_on(self) -> None:
        for value in ("true", "TRUE", "yes", "On"):
            self.assertTrue(verbose_log_enabled({"APPTRAVERSE_VERBOSE_LOG": value}))

    def test_zero_and_empty_are_off(self) -> None:
        self.assertFalse(verbose_log_enabled({"APPTRAVERSE_VERBOSE_LOG": "0"}))
        self.assertFalse(verbose_log_enabled({"APPTRAVERSE_VERBOSE_LOG": ""}))
        self.assertFalse(verbose_log_enabled({"APPTRAVERSE_VERBOSE_LOG": "false"}))

    def test_jsonl_independent_of_verbose(self) -> None:
        env = {"APPTRAVERSE_RUNTIME_JSONL": "C:/tmp/runtime.jsonl"}
        self.assertTrue(runtime_jsonl_enabled(env))
        self.assertFalse(verbose_log_enabled(env))
        env["APPTRAVERSE_VERBOSE_LOG"] = "1"
        self.assertTrue(runtime_jsonl_enabled(env))
        self.assertTrue(verbose_log_enabled(env))
        env2 = {"APPTRAVERSE_VERBOSE_LOG": "1"}
        self.assertFalse(runtime_jsonl_enabled(env2))
        self.assertTrue(verbose_log_enabled(env2))


if __name__ == "__main__":
    unittest.main()
