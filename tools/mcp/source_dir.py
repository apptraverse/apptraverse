#!/usr/bin/env python3
"""Resolve and validate an App Traverse checkout for MCP job/process tools.

MCP never uses cwd. The default root is the checkout that launched the server.
An explicit source_dir selects another git worktree of the same repository.
"""

from __future__ import annotations

import json
import re
from pathlib import Path

INDEX_REL = Path(".artifacts") / "mcp-source-index"
FAILURE_KIND = "invalid_source_dir"

_REQUIRED_RUNNER = Path("tools") / "runners" / "run_apptraverse_build.py"
_PROJECT_RE = re.compile(r'project\s*\(\s*"?App' + r'\s*' + r'Traverse"?')


class SourceDirError(Exception):
    def __init__(self, message: str) -> None:
        super().__init__(message)
        self.message = message


def canonical_path(path: Path) -> Path:
    return path.expanduser().resolve()


def git_common_dir(root: Path) -> Path | None:
    git = root / ".git"
    if git.is_dir():
        return git.resolve()
    if not git.is_file():
        return None
    try:
        text = git.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return None
    for line in text.splitlines():
        stripped = line.strip()
        if not stripped.lower().startswith("gitdir:"):
            continue
        raw = stripped.split(":", 1)[1].strip()
        gitdir = Path(raw)
        if not gitdir.is_absolute():
            gitdir = (root / gitdir)
        gitdir = gitdir.resolve()
        if gitdir.parent.name == "worktrees":
            return gitdir.parent.parent
        return gitdir
    return None


def looks_like_apptraverse(root: Path) -> str | None:
    cmake = root / "CMakeLists.txt"
    tools = root / "tools"
    runner = root / _REQUIRED_RUNNER
    if not cmake.is_file():
        return "missing CMakeLists.txt"
    if not tools.is_dir():
        return "missing tools/"
    if not runner.is_file():
        return "missing tools/runners/run_apptraverse_build.py"
    try:
        cmake_text = cmake.read_text(encoding="utf-8", errors="replace")
    except OSError as exc:
        return f"cannot read CMakeLists.txt: {exc}"
    if "cmake_minimum_required" not in cmake_text:
        return "CMakeLists.txt is not an App Traverse project"
    if _PROJECT_RE.search(cmake_text) is None:
        return "CMakeLists.txt is not an App Traverse project"
    return None


def validate_source_dir(source_dir: str, *, peer_root: Path) -> Path:
    if not isinstance(source_dir, str) or not source_dir.strip():
        raise SourceDirError("source_dir must be a non-empty string")
    if source_dir.strip() != source_dir:
        raise SourceDirError("source_dir must not have surrounding whitespace")
    raw = Path(source_dir)
    if not raw.is_absolute():
        raise SourceDirError("source_dir must be an absolute path")
    try:
        root = canonical_path(raw)
    except OSError as exc:
        raise SourceDirError(f"source_dir cannot be resolved: {exc}") from exc
    if not root.exists():
        raise SourceDirError("source_dir does not exist")
    if not root.is_dir():
        raise SourceDirError("source_dir is not a directory")
    reason = looks_like_apptraverse(root)
    if reason is not None:
        raise SourceDirError(reason)
    peer = canonical_path(peer_root)
    peer_git = git_common_dir(peer)
    candidate_git = git_common_dir(root)
    if peer_git is not None and candidate_git is not None and peer_git != candidate_git:
        raise SourceDirError("source_dir is not a git worktree of this repository")
    return root


def resolve_source_dir(source_dir: str | None, *, default_root: Path) -> Path:
    if source_dir is None:
        return canonical_path(default_root)
    return validate_source_dir(source_dir, peer_root=default_root)


def error_payload(operation: str, message: str, *, schema_version: str) -> dict:
    return {
        "schema_version": schema_version,
        "operation": operation,
        "job_id": None,
        "artifact_id": None,
        "state": "failed",
        "pid": None,
        "profile": "",
        "stage": "",
        "targets": [],
        "started_at_utc": None,
        "finished_at_utc": None,
        "duration_ms": None,
        "build_result": None,
        "platform_result": None,
        "failure_kind": FAILURE_KIND,
        "first_error": message,
        "source_dir": None,
    }


def _safe_index_id(job_id: str) -> bool:
    if not isinstance(job_id, str) or not job_id or job_id.strip() != job_id:
        return False
    if job_id in {".", ".."} or "/" in job_id or "\\" in job_id:
        return False
    return True


def index_path(mcp_root: Path, kind: str, job_id: str) -> Path:
    return canonical_path(mcp_root) / INDEX_REL / kind / f"{job_id}.json"


def remember_source(
    mcp_root: Path, kind: str, job_id: str, source_dir: Path
) -> None:
    if not _safe_index_id(job_id):
        return
    path = index_path(mcp_root, kind, job_id)
    path.parent.mkdir(parents=True, exist_ok=True)
    payload = {
        "job_id": job_id,
        "kind": kind,
        "source_dir": str(canonical_path(source_dir)),
    }
    tmp = path.with_name(path.name + ".tmp")
    tmp.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
    tmp.replace(path)


def lookup_source(mcp_root: Path, kind: str, job_id: str) -> Path | None:
    if not _safe_index_id(job_id):
        return None
    path = index_path(mcp_root, kind, job_id)
    if not path.is_file():
        return None
    try:
        payload = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return None
    if not isinstance(payload, dict):
        return None
    raw = payload.get("source_dir")
    if not isinstance(raw, str):
        return None
    try:
        return validate_source_dir(raw, peer_root=mcp_root)
    except SourceDirError:
        return None


def resolve_existing_job_source(
    source_dir: str | None,
    job_id: str,
    kind: str,
    *,
    default_root: Path,
    job_exists,
) -> Path:
    """Resolve which checkout owns a job.

    Explicit source_dir always wins. Otherwise prefer a job directory in the
    MCP server checkout, then the remembered worktree from start.
    """
    default = canonical_path(default_root)
    if source_dir is not None:
        return validate_source_dir(source_dir, peer_root=default)
    if job_exists(default, job_id):
        return default
    indexed = lookup_source(default, kind, job_id)
    if indexed is not None:
        return indexed
    return default
