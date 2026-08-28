#!/usr/bin/env python3
"""Canonical staged build runner. Windows Ninja and VS 2022/MSVC profiles."""

from __future__ import annotations

import argparse
import json
import os
import re
import secrets
import shutil
import subprocess
import sys
import time
from dataclasses import asdict, dataclass, field
from datetime import datetime, timezone
from pathlib import Path

NINJA_PROFILE = "win64-ninja-msvc-debug"
VS2022_PROFILE = "win64-vs2022-msvc-debug"
VS2022_GENERATOR = "Visual Studio 17 2022"
DEFAULT_COMMAND_TIMEOUT_SEC = 15 * 60
RESULT_SCHEMA_VERSION = "apptraverse.build_result/1"
COMMAND_SCHEMA_VERSION = "apptraverse.build_command/1"
ARTIFACT_ROOT_REL = Path(".artifacts") / "apptraverse-build"
MAX_FIRST_ERROR_CHARS = 1000
MAX_EXCERPT_LINES = 40
EXCERPT_CONTEXT = 20

PROFILES = {
    NINJA_PROFILE: {
        "generator": "Ninja",
        "build_dir": Path("build") / "win64-ninja-msvc-debug",
        "require_ninja": True,
        "require_cl": True,
        "multi_config": False,
    },
    VS2022_PROFILE: {
        "generator": VS2022_GENERATOR,
        "build_dir": Path("build") / "win64-vs2022-msvc-debug",
        "require_ninja": False,
        "require_cl": False,
        "multi_config": True,
        "config": "Debug",
    },
}

STATUS_OK = "ok"
STATUS_BLOCKED = "blocked"
STATUS_FAILED = "failed"
SUPPORTED_PROFILE = NINJA_PROFILE

_RE_MSVC_FATAL = re.compile(r"fatal error C\d+", re.IGNORECASE)
_RE_MSVC_ERROR = re.compile(r"error C\d+", re.IGNORECASE)
_RE_LNK_ERROR = re.compile(r"error LNK\d+", re.IGNORECASE)
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
class BuildResult:
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
    return f"{stamp}-{secrets.token_hex(3)}"


def artifact_id_for(run_id: str) -> str:
    return f"apptraverse-build/{run_id}"


def prepare_run_dir(source_dir: Path, run_id: str) -> Path:
    run_dir = source_dir / ARTIFACT_ROOT_REL / run_id
    run_dir.mkdir(parents=True, exist_ok=True)
    return run_dir


def cmake_configure_argv(profile: str) -> list[str]:
    return ["cmake", "--preset", profile]


def cmake_build_argv(profile: str, targets: list[str]) -> list[str]:
    argv = ["cmake", "--build", "--preset", profile]
    spec = PROFILES[profile]
    if spec.get("multi_config"):
        argv.extend(["--config", spec["config"]])
    for target in targets:
        argv.extend(["--target", target])
    return argv


def command_is_destructive(argv: list[str]) -> bool:
    lowered = [part.lower() for part in argv]
    forbidden = {
        "--clean-first",
        "clean",
        "rebuild",
        "/t:rebuild",
        "/t:clean",
        "msbuild /t:rebuild",
    }
    if any(token in forbidden for token in lowered):
        return True
    joined = " ".join(lowered)
    if "/t:rebuild" in joined or " /t:clean" in joined:
        return True
    return False


def parse_cache_generator(cache_text: str) -> str | None:
    for line in cache_text.splitlines():
        stripped = line.strip()
        if stripped.startswith("CMAKE_GENERATOR:") and "=" in stripped:
            return stripped.split("=", 1)[1].strip()
    return None


def parse_cache_cxx_compiler(cache_text: str) -> str | None:
    for line in cache_text.splitlines():
        stripped = line.strip()
        if stripped.startswith("CMAKE_CXX_COMPILER:") and "=" in stripped:
            return stripped.split("=", 1)[1].strip()
    return None


def parse_cache_cxx_compiler_id(cache_text: str) -> str | None:
    for line in cache_text.splitlines():
        stripped = line.strip()
        if stripped.startswith("CMAKE_CXX_COMPILER_ID:") and "=" in stripped:
            return stripped.split("=", 1)[1].strip()
    return None


def compiler_is_msvc_cl(compiler: str) -> bool:
    lowered = compiler.replace("\\", "/").lower()
    if "mingw" in lowered or "msys64" in lowered or "/ucrt64/" in lowered:
        return False
    name = Path(compiler).name.lower()
    return name in {"cl", "cl.exe"}


def inspect_cache(cache_text: str, expected_generator: str) -> tuple[str, str]:
    generator = parse_cache_generator(cache_text)
    if generator is None:
        return "conflict", "CMAKE_GENERATOR missing from CMakeCache.txt"
    if generator != expected_generator:
        return "conflict", f"CMAKE_GENERATOR={generator} (expected {expected_generator})"
    compiler_id = parse_cache_cxx_compiler_id(cache_text)
    compiler = parse_cache_cxx_compiler(cache_text)
    if compiler_id is not None and compiler_id != "MSVC":
        return "conflict", f"CMAKE_CXX_COMPILER_ID={compiler_id} (expected MSVC)"
    if compiler is not None and not compiler_is_msvc_cl(compiler):
        if expected_generator == "Ninja":
            return "conflict", f"CMAKE_CXX_COMPILER is not MSVC cl: {compiler}"
        if compiler_id is None:
            return "conflict", f"CMAKE_CXX_COMPILER is not MSVC cl: {compiler}"
    if expected_generator == "Ninja":
        if compiler is None:
            return "conflict", "CMAKE_CXX_COMPILER missing from CMakeCache.txt"
        if not compiler_is_msvc_cl(compiler):
            return "conflict", f"CMAKE_CXX_COMPILER is not MSVC cl: {compiler}"
        return "ok", "ninja+msvc"
    return "ok", "vs2022+msvc"


def load_configure_preset_names(presets_path: Path) -> list[str]:
    data = json.loads(presets_path.read_text(encoding="utf-8"))
    names: list[str] = []
    for preset in data.get("configurePresets", []):
        name = preset.get("name")
        if name:
            names.append(name)
    return names


def list_cmake_generators(
    *,
    which=shutil.which,
    capabilities_json: str | None = None,
) -> list[str]:
    if capabilities_json is not None:
        data = json.loads(capabilities_json)
        return [item.get("name", "") for item in data.get("generators", [])]
    cmake = which("cmake")
    if cmake is None:
        return []
    proc = subprocess.run(
        [cmake, "-E", "capabilities"],
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        shell=False,
        timeout=60,
    )
    if proc.returncode != 0:
        return []
    data = json.loads(proc.stdout or "{}")
    return [item.get("name", "") for item in data.get("generators", [])]


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


def find_vcvars64_bat() -> Path | None:
    program_files_x86 = os.environ.get("ProgramFiles(x86)") or r"C:\Program Files (x86)"
    vswhere = Path(program_files_x86) / "Microsoft Visual Studio" / "Installer" / "vswhere.exe"
    if not vswhere.is_file():
        return None
    proc = subprocess.run(
        [
            str(vswhere),
            "-latest",
            "-products",
            "*",
            "-requires",
            "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
            "-property",
            "installationPath",
        ],
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        shell=False,
        check=False,
    )
    install = (proc.stdout or "").strip()
    if not install:
        return None
    candidate = Path(install) / "VC" / "Auxiliary" / "Build" / "vcvars64.bat"
    return candidate if candidate.is_file() else None


def parse_cmd_set_output(text: str) -> dict[str, str]:
    env: dict[str, str] = {}
    for line in text.splitlines():
        if not line or "=" not in line:
            continue
        key, _, value = line.partition("=")
        if key:
            env[key] = value
    return env


def load_msvc_environment(*, vcvars: Path | None = None) -> dict[str, str] | None:
    bat = vcvars if vcvars is not None else find_vcvars64_bat()
    if bat is None:
        return None
    # Import Developer Command Prompt variables into this process.
    # shell=True is required so cmd.exe parses quoting for paths with spaces;
    # argv-list form ends up passing literal \" which breaks vcvars64.bat.
    command = f'call "{bat}" >nul && set'
    proc = subprocess.run(
        command,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        shell=True,
        check=False,
    )
    if proc.returncode != 0:
        return None
    env = parse_cmd_set_output(proc.stdout or "")
    return env or None


def ensure_msvc_on_path(*, which=shutil.which) -> bool:
    if which("cl") is not None:
        return True
    env = load_msvc_environment()
    if not env:
        return False
    os.environ.update(env)
    return which("cl") is not None


def preflight(
    profile: str,
    source_dir: Path,
    *,
    platform: str | None = None,
    which=shutil.which,
    generators: list[str] | None = None,
) -> tuple[str, str | None, str | None]:
    if profile not in PROFILES:
        return STATUS_BLOCKED, "unsupported_profile", profile
    host = platform if platform is not None else sys.platform
    if not host.startswith("win"):
        return STATUS_BLOCKED, "unsupported_platform", host
    presets_path = source_dir / "CMakePresets.json"
    if not presets_path.is_file():
        return STATUS_BLOCKED, "preset_missing", "CMakePresets.json not found"
    names = load_configure_preset_names(presets_path)
    if profile not in names:
        return STATUS_BLOCKED, "preset_missing", profile
    if which("cmake") is None:
        return STATUS_BLOCKED, "cmake_missing", "cmake not on PATH"
    spec = PROFILES[profile]
    if spec["require_ninja"] and which("ninja") is None:
        return STATUS_BLOCKED, "ninja_missing", "ninja not on PATH"
    if spec["require_cl"] and which("cl") is None:
        if not ensure_msvc_on_path(which=which):
            return STATUS_BLOCKED, "msvc_environment_missing", "cl.exe not on PATH"
    if profile == VS2022_PROFILE:
        known = generators if generators is not None else list_cmake_generators(which=which)
        if VS2022_GENERATOR not in known:
            return STATUS_BLOCKED, "visual_studio_generator_missing", VS2022_GENERATOR
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
        os.kill(pid, 15)
    except OSError:
        pass


def bound_first_error(text: str) -> str:
    if len(text) <= MAX_FIRST_ERROR_CHARS:
        return text
    return text[: MAX_FIRST_ERROR_CHARS - 3] + "..."


def extract_first_error(text: str) -> tuple[str, list[str]]:
    lines = text.splitlines()
    if not lines:
        return "", []
    patterns = (
        _RE_MSVC_FATAL,
        _RE_MSVC_ERROR,
        _RE_LNK_ERROR,
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
                env=os.environ.copy(),
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


def run_command(
    argv: list[str],
    cwd: Path,
    timeout_sec: int = DEFAULT_COMMAND_TIMEOUT_SEC,
):
    """Back-compat wrapper used by older unit tests."""
    import tempfile

    with tempfile.TemporaryDirectory() as tmp:
        stdout_path = Path(tmp) / "stdout.log"
        stderr_path = Path(tmp) / "stderr.log"
        execution = execute_external(
            argv, cwd, stdout_path, stderr_path, timeout_sec=timeout_sec
        )
        return execution


def build_reported_idle(text: str) -> bool:
    lowered = text.lower()
    return (
        "ninja: no work to do" in lowered
        or "up-to-date" in lowered
        or "up to date" in lowered
    )


def emit_result(result: BuildResult, json_mode: bool) -> None:
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


def exit_code_for(result: BuildResult) -> int:
    if result.status == STATUS_OK:
        return 0
    if result.status == STATUS_BLOCKED:
        return 2
    return 1


def persist_result_files(
    run_dir: Path,
    result: BuildResult,
    excerpt_lines: list[str] | None,
) -> None:
    (run_dir / "result.json").write_text(
        json.dumps(result.to_public_dict(), indent=2) + "\n",
        encoding="utf-8",
    )
    if excerpt_lines:
        (run_dir / "failure_excerpt.txt").write_text(
            "\n".join(excerpt_lines) + "\n",
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
        timeout_result = BuildResult(
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


def stage_preflight(source_dir: Path, profile: str) -> BuildResult:
    status, kind, reason = preflight(profile, source_dir)
    if status == STATUS_OK:
        return BuildResult(
            status=STATUS_OK,
            stage="preflight",
            profile=profile,
            exit_code=0,
        )
    return BuildResult(
        status=STATUS_BLOCKED,
        stage="preflight",
        profile=profile,
        failure_kind=kind or "preflight_failed",
        first_error=reason,
        exit_code=2,
    )


def stage_configure(source_dir: Path, profile: str) -> BuildResult:
    pre_status, kind, reason = preflight(profile, source_dir)
    if pre_status != STATUS_OK:
        return BuildResult(
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
        return BuildResult(
            status=STATUS_BLOCKED,
            stage="configure",
            profile=profile,
            failure_kind=action,
            first_error=conflict_reason,
            exit_code=2,
        )
    if action == "already_configured":
        return BuildResult(
            status=STATUS_OK,
            stage="configure",
            profile=profile,
            action="already_configured",
            exit_code=0,
        )
    argv = cmake_configure_argv(profile)
    try:
        run_id, artifact_id, execution, first_error, excerpt = run_logged_command(
            source_dir, argv, profile=profile, stage="configure", targets=[]
        )
    except CommandTimeout as exc:
        return BuildResult(
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
    result = BuildResult(
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


def stage_build(source_dir: Path, profile: str, targets: list[str]) -> BuildResult:
    if not targets:
        return BuildResult(
            status=STATUS_BLOCKED,
            stage="build",
            profile=profile,
            failure_kind="missing_target",
            exit_code=2,
        )
    pre_status, kind, reason = preflight(profile, source_dir)
    if pre_status != STATUS_OK:
        return BuildResult(
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
            return BuildResult(
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
        return BuildResult(
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
    result = BuildResult(
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
        description="Canonical App Traverse staged build runner (Windows Ninja or VS 2022)."
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
        result = BuildResult(
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
