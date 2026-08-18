#!/usr/bin/env python3
"""Background job controller wrapping the canonical build runner."""

from __future__ import annotations

import argparse
import json
import os
import secrets
import subprocess
import sys
import time
from dataclasses import asdict, dataclass, field
from datetime import datetime, timezone
from pathlib import Path

_REPO_ROOT = Path(__file__).resolve().parents[2]
if str(_REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(_REPO_ROOT))

from tools.runners import run_apptraverse_build as build_runner

JOB_SCHEMA_VERSION = "apptraverse.build_job/1"
JOB_ROOT_REL = Path(".artifacts") / "apptraverse-jobs"
BUILD_RUNNER_REL = Path("tools") / "runners" / "run_apptraverse_build.py"
COMPACT_BUILD_FIELDS = (
    "schema_version",
    "run_id",
    "artifact_id",
    "status",
    "stage",
    "profile",
    "targets",
    "action",
    "duration_ms",
    "exit_code",
    "failure_kind",
    "first_error",
)

STATE_STARTING = "starting"
STATE_RUNNING = "running"
STATE_COMPLETED = "completed"
STATE_CANCELLED = "cancelled"
STATE_FAILED = "failed"
STATE_NOT_FOUND = "not_found"


@dataclass
class JobResult:
    schema_version: str = JOB_SCHEMA_VERSION
    operation: str = ""
    job_id: str | None = None
    artifact_id: str | None = None
    state: str = STATE_FAILED
    pid: int | None = None
    profile: str = ""
    stage: str = ""
    targets: list[str] = field(default_factory=list)
    started_at_utc: str | None = None
    finished_at_utc: str | None = None
    duration_ms: int | None = None
    build_result: dict | None = None
    failure_kind: str | None = None
    first_error: str | None = None

    def to_public_dict(self) -> dict:
        return asdict(self)


def repo_root() -> Path:
    return Path(__file__).resolve().parents[2]


def utc_now() -> str:
    return datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def new_job_id() -> str:
    stamp = datetime.now(timezone.utc).strftime("%Y%m%d-%H%M%S")
    return f"{stamp}-{secrets.token_hex(3)}"


def jobs_root(source_dir: Path) -> Path:
    return source_dir / JOB_ROOT_REL


def job_dir_for(source_dir: Path, job_id: str) -> Path:
    return jobs_root(source_dir) / job_id


def artifact_id_for(job_id: str) -> str:
    return f"apptraverse-jobs/{job_id}"


def atomic_write_json(path: Path, payload: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_name(path.name + ".tmp")
    tmp.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
    tmp.replace(path)


def read_json(path: Path) -> dict | None:
    if not path.is_file():
        return None
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError:
        return None


def compact_build_result(payload: dict | None) -> dict | None:
    if not isinstance(payload, dict):
        return None
    compact = {key: payload.get(key) for key in COMPACT_BUILD_FIELDS}
    if compact.get("targets") is None:
        compact["targets"] = []
    return compact


def canonical_runner_argv(
    source_dir: Path,
    profile: str,
    stage: str,
    targets: list[str],
) -> list[str]:
    runner = source_dir / BUILD_RUNNER_REL
    argv = [
        sys.executable,
        str(runner),
        "--profile",
        profile,
        "--stage",
        stage,
        "--json",
    ]
    for target in targets:
        argv.extend(["--target", target])
    return argv


def pid_is_alive(pid: int | None) -> bool:
    if pid is None or pid <= 0:
        return False
    if sys.platform.startswith("win"):
        # os.kill(pid, 0) raises WinError 87 on other processes; it is not a liveness probe.
        import ctypes
        from ctypes import wintypes

        kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
        PROCESS_QUERY_LIMITED_INFORMATION = 0x1000
        STILL_ACTIVE = 259
        ERROR_ACCESS_DENIED = 5
        handle = kernel32.OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, False, int(pid))
        if not handle:
            return ctypes.get_last_error() == ERROR_ACCESS_DENIED
        try:
            exit_code = wintypes.DWORD()
            if not kernel32.GetExitCodeProcess(handle, ctypes.byref(exit_code)):
                return True
            return exit_code.value == STILL_ACTIVE
        finally:
            kernel32.CloseHandle(handle)
    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        return False
    except PermissionError:
        return True
    except OSError:
        return False
    return True


def terminate_process_tree(pid: int) -> None:
    if sys.platform.startswith("win"):
        subprocess.run(
            ["taskkill.exe", "/PID", str(pid), "/T", "/F"],
            capture_output=True,
            shell=False,
        )
        return
    try:
        os.kill(pid, 15)
    except OSError:
        pass


def worker_launch_argv(worker_argv: list[str]) -> list[str]:
    """Launch argv that returns as soon as the background worker exists."""
    if not sys.platform.startswith("win"):
        return worker_argv
    # `start` is a cmd builtin. CreateProcess of the worker with redirected
    # handles cannot detach, so `start` returns immediately while Python
    # continues after the parent `start` command exits.
    return ["cmd.exe", "/c", "start", "", "/b"] + worker_argv


def wait_for_worker_pid(job_dir: Path, timeout_s: float = 2.0) -> int | None:
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        meta = read_json(job_dir / "job.json") or {}
        pid = meta.get("pid")
        if isinstance(pid, int) and pid > 0:
            return pid
        time.sleep(0.05)
    return None


def write_worker_pid(source_dir: Path, job_id: str) -> None:
    job_dir = job_dir_for(source_dir, job_id)
    meta = read_json(job_dir / "job.json") or {}
    meta["pid"] = os.getpid()
    meta["state"] = STATE_RUNNING
    atomic_write_json(job_dir / "job.json", meta)


def emit_job_result(result: JobResult, json_mode: bool) -> None:
    if json_mode:
        sys.stdout.write(json.dumps(result.to_public_dict(), separators=(",", ":")) + "\n")
        sys.stdout.flush()
        return
    parts = [
        f"state={result.state}",
        f"operation={result.operation}",
        f"job_id={result.job_id or ''}",
        f"artifact_id={result.artifact_id or ''}",
    ]
    if result.failure_kind:
        parts.append(f"failure_kind={result.failure_kind}")
    if result.build_result and result.build_result.get("status"):
        parts.append("build_status=" + str(result.build_result.get("status")))
    print(" ".join(parts))


def exit_code_for(result: JobResult) -> int:
    if result.state in {STATE_RUNNING, STATE_STARTING, STATE_COMPLETED, STATE_CANCELLED}:
        return 0
    if result.state == STATE_NOT_FOUND:
        return 2
    return 1


def start_job(
    source_dir: Path,
    profile: str,
    stage: str,
    targets: list[str],
    *,
    popen=subprocess.Popen,
) -> JobResult:
    if profile not in build_runner.PROFILES:
        return JobResult(
            operation="start",
            state=STATE_FAILED,
            profile=profile,
            stage=stage,
            targets=targets,
            failure_kind="unsupported_profile",
            first_error=profile,
        )
    job_id = new_job_id()
    started = utc_now()
    job_dir = job_dir_for(source_dir, job_id)
    job_dir.mkdir(parents=True, exist_ok=True)
    argv = canonical_runner_argv(source_dir, profile, stage, targets)
    request = {
        "job_id": job_id,
        "profile": profile,
        "stage": stage,
        "targets": targets,
        "runner_argv": argv,
        "started_at_utc": started,
    }
    atomic_write_json(job_dir / "request.json", request)
    job_meta = {
        "schema_version": JOB_SCHEMA_VERSION,
        "job_id": job_id,
        "state": STATE_STARTING,
        "pid": None,
        "profile": profile,
        "stage": stage,
        "targets": targets,
        "started_at_utc": started,
    }
    atomic_write_json(job_dir / "job.json", job_meta)
    worker_argv = worker_launch_argv(
        [
            sys.executable,
            str(source_dir / "tools" / "runners" / "run_apptraverse_job.py"),
            "_worker",
            "--job-id",
            job_id,
        ]
    )
    stdout_log = job_dir / "worker_stdout.log"
    stderr_log = job_dir / "worker_stderr.log"
    stdout_log.touch()
    stderr_log.touch()
    popen_kwargs = {
        "cwd": source_dir,
        "shell": False,
    }
    if sys.platform.startswith("win"):
        popen_kwargs["creationflags"] = getattr(subprocess, "CREATE_NO_WINDOW", 0)
    else:
        popen_kwargs.update(
            start_new_session=True,
            stdin=subprocess.DEVNULL,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
    proc = popen(worker_argv, **popen_kwargs)
    pid = proc.pid
    if popen is subprocess.Popen and sys.platform.startswith("win"):
        pid = wait_for_worker_pid(job_dir) or pid
    job_meta["state"] = STATE_RUNNING
    job_meta["pid"] = pid
    atomic_write_json(job_dir / "job.json", job_meta)
    return JobResult(
        operation="start",
        job_id=job_id,
        artifact_id=artifact_id_for(job_id),
        state=STATE_RUNNING,
        pid=pid,
        profile=profile,
        stage=stage,
        targets=targets,
        started_at_utc=started,
    )


def load_job_meta(source_dir: Path, job_id: str) -> dict | None:
    return read_json(job_dir_for(source_dir, job_id) / "job.json")


def status_job(source_dir: Path, job_id: str) -> JobResult:
    job_dir = job_dir_for(source_dir, job_id)
    if not job_dir.is_dir() and load_job_meta(source_dir, job_id) is None:
        return JobResult(
            operation="status",
            job_id=job_id,
            state=STATE_NOT_FOUND,
            failure_kind="job_not_found",
        )
    final_payload = read_json(job_dir / "final.json")
    if final_payload is not None:
        build_result = compact_build_result(final_payload.get("build_result"))
        return JobResult(
            operation="status",
            job_id=job_id,
            artifact_id=artifact_id_for(job_id),
            state=STATE_COMPLETED,
            pid=final_payload.get("pid"),
            profile=final_payload.get("profile") or "",
            stage=final_payload.get("stage") or "",
            targets=final_payload.get("targets") or [],
            started_at_utc=final_payload.get("started_at_utc"),
            finished_at_utc=final_payload.get("finished_at_utc"),
            duration_ms=final_payload.get("duration_ms"),
            build_result=build_result,
            failure_kind=None,
            first_error=(build_result or {}).get("first_error") if build_result else None,
        )
    if (job_dir / "cancelled.json").is_file():
        cancelled = read_json(job_dir / "cancelled.json") or {}
        meta = load_job_meta(source_dir, job_id) or {}
        return JobResult(
            operation="status",
            job_id=job_id,
            artifact_id=artifact_id_for(job_id),
            state=STATE_CANCELLED,
            pid=cancelled.get("pid") or meta.get("pid"),
            profile=meta.get("profile") or "",
            stage=meta.get("stage") or "",
            targets=meta.get("targets") or [],
            started_at_utc=meta.get("started_at_utc"),
            finished_at_utc=cancelled.get("finished_at_utc"),
            failure_kind="cancelled",
        )
    meta = load_job_meta(source_dir, job_id) or {}
    pid = meta.get("pid")
    if pid_is_alive(pid):
        return JobResult(
            operation="status",
            job_id=job_id,
            artifact_id=artifact_id_for(job_id),
            state=STATE_RUNNING,
            pid=pid,
            profile=meta.get("profile") or "",
            stage=meta.get("stage") or "",
            targets=meta.get("targets") or [],
            started_at_utc=meta.get("started_at_utc"),
        )
    return JobResult(
        operation="status",
        job_id=job_id,
        artifact_id=artifact_id_for(job_id),
        state=STATE_FAILED,
        pid=pid,
        profile=meta.get("profile") or "",
        stage=meta.get("stage") or "",
        targets=meta.get("targets") or [],
        started_at_utc=meta.get("started_at_utc"),
        failure_kind="worker_terminated_without_result",
        first_error="worker_terminated_without_result",
    )


def cancel_job(
    source_dir: Path,
    job_id: str,
    *,
    terminate=terminate_process_tree,
) -> JobResult:
    current = status_job(source_dir, job_id)
    if current.state == STATE_NOT_FOUND:
        current.operation = "cancel"
        return current
    if current.state == STATE_COMPLETED:
        current.operation = "cancel"
        return current
    if current.state == STATE_CANCELLED:
        current.operation = "cancel"
        return current
    pid = current.pid
    if pid:
        terminate(pid)
    finished = utc_now()
    atomic_write_json(
        job_dir_for(source_dir, job_id) / "cancelled.json",
        {"job_id": job_id, "pid": pid, "finished_at_utc": finished},
    )
    meta = load_job_meta(source_dir, job_id) or {}
    meta["state"] = STATE_CANCELLED
    meta["finished_at_utc"] = finished
    atomic_write_json(job_dir_for(source_dir, job_id) / "job.json", meta)
    return JobResult(
        operation="cancel",
        job_id=job_id,
        artifact_id=artifact_id_for(job_id),
        state=STATE_CANCELLED,
        pid=pid,
        profile=current.profile,
        stage=current.stage,
        targets=current.targets,
        started_at_utc=current.started_at_utc,
        finished_at_utc=finished,
        failure_kind="cancelled",
    )


def run_worker(source_dir: Path, job_id: str) -> int:
    write_worker_pid(source_dir, job_id)
    job_dir = job_dir_for(source_dir, job_id)
    request = read_json(job_dir / "request.json") or {}
    argv = request.get("runner_argv") or canonical_runner_argv(
        source_dir,
        request.get("profile") or "",
        request.get("stage") or "",
        request.get("targets") or [],
    )
    started = time.perf_counter()
    started_at = request.get("started_at_utc") or utc_now()
    stdout_path = job_dir / "worker_stdout.log"
    stderr_path = job_dir / "worker_stderr.log"
    with stdout_path.open("w", encoding="utf-8", errors="replace") as out:
        with stderr_path.open("w", encoding="utf-8", errors="replace") as err:
            completed = subprocess.run(
                argv,
                cwd=source_dir,
                stdout=out,
                stderr=err,
                shell=False,
            )
    duration_ms = int((time.perf_counter() - started) * 1000)
    finished_at = utc_now()
    raw = stdout_path.read_text(encoding="utf-8", errors="replace").strip()
    build_result = None
    job_state = STATE_COMPLETED
    failure_kind = None
    first_error = None
    try:
        parsed = json.loads(raw.splitlines()[-1] if raw else "")
        build_result = compact_build_result(parsed)
        first_error = (build_result or {}).get("first_error")
    except (json.JSONDecodeError, IndexError, ValueError):
        job_state = STATE_FAILED
        failure_kind = "worker_terminated_without_result"
        first_error = "worker_terminated_without_result"
        if completed.returncode != 0 and not raw:
            failure_kind = "worker_terminated_without_result"
    payload = {
        "schema_version": JOB_SCHEMA_VERSION,
        "job_id": job_id,
        "state": job_state,
        "pid": os.getpid(),
        "profile": request.get("profile"),
        "stage": request.get("stage"),
        "targets": request.get("targets") or [],
        "started_at_utc": started_at,
        "finished_at_utc": finished_at,
        "duration_ms": duration_ms,
        "build_result": build_result,
        "failure_kind": failure_kind,
        "exit_code": completed.returncode,
    }
    atomic_write_json(job_dir / "final.json", payload)
    meta = load_job_meta(source_dir, job_id) or {}
    meta["state"] = job_state
    meta["finished_at_utc"] = finished_at
    atomic_write_json(job_dir / "job.json", meta)
    return 0 if job_state == STATE_COMPLETED else 1


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Background App Traverse build jobs.")
    sub = parser.add_subparsers(dest="command", required=True)
    start = sub.add_parser("start")
    start.add_argument("--profile", required=True)
    start.add_argument("--stage", required=True, choices=("preflight", "configure", "build"))
    start.add_argument("--target", action="append", default=[])
    start.add_argument("--json", action="store_true")
    status = sub.add_parser("status")
    status.add_argument("--job-id", required=True)
    status.add_argument("--json", action="store_true")
    cancel = sub.add_parser("cancel")
    cancel.add_argument("--job-id", required=True)
    cancel.add_argument("--json", action="store_true")
    worker = sub.add_parser("_worker")
    worker.add_argument("--job-id", required=True)
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    source_dir = repo_root()
    os.chdir(source_dir)
    if args.command == "_worker":
        return run_worker(source_dir, args.job_id)
    if args.command == "start":
        result = start_job(source_dir, args.profile, args.stage, args.target)
        emit_job_result(result, json_mode=args.json)
        return exit_code_for(result)
    if args.command == "status":
        result = status_job(source_dir, args.job_id)
        emit_job_result(result, json_mode=args.json)
        return exit_code_for(result)
    result = cancel_job(source_dir, args.job_id)
    emit_job_result(result, json_mode=args.json)
    return exit_code_for(result)


if __name__ == "__main__":
    sys.exit(main())
