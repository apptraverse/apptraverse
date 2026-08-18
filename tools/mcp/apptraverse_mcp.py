#!/usr/bin/env python3
"""Thin Cursor MCP wrapper over the App Traverse job controller.

Stdout is reserved for the MCP protocol. Diagnostics go to stderr/logging.
Repo root is derived from this file, never from cwd.
"""

from __future__ import annotations

import json
import logging
import sys
from pathlib import Path

_SCRIPT_DIR = Path(__file__).resolve().parent
_REPO_ROOT = Path(__file__).resolve().parents[2]
_script_dir_str = str(_SCRIPT_DIR)
while _script_dir_str in sys.path:
    sys.path.remove(_script_dir_str)
if str(_REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(_REPO_ROOT))

from tools.runners.run_apptraverse_job import (  # noqa: E402
    cancel_job,
    start_job,
    status_job,
)

LOG = logging.getLogger("apptraverse_mcp")
TOOL_NAMES = (
    "apptraverse_build_start",
    "apptraverse_build_status",
    "apptraverse_build_cancel",
    "apptraverse_build_failure_excerpt",
)
BUILD_ARTIFACT_PREFIX = "apptraverse-build/"
MAX_EXCERPT_LINES = 40
MAX_EXCERPT_CHARS = 4000
ALLOWED_RUN_ID = set("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-")


def repo_root() -> Path:
    return Path(__file__).resolve().parents[2]


def apptraverse_build_start(
    profile: str,
    stage: str,
    targets: list[str] | None = None,
) -> dict:
    """Start a background App Traverse build job. Returns a compact job object."""
    result = start_job(repo_root(), profile, stage, list(targets or []))
    return result.to_public_dict()


def apptraverse_build_status(job_id: str) -> dict:
    """Return compact status for an App Traverse background build job."""
    return status_job(repo_root(), job_id).to_public_dict()


def apptraverse_build_cancel(job_id: str) -> dict:
    """Cancel an App Traverse background build job."""
    return cancel_job(repo_root(), job_id).to_public_dict()


def _invalid_artifact(artifact_id: str, kind: str) -> dict:
    return {
        "artifact_id": artifact_id,
        "failure_kind": kind,
        "first_error": kind,
        "excerpt": "",
    }


def parse_build_run_id(artifact_id: str) -> str | None:
    if not isinstance(artifact_id, str) or not artifact_id:
        return None
    if artifact_id.strip() != artifact_id:
        return None
    candidate = Path(artifact_id)
    if candidate.is_absolute():
        return None
    if ".." in artifact_id or "\\" in artifact_id:
        return None
    if not artifact_id.startswith(BUILD_ARTIFACT_PREFIX):
        return None
    run_id = artifact_id[len(BUILD_ARTIFACT_PREFIX) :]
    if not run_id or "/" in run_id:
        return None
    if run_id in {".", ".."} or run_id.startswith("."):
        return None
    if any(ch not in ALLOWED_RUN_ID for ch in run_id):
        return None
    return run_id


def bound_excerpt(text: str) -> str:
    lines = text.splitlines()
    if len(lines) > MAX_EXCERPT_LINES:
        lines = lines[:MAX_EXCERPT_LINES]
    clipped = "\n".join(lines)
    if len(clipped) > MAX_EXCERPT_CHARS:
        clipped = clipped[:MAX_EXCERPT_CHARS]
    return clipped


def apptraverse_build_failure_excerpt(artifact_id: str) -> dict:
    """Return a bounded failure excerpt for an apptraverse-build artifact id."""
    run_id = parse_build_run_id(artifact_id)
    if run_id is None:
        return _invalid_artifact(artifact_id, "invalid_artifact_id")
    root = repo_root()
    base = (root / ".artifacts" / "apptraverse-build").resolve()
    run_dir = (base / run_id).resolve()
    try:
        run_dir.relative_to(base)
    except ValueError:
        return _invalid_artifact(artifact_id, "invalid_artifact_id")
    if not run_dir.is_dir():
        return _invalid_artifact(artifact_id, "artifact_not_found")

    failure_kind = None
    first_error = None
    result_path = run_dir / "result.json"
    if result_path.is_file():
        try:
            payload = json.loads(result_path.read_text(encoding="utf-8"))
        except json.JSONDecodeError:
            payload = {}
        if isinstance(payload, dict):
            failure_kind = payload.get("failure_kind")
            first_error = payload.get("first_error")

    excerpt = ""
    excerpt_path = run_dir / "failure_excerpt.txt"
    if excerpt_path.is_file():
        excerpt = excerpt_path.read_text(encoding="utf-8", errors="replace")
    elif isinstance(first_error, str):
        excerpt = first_error
    return {
        "artifact_id": artifact_id,
        "failure_kind": failure_kind,
        "first_error": first_error,
        "excerpt": bound_excerpt(excerpt),
    }


def create_mcp_server():
    from mcp.server import MCPServer

    server = MCPServer("apptraverse")
    server.tool()(apptraverse_build_start)
    server.tool()(apptraverse_build_status)
    server.tool()(apptraverse_build_cancel)
    server.tool()(apptraverse_build_failure_excerpt)
    return server


def registered_tool_names(server) -> list[str]:
    listed = server._tool_manager.list_tools()
    return [item.name for item in listed]


mcp = None
try:
    mcp = create_mcp_server()
except ImportError:
    mcp = None


if __name__ == "__main__":
    logging.basicConfig(stream=sys.stderr, level=logging.INFO)
    if mcp is None:
        sys.stderr.write("mcp package is required to run the App Traverse MCP server\n")
        raise SystemExit(1)
    mcp.run()
