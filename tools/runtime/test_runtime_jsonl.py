#!/usr/bin/env python3
"""Unit tests for runtime JSONL parser/query and MCP runtime log tool."""

from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from tools.mcp import apptraverse_mcp as mcp_mod
from tools.runtime import runtime_jsonl as runtime_mod


def _sample_record(
    seq: int,
    event: str,
    *,
    run_id: str = "run-123",
    instance: str = "win-a",
) -> dict:
    return {
        "schema_version": runtime_mod.RUNTIME_SCHEMA_VERSION,
        "run_id": run_id,
        "seq": seq,
        "event": event,
        "platform": "windows",
        "instance": instance,
        "pid": 1234,
        "t_us": 1787040000000000 + seq,
        "mono_us": 1000 + seq,
        "data": {"text": "hello", "accepted": True},
    }


class RuntimeJsonlParserTest(unittest.TestCase):
    def _write_lines(self, lines: list[dict]) -> Path:
        handle = tempfile.NamedTemporaryFile("w", encoding="utf-8", delete=False)
        path = Path(handle.name)
        try:
            for line in lines:
                handle.write(json.dumps(line) + "\n")
        finally:
            handle.close()
        self.addCleanup(lambda: path.unlink(missing_ok=True))
        return path

    def test_valid_jsonl_parsing(self) -> None:
        path = self._write_lines([_sample_record(1, "runtime_started")])
        records = [record for _line, record in runtime_mod.iter_records(path)]
        self.assertEqual(len(records), 1)
        self.assertEqual(records[0]["event"], "runtime_started")

    def test_multiple_events(self) -> None:
        path = self._write_lines(
            [
                _sample_record(1, "runtime_started"),
                _sample_record(2, "text_submit"),
            ]
        )
        records, has_more = runtime_mod.query_records(path)
        self.assertEqual(len(records), 2)
        self.assertFalse(has_more)

    def test_event_filter(self) -> None:
        path = self._write_lines(
            [
                _sample_record(1, "runtime_started"),
                _sample_record(2, "text_submit"),
                _sample_record(3, "message_visible"),
            ]
        )
        records, _ = runtime_mod.query_records(path, event="message_visible")
        self.assertEqual([record["event"] for record in records], ["message_visible"])

    def test_after_seq_filter(self) -> None:
        path = self._write_lines(
            [
                _sample_record(1, "runtime_started"),
                _sample_record(2, "text_submit"),
                _sample_record(3, "presentation"),
            ]
        )
        records, _ = runtime_mod.query_records(path, after_seq=1)
        self.assertEqual([record["seq"] for record in records], [2, 3])

    def test_limit(self) -> None:
        path = self._write_lines(
            [_sample_record(seq, "presentation") for seq in range(1, 6)]
        )
        records, has_more = runtime_mod.query_records(path, limit=2)
        self.assertEqual(len(records), 2)
        self.assertTrue(has_more)

    def test_max_limit_enforcement(self) -> None:
        with self.assertRaises(ValueError):
            runtime_mod.query_records(Path("unused"), limit=101)

    def test_malformed_json_line_number(self) -> None:
        handle = tempfile.NamedTemporaryFile("w", encoding="utf-8", delete=False)
        path = Path(handle.name)
        handle.write('{"broken":\n')
        handle.close()
        self.addCleanup(lambda: path.unlink(missing_ok=True))
        with self.assertRaises(runtime_mod.RuntimeJsonlError) as ctx:
            list(runtime_mod.iter_records(path))
        self.assertIn("line 1", str(ctx.exception))

    def test_missing_required_envelope_field(self) -> None:
        path = self._write_lines([{"schema_version": runtime_mod.RUNTIME_SCHEMA_VERSION}])
        with self.assertRaises(runtime_mod.RuntimeJsonlError) as ctx:
            list(runtime_mod.iter_records(path))
        self.assertIn("missing required envelope field", str(ctx.exception))

    def test_empty_file(self) -> None:
        handle = tempfile.NamedTemporaryFile("w", encoding="utf-8", delete=False)
        path = Path(handle.name)
        handle.close()
        self.addCleanup(lambda: path.unlink(missing_ok=True))
        records, has_more = runtime_mod.query_records(path)
        self.assertEqual(records, [])
        self.assertFalse(has_more)


class RuntimeArtifactValidationTest(unittest.TestCase):
    def test_parse_valid_artifact_id(self) -> None:
        self.assertEqual(
            runtime_mod.parse_runtime_artifact_id("apptraverse-runtime/run-1/win-a"),
            ("run-1", "win-a"),
        )

    def test_reject_traversal_and_wrong_prefix(self) -> None:
        cases = [
            r"C:\Windows\system32\config",
            "/etc/passwd",
            "apptraverse-runtime/../secret",
            "apptraverse-runtime/run/extra/level",
            "apptraverse-build/run-1",
            "stdout.log",
        ]
        for artifact_id in cases:
            self.assertIsNone(
                runtime_mod.parse_runtime_artifact_id(artifact_id), artifact_id
            )


class RuntimeMcpQueryTest(unittest.TestCase):
    def test_bounded_mcp_result(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            run_id = "run-abc"
            instance = "win-a"
            artifact_id = f"apptraverse-runtime/{run_id}/{instance}"
            log_dir = root / ".artifacts" / "apptraverse-runtime" / run_id
            log_dir.mkdir(parents=True)
            log_path = log_dir / f"{instance}.jsonl"
            lines = [_sample_record(seq, "presentation", run_id=run_id, instance=instance)
                     for seq in range(1, 6)]
            log_path.write_text(
                "\n".join(json.dumps(line) for line in lines) + "\n",
                encoding="utf-8",
            )
            with mock.patch.object(mcp_mod, "repo_root", return_value=root):
                dumped = mcp_mod.apptraverse_runtime_log_query(
                    artifact_id, limit=2
                )
            self.assertIsNone(dumped["failure_kind"])
            self.assertEqual(dumped["returned_count"], 2)
            self.assertTrue(dumped["has_more"])
            self.assertEqual(len(dumped["records"]), 2)
            self.assertNotIn("C:", json.dumps(dumped))

    def test_invalid_limit(self) -> None:
        dumped = mcp_mod.apptraverse_runtime_log_query(
            "apptraverse-runtime/run-1/win-a", limit=200
        )
        self.assertEqual(dumped["failure_kind"], "invalid_limit")

    def test_no_full_file_return(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            artifact_id = "apptraverse-runtime/run-big/win-a"
            log_dir = root / ".artifacts" / "apptraverse-runtime" / "run-big"
            log_dir.mkdir(parents=True)
            log_path = log_dir / "win-a.jsonl"
            lines = [_sample_record(seq, "presentation") for seq in range(1, 201)]
            log_path.write_text(
                "\n".join(json.dumps(line) for line in lines) + "\n",
                encoding="utf-8",
            )
            with mock.patch.object(mcp_mod, "repo_root", return_value=root):
                dumped = mcp_mod.apptraverse_runtime_log_query(artifact_id, limit=100)
            self.assertEqual(dumped["returned_count"], 100)
            self.assertTrue(dumped["has_more"])


class RuntimeMcpToolRegistryTest(unittest.TestCase):
    def test_server_exposes_six_tools(self) -> None:
        self.assertEqual(len(mcp_mod.TOOL_NAMES), 13)
        self.assertIn("apptraverse_runtime_log_query", mcp_mod.TOOL_NAMES)
        self.assertIn("apptraverse_two_windows_chat_run", mcp_mod.TOOL_NAMES)
        self.assertIn("apptraverse_platform_start", mcp_mod.TOOL_NAMES)
        self.assertIn("apptraverse_process_start", mcp_mod.TOOL_NAMES)


if __name__ == "__main__":
    unittest.main()
