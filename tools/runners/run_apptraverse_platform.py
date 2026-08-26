#!/usr/bin/env python3
"""POSIX staged platform runner. Linux and macOS debug profiles."""

from __future__ import annotations

import argparse
import json
import os
import re
import secrets
import shutil
import signal
import subprocess
import sys
import time
from dataclasses import asdict, dataclass, field
from datetime import datetime, timezone
from pathlib import Path

LINUX_PROFILE = "linux-x64-debug"
MACOS_PROFILE = "macos-x64-debug"
DEFAULT_COMMAND_TIMEOUT_SEC = 15 * 60
RESULT_SCHEMA_VERSION = "apptraverse.platform_result/1"
COMMAND_SCHEMA_VERSION = "apptraverse.platform_command/1"
ARTIFACT_ROOT_REL = Path(".artifacts") / "apptraverse-platform"
USER_CONFIG_REL = Path("cmake") / "aether_user_config.h"
MAX_FIRST_ERROR_CHARS = 1000
MAX_EXCERPT_LINES = 40
MAX_EXCERPT_CHARS = 4000
EXCERPT_CONTEXT = 20

PROFILES = {
    LINUX_PROFILE: {
        "host_prefix": "linux",
        "generator": "Ninja",
        "build_dir": Path("build") / "linux-x64-debug",
        "default_target": "apptraverse_event_sourced_core_test",
        "exe_rel": Path("build")
        / "linux-x64-debug"
        / "tests"
        / "apptraverse_event_sourced_core_test",
        "require_ninja": True,
        "require_cxx": True,
        "require_gtk3": False,
        "build_parallel": None,
        "cache_variables": {},
        "cxx_names": ("g++", "c++"),
    },
    MACOS_PROFILE: {
        "host_prefix": "darwin",
        "generator": "Ninja",
        "build_dir": Path("build") / "macos-x64-debug",
        "default_target": "apptraverse_event_sourced_core_test",
        "exe_rel": Path("build")
        / "macos-x64-debug"
        / "tests"
        / "apptraverse_event_sourced_core_test",
        "require_ninja": True,
        "require_cxx": True,
        "require_gtk3": False,
        "require_macports_clang20": False,
        "build_parallel": None,
        "cache_variables": {
            "CMAKE_OSX_ARCHITECTURES": "x86_64",
        },
        "cxx_names": ("clang++", "c++"),
    },
}

STATUS_OK = "ok"
STATUS_BLOCKED = "blocked"
STATUS_FAILED = "failed"

_RE_NINJA_ERROR = re.compile(r"^ninja: error", re.IGNORECASE)
_RE_CMAKE_ERROR = re.compile(r"CMake Error")
_RE_GENERIC_FATAL = re.compile(r"fatal error", re.IGNORECASE)
_RE_GENERIC_ERROR = re.compile(r"error:", re.IGNORECASE)


class CommandTimeout(Exception):
    def __init__(
        self,
        argv: list[str],
        timeout_sec: int,
        *,
        run_id: str | None = None,
        artifact_id: str | None = None,
        first_error: str | None = None,
        duration_ms: int | None = None,
    ) -> None:
        super().__init__(f"timed out after {timeout_sec}s: {argv}")
        self.argv = argv
        self.timeout_sec = timeout_sec
        self.run_id = run_id
        self.artifact_id = artifact_id
        self.first_error = first_error
        self.duration_ms = duration_ms


@dataclass
class PlatformResult:
    schema_version: str = RESULT_SCHEMA_VERSION
    run_id: str | None = None
    artifact_id: str | None = None
    status: str = STATUS_OK
    stage: str = ""
    profile: str = ""
    targets: list[str] = field(default_factory=list)
    action: str | None = None
    duration_ms: int | None = None
    exit_code: int | None = None
    failure_kind: str | None = None
    first_error: str | None = None

    def to_public_dict(self) -> dict:
        return asdict(self)


@dataclass
class CommandExecution:
    returncode: int
    duration_ms: int
    timed_out: bool
    stdout_text: str
    stderr_text: str


def repo_root() -> Path:
    return Path(__file__).resolve().parents[2]


def new_run_id() -> str:
    stamp = datetime.now(timezone.utc).strftime("%Y%m%d-%H%M%S")
    return f"r-{stamp}-{secrets.token_hex(3)}"


def artifact_id_for(run_id: str) -> str:
    return f"apptraverse-platform/{run_id}"


def prepare_run_dir(source_dir: Path, run_id: str) -> Path:
    run_dir = source_dir / ARTIFACT_ROOT_REL / run_id
    run_dir.mkdir(parents=True, exist_ok=True)
    return run_dir


def host_matches(profile: str, platform: str) -> bool:
    prefix = PROFILES[profile]["host_prefix"]
    return platform.startswith(prefix)


def default_targets(profile: str, targets: list[str]) -> list[str]:
    if targets:
        return list(targets)
    return [PROFILES[profile]["default_target"]]


def cmake_configure_argv(profile: str, source_dir: Path) -> list[str]:
    spec = PROFILES[profile]
    cmake_source = spec.get("cmake_source_dir", Path("."))
    argv = [
        "cmake",
        "-S",
        cmake_source.as_posix(),
        "-B",
        spec["build_dir"].as_posix(),
        "-G",
        spec["generator"],
        "-DCMAKE_BUILD_TYPE=Debug",
    ]
    for key, value in (spec.get("cache_variables") or {}).items():
        argv.append(f"-D{key}={value}")
    user_config = source_dir / USER_CONFIG_REL
    if user_config.is_file():
        argv.append(f"-DUSER_CONFIG={user_config}")
    return argv


def cmake_build_argv(profile: str, targets: list[str]) -> list[str]:
    spec = PROFILES[profile]
    argv = ["cmake", "--build", spec["build_dir"].as_posix()]
    for target in targets:
        argv.extend(["--target", target])
    parallel = spec.get("build_parallel")
    if parallel:
        argv.extend(["--parallel", str(parallel)])
    return argv


def process_argv(source_dir: Path, profile: str, state_dir: str | None = None) -> list[str]:
    del state_dir
    exe = source_dir / PROFILES[profile]["exe_rel"]
    return [str(exe)]


def exe_path_for(source_dir: Path, profile: str) -> Path:
    return source_dir / PROFILES[profile]["exe_rel"]


def command_is_destructive(argv: list[str]) -> bool:
    lowered = [part.lower() for part in argv]
    forbidden = {
        "--clean-first",
        "clean",
        "rebuild",
        "/t:rebuild",
        "/t:clean",
    }
    if any(token in forbidden for token in lowered):
        return True
    joined = " ".join(lowered)
    if "--clean-first" in joined or "/t:rebuild" in joined:
        return True
    return False


def parse_cache_generator(cache_text: str) -> str | None:
    for line in cache_text.splitlines():
        stripped = line.strip()
        if stripped.startswith("CMAKE_GENERATOR:") and "=" in stripped:
            return stripped.split("=", 1)[1].strip()
    return None


def inspect_cache(cache_text: str, expected_generator: str) -> tuple[str, str]:
    generator = parse_cache_generator(cache_text)
    if generator is None:
        return "conflict", "CMAKE_GENERATOR missing from CMakeCache.txt"
    if generator != expected_generator:
        return "conflict", f"CMAKE_GENERATOR={generator} (expected {expected_generator})"
    return "ok", "ninja"


def decide_configure_action(
    build_dir: Path,
    cache_text: str | None,
    expected_generator: str,
) -> tuple[str, str, str | None]:
    if not build_dir.exists():
        return STATUS_OK, "configure", None
    if cache_text is None:
        return STATUS_OK, "configure", None
    cache_status, detail = inspect_cache(cache_text, expected_generator)
    if cache_status == "ok":
        return STATUS_OK, "already_configured", None
    return STATUS_BLOCKED, "build_profile_conflict", detail


def gtk3_available(*, which=shutil.which, run=subprocess.run) -> bool:
    pkg = which("pkg-config")
    if pkg is None:
        return False
    proc = run(
        [pkg, "--exists", "gtk+-3.0"],
        capture_output=True,
        shell=False,
        timeout=30,
    )
    return proc.returncode == 0


def preflight(
    profile: str,
    source_dir: Path,
    *,
    platform: str | None = None,
    which=shutil.which,
    gtk3_ok: bool | None = None,
    macports_clang20_ok: bool | None = None,
) -> tuple[str, str | None, str | None]:
    if profile not in PROFILES:
        return STATUS_BLOCKED, "unsupported_profile", profile
    host = platform if platform is not None else sys.platform
    if not host_matches(profile, host):
        return STATUS_BLOCKED, "wrong_host_os", host
    spec = PROFILES[profile]
    if which("cmake") is None:
        return STATUS_BLOCKED, "cmake_missing", "cmake not on PATH"
    if spec["require_ninja"] and which("ninja") is None:
        return STATUS_BLOCKED, "ninja_missing", "ninja not on PATH"
    if spec["require_cxx"] and not any(which(name) for name in spec["cxx_names"]):
        return STATUS_BLOCKED, "cxx_missing", "C++ compiler not on PATH"
    if spec.get("require_macports_clang20"):
        if macports_clang20_ok is None:
            missing: list[str] = []
            for path in (
                "/opt/local/bin/clang-mp-20",
                "/opt/local/bin/clang++-mp-20",
            ):
                if not Path(path).is_file():
                    missing.append(path)
            macports_clang20_ok = not missing
        if not macports_clang20_ok:
            return (
                STATUS_BLOCKED,
                "macports_clang20_missing",
                "missing MacPorts LLVM 20 toolchain: "
                "/opt/local/bin/clang-mp-20, /opt/local/bin/clang++-mp-20",
            )
    if spec["require_gtk3"]:
        if which("pkg-config") is None:
            return STATUS_BLOCKED, "pkg_config_missing", "pkg-config not on PATH"
        ok = gtk3_available(which=which) if gtk3_ok is None else gtk3_ok
        if not ok:
            return STATUS_BLOCKED, "gtk3_missing", "gtk+-3.0 not found via pkg-config"
    return STATUS_OK, None, None


def terminate_process_tree(pid: int) -> None:
    if sys.platform.startswith("win"):
        subprocess.run(
            ["taskkill", "/F", "/T", "/PID", str(pid)],
            capture_output=True,
            shell=False,
        )
        return
    try:
        os.killpg(pid, signal.SIGTERM)
    except OSError:
        try:
            os.kill(pid, signal.SIGTERM)
        except OSError:
            pass


def bound_first_error(text: str) -> str:
    if len(text) <= MAX_FIRST_ERROR_CHARS:
        return text
    return text[: MAX_FIRST_ERROR_CHARS - 3] + "..."


def bound_excerpt_text(text: str) -> str:
    lines = text.splitlines()
    if len(lines) > MAX_EXCERPT_LINES:
        lines = lines[:MAX_EXCERPT_LINES]
    clipped = "\n".join(lines)
    if len(clipped) > MAX_EXCERPT_CHARS:
        clipped = clipped[:MAX_EXCERPT_CHARS]
    return clipped


def extract_first_error(text: str) -> tuple[str, list[str]]:
    lines = text.splitlines()
    if not lines:
        return "", []
    patterns = (
        _RE_NINJA_ERROR,
        _RE_CMAKE_ERROR,
        _RE_GENERIC_FATAL,
        _RE_GENERIC_ERROR,
    )
    index = None
    for pattern in patterns:
        for i, line in enumerate(lines):
            if pattern.search(line):
                index = i
                break
        if index is not None:
            break
    if index is None:
        nonempty = [i for i, line in enumerate(lines) if line.strip()]
        index = nonempty[-1] if nonempty else len(lines) - 1
    start = max(0, index - EXCERPT_CONTEXT)
    end = min(len(lines), index + EXCERPT_CONTEXT + 1)
    excerpt = lines[start:end]
    if len(excerpt) > MAX_EXCERPT_LINES:
        excerpt = excerpt[:MAX_EXCERPT_LINES]
    excerpt_text = bound_excerpt_text("\n".join(excerpt))
    excerpt = excerpt_text.splitlines()
    return bound_first_error(lines[index].strip()), excerpt


def write_command_json(
    path: Path,
    *,
    profile: str,
    stage: str,
    targets: list[str],
    argv: list[str],
    started_at_utc: str,
) -> None:
    payload = {
        "schema_version": COMMAND_SCHEMA_VERSION,
        "profile": profile,
        "stage": stage,
        "targets": targets,
        "argv": argv,
        "started_at_utc": started_at_utc,
    }
    path.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")


def execute_external(
    argv: list[str],
    cwd: Path,
    stdout_path: Path,
    stderr_path: Path,
    timeout_sec: int = DEFAULT_COMMAND_TIMEOUT_SEC,
) -> CommandExecution:
    if command_is_destructive(argv):
        raise ValueError(f"refusing destructive command: {argv}")
    started = time.perf_counter()
    stdout_path.parent.mkdir(parents=True, exist_ok=True)
    with stdout_path.open("w", encoding="utf-8", errors="replace") as out_file:
        with stderr_path.open("w", encoding="utf-8", errors="replace") as err_file:
            proc = subprocess.Popen(
                argv,
                cwd=cwd,
                stdout=out_file,
                stderr=err_file,
                text=True,
                encoding="utf-8",
                errors="replace",
                shell=False,
            )
            timed_out = False
            try:
                proc.wait(timeout=timeout_sec)
            except subprocess.TimeoutExpired as exc:
                timed_out = True
                terminate_process_tree(proc.pid)
                try:
                    proc.wait(timeout=5)
                except Exception:
                    try:
                        proc.kill()
                        proc.wait(timeout=5)
                    except Exception:
                        pass
                duration_ms = int((time.perf_counter() - started) * 1000)
                raise CommandTimeout(
                    argv, timeout_sec, duration_ms=duration_ms
                ) from exc
    duration_ms = int((time.perf_counter() - started) * 1000)
    stdout_text = stdout_path.read_text(encoding="utf-8", errors="replace")
    stderr_text = stderr_path.read_text(encoding="utf-8", errors="replace")
    return CommandExecution(
        returncode=proc.returncode if proc.returncode is not None else 1,
        duration_ms=duration_ms,
        timed_out=timed_out,
        stdout_text=stdout_text,
        stderr_text=stderr_text,
    )


def build_reported_idle(text: str) -> bool:
    lowered = text.lower()
    return (
        "ninja: no work to do" in lowered
        or "up-to-date" in lowered
        or "up to date" in lowered
    )


def emit_result(result: PlatformResult, json_mode: bool) -> None:
    if json_mode:
        sys.stdout.write(json.dumps(result.to_public_dict(), separators=(",", ":")) + "\n")
        sys.stdout.flush()
        return
    if result.status != STATUS_OK:
        first = (result.first_error or "").replace("\n", " ")
        parts = [
            f"failure_kind={result.failure_kind or 'unknown'}",
            f"first_error={first}",
            f"artifact_id={result.artifact_id or ''}",
        ]
        print(" ".join(parts))
        return
    fields = [
        f"status={result.status}",
        f"stage={result.stage}",
        f"profile={result.profile}",
    ]
    if result.action:
        fields.append(f"action={result.action}")
    if result.targets:
        fields.append("target=" + ",".join(result.targets))
    if result.artifact_id:
        fields.append(f"artifact_id={result.artifact_id}")
    print(" ".join(fields))


def exit_code_for(result: PlatformResult) -> int:
    if result.status == STATUS_OK:
        return 0
    if result.status == STATUS_BLOCKED:
        return 2
    return 1


def persist_result_files(
    run_dir: Path,
    result: PlatformResult,
    excerpt_lines: list[str] | None,
) -> None:
    (run_dir / "result.json").write_text(
        json.dumps(result.to_public_dict(), indent=2) + "\n",
        encoding="utf-8",
    )
    if excerpt_lines:
        (run_dir / "failure_excerpt.txt").write_text(
            bound_excerpt_text("\n".join(excerpt_lines)) + "\n",
            encoding="utf-8",
        )


def run_logged_command(
    source_dir: Path,
    argv: list[str],
    *,
    profile: str,
    stage: str,
    targets: list[str],
) -> tuple[str, str, CommandExecution | None, str, list[str]]:
    run_id = new_run_id()
    artifact_id = artifact_id_for(run_id)
    run_dir = prepare_run_dir(source_dir, run_id)
    started_at = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
    write_command_json(
        run_dir / "command.json",
        profile=profile,
        stage=stage,
        targets=targets,
        argv=argv,
        started_at_utc=started_at,
    )
    stdout_path = run_dir / "stdout.log"
    stderr_path = run_dir / "stderr.log"
    try:
        execution = execute_external(argv, source_dir, stdout_path, stderr_path)
    except CommandTimeout as exc:
        stdout_text = (
            stdout_path.read_text(encoding="utf-8", errors="replace")
            if stdout_path.exists()
            else ""
        )
        stderr_text = (
            stderr_path.read_text(encoding="utf-8", errors="replace")
            if stderr_path.exists()
            else ""
        )
        first_error, excerpt = extract_first_error(stdout_text + "\n" + stderr_text)
        timeout_result = PlatformResult(
            run_id=run_id,
            artifact_id=artifact_id,
            status=STATUS_BLOCKED,
            stage=stage,
            profile=profile,
            targets=targets,
            duration_ms=exc.duration_ms or DEFAULT_COMMAND_TIMEOUT_SEC * 1000,
            exit_code=None,
            failure_kind="command_timeout",
            first_error=first_error or "command_timeout",
        )
        persist_result_files(run_dir, timeout_result, excerpt or None)
        raise CommandTimeout(
            argv,
            exc.timeout_sec,
            run_id=run_id,
            artifact_id=artifact_id,
            first_error=timeout_result.first_error,
            duration_ms=timeout_result.duration_ms,
        ) from exc
    combined = execution.stdout_text + "\n" + execution.stderr_text
    first_error, excerpt = extract_first_error(combined)
    return run_id, artifact_id, execution, first_error, excerpt


def stage_preflight(source_dir: Path, profile: str) -> PlatformResult:
    status, kind, reason = preflight(profile, source_dir)
    if status == STATUS_OK:
        return PlatformResult(
            status=STATUS_OK,
            stage="preflight",
            profile=profile,
            exit_code=0,
        )
    return PlatformResult(
        status=STATUS_BLOCKED,
        stage="preflight",
        profile=profile,
        failure_kind=kind or "preflight_failed",
        first_error=reason,
        exit_code=2,
    )


def stage_configure(source_dir: Path, profile: str) -> PlatformResult:
    pre_status, kind, reason = preflight(profile, source_dir)
    if pre_status != STATUS_OK:
        return PlatformResult(
            status=STATUS_BLOCKED,
            stage="preflight",
            profile=profile,
            failure_kind=kind or "preflight_failed",
            first_error=reason,
            exit_code=2,
        )
    spec = PROFILES[profile]
    build_dir = source_dir / spec["build_dir"]
    cache_path = build_dir / "CMakeCache.txt"
    cache_text = (
        cache_path.read_text(encoding="utf-8", errors="replace")
        if cache_path.is_file()
        else None
    )
    status, action, conflict_reason = decide_configure_action(
        build_dir, cache_text, spec["generator"]
    )
    if status == STATUS_BLOCKED:
        return PlatformResult(
            status=STATUS_BLOCKED,
            stage="configure",
            profile=profile,
            failure_kind=action,
            first_error=conflict_reason,
            exit_code=2,
        )
    if action == "already_configured":
        return PlatformResult(
            status=STATUS_OK,
            stage="configure",
            profile=profile,
            action="already_configured",
            exit_code=0,
        )
    argv = cmake_configure_argv(profile, source_dir)
    try:
        run_id, artifact_id, execution, first_error, excerpt = run_logged_command(
            source_dir, argv, profile=profile, stage="configure", targets=[]
        )
    except CommandTimeout as exc:
        return PlatformResult(
            run_id=exc.run_id,
            artifact_id=exc.artifact_id,
            status=STATUS_BLOCKED,
            stage="configure",
            profile=profile,
            failure_kind="command_timeout",
            first_error=exc.first_error or "command_timeout",
            duration_ms=exc.duration_ms,
            exit_code=None,
        )
    assert execution is not None
    result = PlatformResult(
        run_id=run_id,
        artifact_id=artifact_id,
        status=STATUS_OK if execution.returncode == 0 else STATUS_FAILED,
        stage="configure",
        profile=profile,
        action="configured" if execution.returncode == 0 else None,
        duration_ms=execution.duration_ms,
        exit_code=execution.returncode,
        failure_kind=None if execution.returncode == 0 else "configure_failed",
        first_error=None if execution.returncode == 0 else first_error,
    )
    persist_result_files(
        source_dir / ARTIFACT_ROOT_REL / run_id,
        result,
        None if execution.returncode == 0 else excerpt,
    )
    return result


def stage_build(source_dir: Path, profile: str, targets: list[str]) -> PlatformResult:
    targets = default_targets(profile, targets)
    pre_status, kind, reason = preflight(profile, source_dir)
    if pre_status != STATUS_OK:
        return PlatformResult(
            status=STATUS_BLOCKED,
            stage="preflight",
            profile=profile,
            targets=targets,
            failure_kind=kind or "preflight_failed",
            first_error=reason,
            exit_code=2,
        )
    spec = PROFILES[profile]
    build_dir = source_dir / spec["build_dir"]
    cache_path = build_dir / "CMakeCache.txt"
    if cache_path.is_file():
        cache_text = cache_path.read_text(encoding="utf-8", errors="replace")
        status, action, conflict_reason = decide_configure_action(
            build_dir, cache_text, spec["generator"]
        )
        if status == STATUS_BLOCKED:
            return PlatformResult(
                status=STATUS_BLOCKED,
                stage="build",
                profile=profile,
                targets=targets,
                failure_kind=action,
                first_error=conflict_reason,
                exit_code=2,
            )
    argv = cmake_build_argv(profile, targets)
    try:
        run_id, artifact_id, execution, first_error, excerpt = run_logged_command(
            source_dir, argv, profile=profile, stage="build", targets=targets
        )
    except CommandTimeout as exc:
        return PlatformResult(
            run_id=exc.run_id,
            artifact_id=exc.artifact_id,
            status=STATUS_BLOCKED,
            stage="build",
            profile=profile,
            targets=targets,
            failure_kind="command_timeout",
            first_error=exc.first_error or "command_timeout",
            duration_ms=exc.duration_ms,
            exit_code=None,
        )
    assert execution is not None
    idle = build_reported_idle(execution.stdout_text + execution.stderr_text)
    result = PlatformResult(
        run_id=run_id,
        artifact_id=artifact_id,
        status=STATUS_OK if execution.returncode == 0 else STATUS_FAILED,
        stage="build",
        profile=profile,
        targets=targets,
        action="up_to_date" if execution.returncode == 0 and idle else None,
        duration_ms=execution.duration_ms,
        exit_code=execution.returncode,
        failure_kind=None if execution.returncode == 0 else "compile_failed",
        first_error=None if execution.returncode == 0 else first_error,
    )
    persist_result_files(
        source_dir / ARTIFACT_ROOT_REL / run_id,
        result,
        None if execution.returncode == 0 else excerpt,
    )
    return result


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Canonical App Traverse POSIX platform runner (Linux or macOS)."
    )
    parser.add_argument("--profile", required=True)
    parser.add_argument(
        "--stage",
        required=True,
        choices=("preflight", "configure", "build"),
    )
    parser.add_argument("--target", action="append", default=[])
    parser.add_argument("--json", action="store_true")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    source_dir = repo_root()
    os.chdir(source_dir)
    try:
        if args.stage == "preflight":
            result = stage_preflight(source_dir, args.profile)
        elif args.stage == "configure":
            result = stage_configure(source_dir, args.profile)
        else:
            result = stage_build(source_dir, args.profile, args.target)
    except CommandTimeout as exc:
        result = PlatformResult(
            status=STATUS_BLOCKED,
            stage=args.stage,
            profile=args.profile,
            targets=args.target,
            failure_kind="command_timeout",
            first_error="command_timeout",
            duration_ms=exc.timeout_sec * 1000,
        )
    emit_result(result, json_mode=args.json)
    return exit_code_for(result)


if __name__ == "__main__":
    sys.exit(main())
