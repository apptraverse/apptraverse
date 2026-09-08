#!/usr/bin/env python3
"""Thin Cursor MCP wrapper over the App Traverse job controller.

Stdout is reserved for the MCP protocol. Diagnostics go to stderr/logging.
The default checkout is the tree that launched this server, never cwd.
Optional source_dir selects another App Traverse git worktree.
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

from tools.mcp.source_dir import (  # noqa: E402
    SourceDirError,
    error_payload,
    remember_source,
    resolve_existing_job_source,
    resolve_source_dir,
)
from tools.runners.run_apptraverse_job import (  # noqa: E402
    JOB_SCHEMA_VERSION,
    cancel_job,
    job_dir_for as build_job_dir_for,
    start_job,
    status_job,
)
from tools.runners.run_apptraverse_platform_job import (  # noqa: E402
    JOB_SCHEMA_VERSION as PLATFORM_JOB_SCHEMA_VERSION,
    PROCESS_SCHEMA_VERSION,
    cancel_job as cancel_platform_job,
    job_dir_for as platform_job_dir_for,
    process_dir_for,
    start_job as start_platform_job,
    start_process,
    status_job as status_platform_job,
    status_process,
    stop_process,
)
from tools.runtime.runtime_jsonl import (  # noqa: E402
    MAX_LIMIT,
    RuntimeJsonlError,
    parse_runtime_artifact_id,
    query_records,
    resolve_runtime_log_path,
)

LOG = logging.getLogger("apptraverse_mcp")
TOOL_NAMES = (
    "apptraverse_build_start",
    "apptraverse_build_status",
    "apptraverse_build_cancel",
    "apptraverse_build_failure_excerpt",
    "apptraverse_runtime_log_query",
    "apptraverse_platform_start",
    "apptraverse_platform_status",
    "apptraverse_platform_cancel",
    "apptraverse_platform_failure_excerpt",
    "apptraverse_process_start",
    "apptraverse_process_status",
    "apptraverse_process_stop",
    "apptraverse_chat_headless_test_start",
    "apptraverse_chat_p2p_headless_test_start",
)
BUILD_ARTIFACT_PREFIX = "apptraverse-build/"
PLATFORM_ARTIFACT_PREFIX = "apptraverse-platform/"
RUNTIME_ARTIFACT_PREFIX = "apptraverse-runtime/"
MAX_EXCERPT_LINES = 40
MAX_EXCERPT_CHARS = 4000
DEFAULT_RUNTIME_QUERY_LIMIT = 50
MAX_RUNTIME_QUERY_LIMIT = 100
ALLOWED_RUN_ID = set("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-")


def repo_root() -> Path:
    return Path(__file__).resolve().parents[2]


def _checkout(source_dir: str | None) -> Path:
    return resolve_source_dir(source_dir, default_root=repo_root())


def _start_error(operation: str, exc: SourceDirError, *, platform: bool = False) -> dict:
    schema = PLATFORM_JOB_SCHEMA_VERSION if platform else JOB_SCHEMA_VERSION
    payload = error_payload(operation, exc.message, schema_version=schema)
    if not platform:
        payload.pop("platform_result", None)
    else:
        payload.pop("build_result", None)
    return payload


def _public(result, source: Path) -> dict:
    dumped = result.to_public_dict()
    dumped["source_dir"] = str(source)
    return dumped


def _job_exists_build(root: Path, job_id: str) -> bool:
    return build_job_dir_for(root, job_id).is_dir()


def _job_exists_platform(root: Path, job_id: str) -> bool:
    return platform_job_dir_for(root, job_id).is_dir()


def _process_exists(root: Path, process_id: str) -> bool:
    return process_dir_for(root, process_id).is_dir()


def apptraverse_build_start(
    profile: str,
    stage: str,
    targets: list[str] | None = None,
    source_dir: str | None = None,
) -> dict:
    """Start a background App Traverse build job. Returns a compact job object."""
    try:
        root = _checkout(source_dir)
    except SourceDirError as exc:
        return _start_error("start", exc)
    result = start_job(root, profile, stage, list(targets or []))
    if result.job_id:
        remember_source(repo_root(), "build", result.job_id, root)
    return _public(result, root)


def apptraverse_build_status(job_id: str, source_dir: str | None = None) -> dict:
    """Return compact status for an App Traverse background build job."""
    try:
        root = resolve_existing_job_source(
            source_dir,
            job_id,
            "build",
            default_root=repo_root(),
            job_exists=_job_exists_build,
        )
    except SourceDirError as exc:
        return _start_error("status", exc)
    return _public(status_job(root, job_id), root)


def apptraverse_build_cancel(job_id: str, source_dir: str | None = None) -> dict:
    """Cancel an App Traverse background build job."""
    try:
        root = resolve_existing_job_source(
            source_dir,
            job_id,
            "build",
            default_root=repo_root(),
            job_exists=_job_exists_build,
        )
    except SourceDirError as exc:
        return _start_error("cancel", exc)
    return _public(cancel_job(root, job_id), root)


def apptraverse_platform_start(
    profile: str,
    stage: str,
    targets: list[str] | None = None,
    source_dir: str | None = None,
) -> dict:
    """Start a background POSIX platform job. Returns a compact job object."""
    try:
        root = _checkout(source_dir)
    except SourceDirError as exc:
        return _start_error("start", exc, platform=True)
    result = start_platform_job(root, profile, stage, list(targets or []))
    if result.job_id:
        remember_source(repo_root(), "platform", result.job_id, root)
    return _public(result, root)


def apptraverse_platform_status(job_id: str, source_dir: str | None = None) -> dict:
    """Return compact status for a POSIX platform background job."""
    try:
        root = resolve_existing_job_source(
            source_dir,
            job_id,
            "platform",
            default_root=repo_root(),
            job_exists=_job_exists_platform,
        )
    except SourceDirError as exc:
        return _start_error("status", exc, platform=True)
    return _public(status_platform_job(root, job_id), root)


def apptraverse_platform_cancel(job_id: str, source_dir: str | None = None) -> dict:
    """Cancel a POSIX platform background job."""
    try:
        root = resolve_existing_job_source(
            source_dir,
            job_id,
            "platform",
            default_root=repo_root(),
            job_exists=_job_exists_platform,
        )
    except SourceDirError as exc:
        return _start_error("cancel", exc, platform=True)
    return _public(cancel_platform_job(root, job_id), root)


def apptraverse_process_start(
    profile: str, state_dir: str, source_dir: str | None = None
) -> dict:
    """Start the known-profile product process with an explicit state dir."""
    try:
        root = _checkout(source_dir)
    except SourceDirError as exc:
        payload = error_payload(
            "start", exc.message, schema_version=PROCESS_SCHEMA_VERSION
        )
        payload.pop("build_result", None)
        payload.pop("platform_result", None)
        payload.pop("targets", None)
        payload.pop("stage", None)
        payload["process_id"] = None
        return payload
    result = start_process(root, profile, state_dir)
    if result.process_id:
        remember_source(repo_root(), "process", result.process_id, root)
    return _public(result, root)


def apptraverse_process_status(
    process_id: str, source_dir: str | None = None
) -> dict:
    """Return compact status for a known-profile product process."""
    try:
        root = resolve_existing_job_source(
            source_dir,
            process_id,
            "process",
            default_root=repo_root(),
            job_exists=_process_exists,
        )
    except SourceDirError as exc:
        payload = error_payload(
            "status", exc.message, schema_version=PROCESS_SCHEMA_VERSION
        )
        payload.pop("build_result", None)
        payload.pop("platform_result", None)
        payload["process_id"] = process_id
        return payload
    return _public(status_process(root, process_id), root)


def apptraverse_process_stop(
    process_id: str, source_dir: str | None = None
) -> dict:
    """Stop a known-profile product process."""
    try:
        root = resolve_existing_job_source(
            source_dir,
            process_id,
            "process",
            default_root=repo_root(),
            job_exists=_process_exists,
        )
    except SourceDirError as exc:
        payload = error_payload(
            "stop", exc.message, schema_version=PROCESS_SCHEMA_VERSION
        )
        payload.pop("build_result", None)
        payload.pop("platform_result", None)
        payload["process_id"] = process_id
        return payload
    return _public(stop_process(root, process_id), root)


def apptraverse_chat_headless_test_start(
    profile: str = "win64-ninja-msvc-debug",
    source_dir: str | None = None,
) -> dict:
    """Canonical first test for App Traverse chat/shared/presentation behavior.
    Runs headlessly without a product process and without Model→UI mirror.
    Use mirror/native tests only when the task explicitly changes mirror
    serialization or native rendering."""
    try:
        root = _checkout(source_dir)
    except SourceDirError as exc:
        return _start_error("start", exc)
    result = start_job(
        root,
        profile,
        "build",
        ["apptraverse_chat_headless_check"],
    )
    if result.job_id:
        remember_source(repo_root(), "build", result.job_id, root)
    return _public(result, root)


def apptraverse_chat_p2p_headless_test_start(
    profile: str = "win64-ninja-msvc-debug",
    source_dir: str | None = None,
) -> dict:
    """Real Aether P2P headless chat journal convergence test.
    Model Domain + ChatAetherRuntime + SharedRuntime only — no UiMirror,
    UI Domain, presenters, HWND, or product process."""
    try:
        root = _checkout(source_dir)
    except SourceDirError as exc:
        return _start_error("start", exc)
    result = start_job(
        root,
        profile,
        "build",
        ["apptraverse_chat_p2p_headless_test"],
    )
    if result.job_id:
        remember_source(repo_root(), "build", result.job_id, root)
    return _public(result, root)


def _invalid_artifact(artifact_id: str, kind: str) -> dict:
    return {
        "artifact_id": artifact_id,
        "failure_kind": kind,
        "first_error": kind,
        "excerpt": "",
    }


def _parse_prefixed_id(artifact_id: str, prefix: str) -> str | None:
    if not isinstance(artifact_id, str) or not artifact_id:
        return None
    if artifact_id.strip() != artifact_id:
        return None
    candidate = Path(artifact_id)
    if candidate.is_absolute():
        return None
    if ".." in artifact_id or "\\" in artifact_id:
        return None
    if not artifact_id.startswith(prefix):
        return None
    run_id = artifact_id[len(prefix) :]
    if not run_id or "/" in run_id:
        return None
    if run_id in {".", ".."} or run_id.startswith("."):
        return None
    if any(ch not in ALLOWED_RUN_ID for ch in run_id):
        return None
    return run_id


def parse_build_run_id(artifact_id: str) -> str | None:
    return _parse_prefixed_id(artifact_id, BUILD_ARTIFACT_PREFIX)


def parse_platform_artifact_id(artifact_id: str) -> str | None:
    return _parse_prefixed_id(artifact_id, PLATFORM_ARTIFACT_PREFIX)


def bound_excerpt(text: str) -> str:
    lines = text.splitlines()
    if len(lines) > MAX_EXCERPT_LINES:
        lines = lines[:MAX_EXCERPT_LINES]
    clipped = "\n".join(lines)
    if len(clipped) > MAX_EXCERPT_CHARS:
        clipped = clipped[:MAX_EXCERPT_CHARS]
    return clipped


def apptraverse_build_failure_excerpt(
    artifact_id: str, source_dir: str | None = None
) -> dict:
    """Return a bounded failure excerpt for an apptraverse-build artifact id."""
    run_id = parse_build_run_id(artifact_id)
    if run_id is None:
        return _invalid_artifact(artifact_id, "invalid_artifact_id")
    try:
        root = _checkout(source_dir)
    except SourceDirError as exc:
        payload = _invalid_artifact(artifact_id, "invalid_source_dir")
        payload["first_error"] = exc.message
        payload["source_dir"] = None
        return payload
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
        "source_dir": str(root),
    }


def apptraverse_platform_failure_excerpt(
    artifact_id: str, source_dir: str | None = None
) -> dict:
    """Return a bounded failure excerpt for an apptraverse-platform artifact id."""
    run_id = parse_platform_artifact_id(artifact_id)
    if run_id is None:
        return _invalid_artifact(artifact_id, "invalid_artifact_id")
    try:
        root = _checkout(source_dir)
    except SourceDirError as exc:
        payload = _invalid_artifact(artifact_id, "invalid_source_dir")
        payload["first_error"] = exc.message
        payload["source_dir"] = None
        return payload
    base = (root / ".artifacts" / "apptraverse-platform").resolve()
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
            nested = payload.get("platform_result")
            if isinstance(nested, dict):
                failure_kind = nested.get("failure_kind") or payload.get("failure_kind")
                first_error = nested.get("first_error") or payload.get("first_error")
            else:
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
        "source_dir": str(root),
    }


def _invalid_runtime_query(artifact_id: str, failure_kind: str) -> dict:
    return {
        "artifact_id": artifact_id,
        "records": [],
        "returned_count": 0,
        "has_more": False,
        "failure_kind": failure_kind,
    }


def apptraverse_runtime_log_query(
    artifact_id: str,
    event: str | None = None,
    after_seq: int | None = None,
    limit: int = DEFAULT_RUNTIME_QUERY_LIMIT,
    source_dir: str | None = None,
) -> dict:
    """Return bounded runtime JSONL records for an apptraverse-runtime artifact id."""
    if not isinstance(limit, int) or limit < 1 or limit > MAX_RUNTIME_QUERY_LIMIT:
        return _invalid_runtime_query(artifact_id, "invalid_limit")
    if parse_runtime_artifact_id(artifact_id) is None:
        return _invalid_runtime_query(artifact_id, "invalid_artifact_id")
    try:
        root = _checkout(source_dir)
    except SourceDirError as exc:
        payload = _invalid_runtime_query(artifact_id, "invalid_source_dir")
        payload["first_error"] = exc.message
        return payload
    log_path = resolve_runtime_log_path(root, artifact_id)
    if log_path is None:
        return _invalid_runtime_query(artifact_id, "invalid_artifact_id")
    if not log_path.is_file():
        return _invalid_runtime_query(artifact_id, "artifact_not_found")
    try:
        records, has_more = query_records(
            log_path,
            event=event,
            after_seq=after_seq,
            limit=limit,
        )
    except RuntimeJsonlError as exc:
        return {
            "artifact_id": artifact_id,
            "records": [],
            "returned_count": 0,
            "has_more": False,
            "failure_kind": "invalid_runtime_log",
            "first_error": str(exc),
        }
    return {
        "artifact_id": artifact_id,
        "records": records,
        "returned_count": len(records),
        "has_more": has_more,
        "failure_kind": None,
        "source_dir": str(root),
    }


def create_mcp_server():
    from mcp.server import MCPServer

    server = MCPServer("apptraverse")
    server.tool()(apptraverse_build_start)
    server.tool()(apptraverse_build_status)
    server.tool()(apptraverse_build_cancel)
    server.tool()(apptraverse_build_failure_excerpt)
    server.tool()(apptraverse_runtime_log_query)
    server.tool()(apptraverse_platform_start)
    server.tool()(apptraverse_platform_status)
    server.tool()(apptraverse_platform_cancel)
    server.tool()(apptraverse_platform_failure_excerpt)
    server.tool()(apptraverse_process_start)
    server.tool()(apptraverse_process_status)
    server.tool()(apptraverse_process_stop)
    server.tool()(apptraverse_chat_headless_test_start)
    server.tool()(apptraverse_chat_p2p_headless_test_start)
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
