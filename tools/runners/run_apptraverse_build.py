#!/usr/bin/env python3
"""Canonical staged build runner. v1 supports win64-ninja-msvc-debug only."""

from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

SUPPORTED_PROFILE = "win64-ninja-msvc-debug"
CANONICAL_BUILD_DIR = Path("build") / "win64-ninja-msvc-debug"

STATUS_OK = "ok"
STATUS_BLOCKED = "blocked"
STATUS_FAILED = "failed"


def repo_root() -> Path:
    return Path(__file__).resolve().parents[2]


def emit(**fields: str) -> None:
    print(" ".join(f"{key}={value}" for key, value in fields.items()))


def cmake_configure_argv(profile: str) -> list[str]:
    return ["cmake", "--preset", profile]


def cmake_build_argv(profile: str, targets: list[str]) -> list[str]:
    argv = ["cmake", "--build", "--preset", profile]
    for target in targets:
        argv.extend(["--target", target])
    return argv


def command_is_destructive(argv: list[str]) -> bool:
    lowered = [part.lower() for part in argv]
    if "--clean-first" in lowered:
        return True
    if "clean" in lowered:
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


def compiler_is_msvc_cl(compiler: str) -> bool:
    lowered = compiler.replace("\\", "/").lower()
    if "mingw" in lowered or "msys64" in lowered or "/ucrt64/" in lowered:
        return False
    name = Path(compiler).name.lower()
    return name in {"cl", "cl.exe"}


def inspect_cache(cache_text: str) -> tuple[str, str]:
    """Return (status, detail). status is ok or conflict reason."""
    generator = parse_cache_generator(cache_text)
    compiler = parse_cache_cxx_compiler(cache_text)
    if generator is None:
        return "conflict", "CMAKE_GENERATOR missing from CMakeCache.txt"
    if generator != "Ninja":
        return "conflict", f"CMAKE_GENERATOR={generator} (expected Ninja)"
    if compiler is None:
        return "conflict", "CMAKE_CXX_COMPILER missing from CMakeCache.txt"
    if not compiler_is_msvc_cl(compiler):
        return "conflict", f"CMAKE_CXX_COMPILER is not MSVC cl: {compiler}"
    return "ok", "ninja+msvc"


def load_configure_preset_names(presets_path: Path) -> list[str]:
    data = json.loads(presets_path.read_text(encoding="utf-8"))
    names: list[str] = []
    for preset in data.get("configurePresets", []):
        name = preset.get("name")
        if name:
            names.append(name)
    return names


def decide_configure_action(
    build_dir: Path,
    cache_text: str | None,
) -> tuple[str, str, str | None]:
    """Return (status, action_or_kind, reason)."""
    if not build_dir.exists():
        return STATUS_OK, "configure", None
    if cache_text is None:
        return STATUS_OK, "configure", None
    cache_status, detail = inspect_cache(cache_text)
    if cache_status == "ok":
        return STATUS_OK, "already_configured", None
    return STATUS_BLOCKED, "build_profile_conflict", detail


def preflight(
    profile: str,
    source_dir: Path,
    *,
    platform: str | None = None,
    which=shutil.which,
) -> tuple[str, str | None, str | None]:
    """Return (status, failure_kind, reason)."""
    if profile != SUPPORTED_PROFILE:
        return STATUS_BLOCKED, "unsupported_profile", profile
    host = platform if platform is not None else sys.platform
    if not host.startswith("win"):
        return STATUS_BLOCKED, "os_not_windows", host
    presets_path = source_dir / "CMakePresets.json"
    if not presets_path.is_file():
        return STATUS_BLOCKED, "preset_missing", "CMakePresets.json not found"
    names = load_configure_preset_names(presets_path)
    if profile not in names:
        return STATUS_BLOCKED, "preset_missing", profile
    if which("cmake") is None:
        return STATUS_BLOCKED, "cmake_missing", "cmake not on PATH"
    if which("ninja") is None:
        return STATUS_BLOCKED, "ninja_missing", "ninja not on PATH"
    if which("cl") is None:
        return STATUS_BLOCKED, "msvc_environment_missing", "cl.exe not on PATH"
    return STATUS_OK, None, None


def run_command(argv: list[str], cwd: Path) -> subprocess.CompletedProcess[str]:
    if command_is_destructive(argv):
        raise ValueError(f"refusing destructive command: {argv}")
    return subprocess.run(
        argv,
        cwd=cwd,
        shell=False,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
    )


def output_tail(proc: subprocess.CompletedProcess[str], limit: int = 40) -> str:
    combined = (proc.stdout or "") + (proc.stderr or "")
    lines = [line for line in combined.splitlines() if line.strip()]
    return "\n".join(lines[-limit:])


def first_error_tail(proc: subprocess.CompletedProcess[str]) -> str:
    combined = (proc.stdout or "") + (proc.stderr or "")
    lines = combined.splitlines()
    useful = [
        line
        for line in lines
        if "error" in line.lower() or "fatal" in line.lower()
    ]
    chosen = useful[-12:] if useful else lines[-12:]
    return "\n".join(chosen)


def ninja_reported_no_work(proc: subprocess.CompletedProcess[str]) -> bool:
    combined = ((proc.stdout or "") + (proc.stderr or "")).lower()
    return "ninja: no work to do" in combined


def stage_preflight(source_dir: Path, profile: str) -> int:
    status, kind, reason = preflight(profile, source_dir)
    if status == STATUS_OK:
        emit(status=STATUS_OK, stage="preflight", profile=profile)
        return 0
    fields = {
        "status": STATUS_BLOCKED,
        "stage": "preflight",
        "failure_kind": kind or "preflight_failed",
    }
    if reason:
        fields["reason"] = reason.replace(" ", "_")
    emit(**fields)
    return 2


def stage_configure(source_dir: Path, profile: str) -> int:
    pre_status, kind, reason = preflight(profile, source_dir)
    if pre_status != STATUS_OK:
        fields = {
            "status": STATUS_BLOCKED,
            "stage": "preflight",
            "failure_kind": kind or "preflight_failed",
        }
        if reason:
            fields["reason"] = reason.replace(" ", "_")
        emit(**fields)
        return 2

    build_dir = source_dir / CANONICAL_BUILD_DIR
    cache_path = build_dir / "CMakeCache.txt"
    cache_text = cache_path.read_text(encoding="utf-8", errors="replace") if cache_path.is_file() else None
    status, action, conflict_reason = decide_configure_action(build_dir, cache_text)
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
    proc = run_command(argv, source_dir)
    if proc.returncode != 0:
        emit(status=STATUS_FAILED, stage="configure", failure_kind="configure_failed")
        print("command=" + " ".join(argv))
        print(first_error_tail(proc))
        return proc.returncode
    emit(status=STATUS_OK, stage="configure", action="configured")
    return 0


def stage_build(source_dir: Path, profile: str, targets: list[str]) -> int:
    if not targets:
        emit(
            status=STATUS_BLOCKED,
            stage="build",
            failure_kind="missing_target",
        )
        return 2
    pre_status, kind, reason = preflight(profile, source_dir)
    if pre_status != STATUS_OK:
        fields = {
            "status": STATUS_BLOCKED,
            "stage": "preflight",
            "failure_kind": kind or "preflight_failed",
        }
        if reason:
            fields["reason"] = reason.replace(" ", "_")
        emit(**fields)
        return 2

    build_dir = source_dir / CANONICAL_BUILD_DIR
    cache_path = build_dir / "CMakeCache.txt"
    if cache_path.is_file():
        cache_text = cache_path.read_text(encoding="utf-8", errors="replace")
        status, action, conflict_reason = decide_configure_action(build_dir, cache_text)
        if status == STATUS_BLOCKED:
            emit(
                status=STATUS_BLOCKED,
                stage="build",
                failure_kind=action,
                reason=(conflict_reason or "profile_mismatch").replace(" ", "_"),
            )
            return 2

    argv = cmake_build_argv(profile, targets)
    proc = run_command(argv, source_dir)
    target_text = ",".join(targets)
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
    if ninja_reported_no_work(proc):
        fields["ninja_no_work"] = "yes"
    emit(**fields)
    return 0


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Canonical App Traverse staged build runner (Windows Ninja/MSVC v1)."
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
