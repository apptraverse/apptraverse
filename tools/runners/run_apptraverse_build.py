#!/usr/bin/env python3
"""Canonical staged build runner. Windows Ninja and VS 2022/MSVC profiles."""

from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path
from types import SimpleNamespace

NINJA_PROFILE = "win64-ninja-msvc-debug"
VS2022_PROFILE = "win64-vs2022-msvc-debug"
VS2022_GENERATOR = "Visual Studio 17 2022"
DEFAULT_COMMAND_TIMEOUT_SEC = 15 * 60

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


class CommandTimeout(Exception):
    def __init__(self, argv: list[str], timeout_sec: int) -> None:
        super().__init__(f"timed out after {timeout_sec}s: {argv}")
        self.argv = argv
        self.timeout_sec = timeout_sec


def repo_root() -> Path:
    return Path(__file__).resolve().parents[2]


def emit(**fields: str) -> None:
    print(" ".join(f"{key}={value}" for key, value in fields.items()))


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
    """Return (status, detail). status is ok or conflict."""
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


def run_command(
    argv: list[str],
    cwd: Path,
    timeout_sec: int = DEFAULT_COMMAND_TIMEOUT_SEC,
) -> subprocess.CompletedProcess[str] | SimpleNamespace:
    if command_is_destructive(argv):
        raise ValueError(f"refusing destructive command: {argv}")
    proc = subprocess.Popen(
        argv,
        cwd=cwd,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        encoding="utf-8",
        errors="replace",
        shell=False,
    )
    try:
        out, _err = proc.communicate(timeout=timeout_sec)
    except subprocess.TimeoutExpired as exc:
        terminate_process_tree(proc.pid)
        try:
            proc.communicate(timeout=5)
        except Exception:
            try:
                proc.kill()
                proc.communicate()
            except Exception:
                pass
        raise CommandTimeout(argv, timeout_sec) from exc
    return SimpleNamespace(returncode=proc.returncode, stdout=out or "", stderr="")


def first_error_tail(proc: subprocess.CompletedProcess[str] | SimpleNamespace) -> str:
    combined = (getattr(proc, "stdout", "") or "") + (getattr(proc, "stderr", "") or "")
    lines = combined.splitlines()
    useful = [
        line
        for line in lines
        if "error" in line.lower() or "fatal" in line.lower()
    ]
    chosen = useful[-12:] if useful else lines[-12:]
    return "\n".join(chosen)


def build_reported_idle(proc: subprocess.CompletedProcess[str] | SimpleNamespace) -> bool:
    combined = ((getattr(proc, "stdout", "") or "") + (getattr(proc, "stderr", "") or "")).lower()
    return (
        "ninja: no work to do" in combined
        or "up-to-date" in combined
        or "up to date" in combined
    )


def emit_preflight_failure(kind: str | None, reason: str | None) -> int:
    fields = {
        "status": STATUS_BLOCKED,
        "stage": "preflight",
        "failure_kind": kind or "preflight_failed",
    }
    if reason:
        fields["reason"] = reason.replace(" ", "_")
    emit(**fields)
    return 2


def stage_preflight(source_dir: Path, profile: str) -> int:
    status, kind, reason = preflight(profile, source_dir)
    if status == STATUS_OK:
        emit(status=STATUS_OK, stage="preflight", profile=profile)
        return 0
    return emit_preflight_failure(kind, reason)


def stage_configure(source_dir: Path, profile: str) -> int:
    pre_status, kind, reason = preflight(profile, source_dir)
    if pre_status != STATUS_OK:
        return emit_preflight_failure(kind, reason)

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
        emit(
            status=STATUS_BLOCKED,
            stage="configure",
            failure_kind=action,
            reason=(conflict_reason or "profile_mismatch").replace(" ", "_"),
        )
        return 2
    if action == "already_configured":
        emit(status=STATUS_OK, stage="configure", action="already_configured")
        return 0

    argv = cmake_configure_argv(profile)
    try:
        proc = run_command(argv, source_dir)
    except CommandTimeout:
        emit(
            status=STATUS_BLOCKED,
            stage="configure",
            failure_kind="command_timeout",
        )
        return 2
    if proc.returncode != 0:
        emit(status=STATUS_FAILED, stage="configure", failure_kind="configure_failed")
        print("command=" + " ".join(argv))
        print(first_error_tail(proc))
        return proc.returncode
    emit(status=STATUS_OK, stage="configure", action="configured")
    return 0


def stage_build(source_dir: Path, profile: str, targets: list[str]) -> int:
    if not targets:
        emit(status=STATUS_BLOCKED, stage="build", failure_kind="missing_target")
        return 2
    pre_status, kind, reason = preflight(profile, source_dir)
    if pre_status != STATUS_OK:
        return emit_preflight_failure(kind, reason)

    spec = PROFILES[profile]
    build_dir = source_dir / spec["build_dir"]
    cache_path = build_dir / "CMakeCache.txt"
    if cache_path.is_file():
        cache_text = cache_path.read_text(encoding="utf-8", errors="replace")
        status, action, conflict_reason = decide_configure_action(
            build_dir, cache_text, spec["generator"]
        )
        if status == STATUS_BLOCKED:
            emit(
                status=STATUS_BLOCKED,
                stage="build",
                failure_kind=action,
                reason=(conflict_reason or "profile_mismatch").replace(" ", "_"),
            )
            return 2

    argv = cmake_build_argv(profile, targets)
    target_text = ",".join(targets)
    try:
        proc = run_command(argv, source_dir)
    except CommandTimeout:
        emit(
            status=STATUS_BLOCKED,
            stage="build",
            failure_kind="command_timeout",
            target=target_text,
        )
        return 2
    if proc.returncode != 0:
        emit(
            status=STATUS_FAILED,
            stage="build",
            failure_kind="compile_failed",
            target=target_text,
        )
        print("command=" + " ".join(argv))
        print(first_error_tail(proc))
        return proc.returncode
    fields = {
        "status": STATUS_OK,
        "stage": "build",
        "target": target_text,
    }
    if build_reported_idle(proc):
        fields["up_to_date"] = "yes"
    emit(**fields)
    return 0


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
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    source_dir = repo_root()
    os.chdir(source_dir)
    if args.stage == "preflight":
        return stage_preflight(source_dir, args.profile)
    if args.stage == "configure":
        return stage_configure(source_dir, args.profile)
    return stage_build(source_dir, args.profile, args.target)


if __name__ == "__main__":
    sys.exit(main())
