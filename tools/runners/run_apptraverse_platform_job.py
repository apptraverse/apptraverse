#!/usr/bin/env python3
"""Background POSIX platform jobs and known-profile process control."""

from __future__ import annotations

import argparse
import json
import os
import secrets
import signal
import subprocess
import sys
import time
from dataclasses import asdict, dataclass, field
from datetime import datetime, timezone
from pathlib import Path

_REPO_ROOT = Path(__file__).resolve().parents[2]
if str(_REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(_REPO_ROOT))

from tools.runners import run_apptraverse_platform as platform_runner

JOB_SCHEMA_VERSION = "apptraverse.platform_job/1"
PROCESS_SCHEMA_VERSION = "apptraverse.process_result/1"
JOB_ROOT_REL = Path(".artifacts") / "apptraverse-platform"
PLATFORM_RUNNER_REL = Path("tools") / "runners" / "run_apptraverse_platform.py"
COMPACT_PLATFORM_FIELDS = (
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
STATE_STOPPED = "stopped"


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
    platform_result: dict | None = None
    failure_kind: str | None = None
    first_error: str | None = None

    def to_public_dict(self) -> dict:
        return asdict(self)


@dataclass
class ProcessResult:
    schema_version: str = PROCESS_SCHEMA_VERSION
    operation: str = ""
    process_id: str | None = None
    artifact_id: str | None = None
    state: str = STATE_FAILED
    pid: int | None = None
    profile: str = ""
    duration_ms: int | None = None
    exit_code: int | None = None
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
    return f"j-{stamp}-{secrets.token_hex(3)}"


def new_process_id() -> str:
    stamp = datetime.now(timezone.utc).strftime("%Y%m%d-%H%M%S")
    return f"p-{stamp}-{secrets.token_hex(3)}"


def jobs_root(source_dir: Path) -> Path:
    return source_dir / JOB_ROOT_REL


def job_dir_for(source_dir: Path, job_id: str) -> Path:
    return jobs_root(source_dir) / job_id


def process_dir_for(source_dir: Path, process_id: str) -> Path:
    return jobs_root(source_dir) / process_id


def artifact_id_for(item_id: str) -> str:
    return f"apptraverse-platform/{item_id}"


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


def compact_platform_result(payload: dict | None) -> dict | None:
    if not isinstance(payload, dict):
        return None
    compact = {key: payload.get(key) for key in COMPACT_PLATFORM_FIELDS}
    if compact.get("targets") is None:
        compact["targets"] = []
    return compact


def canonical_runner_argv(
    source_dir: Path,
    profile: str,
    stage: str,
    targets: list[str],
) -> list[str]:
    runner = source_dir / PLATFORM_RUNNER_REL
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


def inspect_pid(pid: int | None) -> tuple[bool, int | None]:
    if pid is None or pid <= 0:
        return False, None
    try:
        waited, status = os.waitpid(pid, os.WNOHANG)
        if waited == pid:
            if os.WIFEXITED(status):
                return False, os.WEXITSTATUS(status)
            if os.WIFSIGNALED(status):
                return False, -os.WTERMSIG(status)
            return False, 1
    except (ChildProcessError, OSError):
        pass
    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        return False, None
    except PermissionError:
        return True, None
    except OSError:
        return False, None
    return True, None


def pid_is_alive(pid: int | None) -> bool:
    alive, _exit_code = inspect_pid(pid)
    return alive


def reap_exit_code(pid: int | None) -> int | None:
    _alive, exit_code = inspect_pid(pid)
    return exit_code


def terminate_process_tree(pid: int) -> None:
    try:
        os.killpg(pid, signal.SIGTERM)
    except OSError:
        try:
            os.kill(pid, signal.SIGTERM)
        except OSError:
            pass


def force_kill_process_tree(pid: int) -> None:
    try:
        os.killpg(pid, signal.SIGKILL)
    except OSError:
        try:
            os.kill(pid, signal.SIGKILL)
        except OSError:
            pass


def worker_launch_argv(worker_argv: list[str]) -> list[str]:
    return worker_argv


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
    print(" ".join(parts))


def emit_process_result(result: ProcessResult, json_mode: bool) -> None:
    if json_mode:
        sys.stdout.write(json.dumps(result.to_public_dict(), separators=(",", ":")) + "\n")
        sys.stdout.flush()
        return
    parts = [
        f"state={result.state}",
        f"operation={result.operation}",
        f"process_id={result.process_id or ''}",
        f"artifact_id={result.artifact_id or ''}",
    ]
    if result.failure_kind:
        parts.append(f"failure_kind={result.failure_kind}")
    print(" ".join(parts))


def exit_code_for_job(result: JobResult) -> int:
    if result.state in {STATE_RUNNING, STATE_STARTING, STATE_COMPLETED, STATE_CANCELLED}:
        return 0
    if result.state == STATE_NOT_FOUND:
        return 2
    return 1


def exit_code_for_process(result: ProcessResult) -> int:
    if result.state in {STATE_RUNNING, STATE_STARTING, STATE_STOPPED}:
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
    if profile not in platform_runner.PROFILES:
        return JobResult(
            operation="start",
            state=STATE_FAILED,
            profile=profile,
            stage=stage,
            targets=targets,
            failure_kind="unsupported_profile",
            first_error=profile,
        )
    if stage == "build":
        targets = platform_runner.default_targets(profile, targets)
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
            str(source_dir / "tools" / "runners" / "run_apptraverse_platform_job.py"),
            "_worker",
            "--job-id",
            job_id,
        ]
    )
    stdout_log = job_dir / "worker_stdout.log"
    stderr_log = job_dir / "worker_stderr.log"
    stdout_log.touch()
    stderr_log.touch()
    proc = popen(
        worker_argv,
        cwd=source_dir,
        shell=False,
        start_new_session=True,
        stdin=subprocess.DEVNULL,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    pid = proc.pid
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
        platform_result = compact_platform_result(final_payload.get("platform_result"))
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
            platform_result=platform_result,
            failure_kind=None,
            first_error=(platform_result or {}).get("first_error") if platform_result else None,
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
    platform_result = None
    job_state = STATE_COMPLETED
    failure_kind = None
    first_error = None
    try:
        parsed = json.loads(raw.splitlines()[-1] if raw else "")
        platform_result = compact_platform_result(parsed)
        first_error = (platform_result or {}).get("first_error")
        nested_id = (platform_result or {}).get("artifact_id")
        if nested_id and (platform_result or {}).get("status") != platform_runner.STATUS_OK:
            nested_dir = source_dir / JOB_ROOT_REL / str(nested_id).split("/", 1)[-1]
            excerpt_src = nested_dir / "failure_excerpt.txt"
            if excerpt_src.is_file():
                excerpt = platform_runner.bound_excerpt_text(
                    excerpt_src.read_text(encoding="utf-8", errors="replace")
                )
                (job_dir / "failure_excerpt.txt").write_text(excerpt + "\n", encoding="utf-8")
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
        "platform_result": platform_result,
        "failure_kind": failure_kind,
        "exit_code": completed.returncode,
    }
    atomic_write_json(job_dir / "final.json", payload)
    if first_error and job_state != STATE_COMPLETED:
        (job_dir / "result.json").write_text(
            json.dumps(
                {
                    "failure_kind": failure_kind,
                    "first_error": first_error,
                },
                indent=2,
            )
            + "\n",
            encoding="utf-8",
        )
    elif platform_result:
        (job_dir / "result.json").write_text(
            json.dumps(platform_result, indent=2) + "\n",
            encoding="utf-8",
        )
    meta = load_job_meta(source_dir, job_id) or {}
    meta["state"] = job_state
    meta["finished_at_utc"] = finished_at
    atomic_write_json(job_dir / "job.json", meta)
    return 0 if job_state == STATE_COMPLETED else 1


def _failed_process(
    operation: str,
    *,
    profile: str = "",
    process_id: str | None = None,
    failure_kind: str,
    first_error: str | None = None,
) -> ProcessResult:
    return ProcessResult(
        operation=operation,
        process_id=process_id,
        artifact_id=artifact_id_for(process_id) if process_id else None,
        state=STATE_FAILED,
        profile=profile,
        failure_kind=failure_kind,
        first_error=first_error or failure_kind,
    )


def _write_process_excerpt(proc_dir: Path, text: str) -> str:
    excerpt = platform_runner.bound_excerpt_text(text)
    if excerpt.strip():
        (proc_dir / "failure_excerpt.txt").write_text(excerpt + "\n", encoding="utf-8")
    return excerpt


def _log_excerpt(proc_dir: Path) -> str:
    chunks: list[str] = []
    for name in ("stderr.log", "stdout.log"):
        path = proc_dir / name
        if path.is_file():
            chunks.append(path.read_text(encoding="utf-8", errors="replace"))
    combined = "\n".join(chunks)
    _first, excerpt_lines = platform_runner.extract_first_error(combined)
    excerpt = platform_runner.bound_excerpt_text("\n".join(excerpt_lines))
    return excerpt


def start_process(
    source_dir: Path,
    profile: str,
    state_dir: str,
    *,
    popen=subprocess.Popen,
) -> ProcessResult:
    if profile not in platform_runner.PROFILES:
        return _failed_process("start", profile=profile, failure_kind="unsupported_profile", first_error=profile)
    if not platform_runner.host_matches(profile, sys.platform):
        return _failed_process(
            "start",
            profile=profile,
            failure_kind="wrong_host_os",
            first_error=sys.platform,
        )
    if not isinstance(state_dir, str) or not state_dir.strip():
        return _failed_process("start", profile=profile, failure_kind="missing_state_dir")
    exe = platform_runner.exe_path_for(source_dir, profile)
    if not exe.is_file():
        return _failed_process(
            "start",
            profile=profile,
            failure_kind="exe_missing",
            first_error=exe.as_posix(),
        )
    process_id = new_process_id()
    proc_dir = process_dir_for(source_dir, process_id)
    proc_dir.mkdir(parents=True, exist_ok=True)
    state_path = Path(state_dir)
    if not state_path.is_absolute():
        state_path = (source_dir / state_path).resolve()
    argv = platform_runner.process_argv(source_dir, profile, str(state_path))
    started = utc_now()
    started_mono = time.perf_counter()
    request = {
        "process_id": process_id,
        "profile": profile,
        "argv": argv,
        "started_at_utc": started,
    }
    atomic_write_json(proc_dir / "request.json", request)
    stdout_path = proc_dir / "stdout.log"
    stderr_path = proc_dir / "stderr.log"
    out_file = stdout_path.open("w", encoding="utf-8", errors="replace")
    err_file = stderr_path.open("w", encoding="utf-8", errors="replace")
    try:
        proc = popen(
            argv,
            cwd=source_dir,
            stdin=subprocess.DEVNULL,
            stdout=out_file,
            stderr=err_file,
            start_new_session=True,
            shell=False,
        )
    except OSError as exc:
        out_file.close()
        err_file.close()
        return _failed_process(
            "start",
            profile=profile,
            process_id=process_id,
            failure_kind="process_start_failed",
            first_error=str(exc),
        )
    finally:
        out_file.close()
        err_file.close()
    meta = {
        "schema_version": PROCESS_SCHEMA_VERSION,
        "process_id": process_id,
        "state": STATE_RUNNING,
        "pid": proc.pid,
        "profile": profile,
        "started_at_utc": started,
        "started_mono": started_mono,
    }
    atomic_write_json(proc_dir / "process.json", meta)
    return ProcessResult(
        operation="start",
        process_id=process_id,
        artifact_id=artifact_id_for(process_id),
        state=STATE_RUNNING,
        pid=proc.pid,
        profile=profile,
    )


def _duration_ms_from_meta(meta: dict) -> int | None:
    started_mono = meta.get("started_mono")
    if isinstance(started_mono, (int, float)):
        return int((time.perf_counter() - started_mono) * 1000)
    return None


def status_process(source_dir: Path, process_id: str) -> ProcessResult:
    proc_dir = process_dir_for(source_dir, process_id)
    meta = read_json(proc_dir / "process.json")
    if meta is None:
        return ProcessResult(
            operation="status",
            process_id=process_id,
            state=STATE_NOT_FOUND,
            failure_kind="process_not_found",
            first_error="process_not_found",
        )
    pid = meta.get("pid")
    profile = meta.get("profile") or ""
    alive, reaped_exit = inspect_pid(pid)
    if alive:
        return ProcessResult(
            operation="status",
            process_id=process_id,
            artifact_id=artifact_id_for(process_id),
            state=STATE_RUNNING,
            pid=pid,
            profile=profile,
            duration_ms=_duration_ms_from_meta(meta),
        )
    stopped = read_json(proc_dir / "stopped.json") or {}
    exit_code = meta.get("exit_code")
    if exit_code is None:
        exit_code = reaped_exit
    if exit_code is None:
        exit_code = stopped.get("exit_code")
    excerpt = _log_excerpt(proc_dir)
    if excerpt:
        _write_process_excerpt(proc_dir, excerpt)
    if stopped:
        state = STATE_STOPPED
        failure_kind = None
        first_error = None
    else:
        state = STATE_FAILED
        failure_kind = "process_exited"
        first_error = platform_runner.bound_first_error(
            excerpt.splitlines()[0] if excerpt else "process_exited"
        )
    result_payload = {
        "schema_version": PROCESS_SCHEMA_VERSION,
        "process_id": process_id,
        "state": state,
        "pid": pid,
        "profile": profile,
        "exit_code": exit_code,
        "failure_kind": failure_kind,
        "first_error": first_error,
    }
    atomic_write_json(proc_dir / "result.json", result_payload)
    meta["state"] = state
    meta["exit_code"] = exit_code
    atomic_write_json(proc_dir / "process.json", meta)
    return ProcessResult(
        operation="status",
        process_id=process_id,
        artifact_id=artifact_id_for(process_id),
        state=state,
        pid=pid,
        profile=profile,
        duration_ms=_duration_ms_from_meta(meta),
        exit_code=exit_code,
        failure_kind=failure_kind,
        first_error=first_error,
    )


def stop_process(
    source_dir: Path,
    process_id: str,
    *,
    terminate=terminate_process_tree,
    kill=force_kill_process_tree,
) -> ProcessResult:
    current = status_process(source_dir, process_id)
    if current.state == STATE_NOT_FOUND:
        current.operation = "stop"
        return current
    if current.state != STATE_RUNNING:
        current.operation = "stop"
        return current
    pid = current.pid
    if pid:
        terminate(pid)
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline and pid_is_alive(pid):
            time.sleep(0.05)
        if pid_is_alive(pid):
            kill(pid)
            deadline = time.monotonic() + 2
            while time.monotonic() < deadline and pid_is_alive(pid):
                time.sleep(0.05)
    exit_code = reap_exit_code(pid)
    finished = utc_now()
    proc_dir = process_dir_for(source_dir, process_id)
    atomic_write_json(
        proc_dir / "stopped.json",
        {
            "process_id": process_id,
            "pid": pid,
            "finished_at_utc": finished,
            "exit_code": exit_code,
        },
    )
    meta = read_json(proc_dir / "process.json") or {}
    meta["state"] = STATE_STOPPED
    meta["exit_code"] = exit_code
    meta["finished_at_utc"] = finished
    atomic_write_json(proc_dir / "process.json", meta)
    return ProcessResult(
        operation="stop",
        process_id=process_id,
        artifact_id=artifact_id_for(process_id),
        state=STATE_STOPPED,
        pid=pid,
        profile=current.profile,
        duration_ms=_duration_ms_from_meta(meta),
        exit_code=exit_code,
    )


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Background App Traverse POSIX platform jobs.")
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
    proc_start = sub.add_parser("process-start")
    proc_start.add_argument("--profile", required=True)
    proc_start.add_argument("--state-dir", required=True)
    proc_start.add_argument("--json", action="store_true")
    proc_status = sub.add_parser("process-status")
    proc_status.add_argument("--process-id", required=True)
    proc_status.add_argument("--json", action="store_true")
    proc_stop = sub.add_parser("process-stop")
    proc_stop.add_argument("--process-id", required=True)
    proc_stop.add_argument("--json", action="store_true")
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
        return exit_code_for_job(result)
    if args.command == "status":
        result = status_job(source_dir, args.job_id)
        emit_job_result(result, json_mode=args.json)
        return exit_code_for_job(result)
    if args.command == "cancel":
        result = cancel_job(source_dir, args.job_id)
        emit_job_result(result, json_mode=args.json)
        return exit_code_for_job(result)
    if args.command == "process-start":
        result = start_process(source_dir, args.profile, args.state_dir)
        emit_process_result(result, json_mode=args.json)
        return exit_code_for_process(result)
    if args.command == "process-status":
        result = status_process(source_dir, args.process_id)
        emit_process_result(result, json_mode=args.json)
        return exit_code_for_process(result)
    result = stop_process(source_dir, args.process_id)
    emit_process_result(result, json_mode=args.json)
    return exit_code_for_process(result)


if __name__ == "__main__":
    sys.exit(main())
