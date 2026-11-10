#!/usr/bin/env python3
"""Apply the ordered Aether patches to an isolated archive of the pinned commit.

Example (offline, no dependency-cache or build-tree changes):
  python cmake/scripts/verify-aether-patch-chain.py \
    --source-git-dir /path/to/existing/aether-objects/.git

Evidence, the pristine archive, and the patched tree remain under .artifacts.
Each extracted tree gets its own Git repository, including when the evidence
directory is nested inside an existing repository or ignored directory.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tarfile


def digest(path: Path) -> str | None:
    return hashlib.sha256(path.read_bytes()).hexdigest() if path.is_file() else None


def patch_paths(path: Path, tree: Path) -> list[str]:
    paths = set()
    for line in path.read_text(encoding="utf-8-sig").splitlines():
        match = re.match(r"^(?:---|\+\+\+) [ab]/([^\t]+)", line)
        if match:
            relative = match.group(1)
            if not (tree / relative).resolve().is_relative_to(tree):
                raise RuntimeError(f"Patch path escapes destination: {relative}")
            paths.add(relative)
    if not paths:
        raise RuntimeError(f"No source paths found in {path.name}")
    return sorted(paths)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--source-git-dir", type=Path, required=True)
    parser.add_argument("--output-root", type=Path)
    parser.add_argument("--apply-tool", choices=("gnu-patch", "git"), default="git")
    args = parser.parse_args()
    root = args.source_root.resolve()
    parent = (args.output_root or root / ".artifacts" / "patch-chain-verification").resolve()
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")
    out = parent / stamp
    out.mkdir(parents=True, exist_ok=False)
    tree = out / "tree"
    tree.mkdir()
    snapshots = out / "patches"
    snapshots.mkdir()
    apply_inputs = out / "apply-patches"
    apply_inputs.mkdir()
    metadata = {
        "started_at_utc": stamp,
        "source_root": str(root),
        "source_git_dir": str(args.source_git_dir.resolve()),
        "apply_tool": args.apply_tool,
        "tree": str(tree),
        "commands": [],
        "patches": [],
        "result": "RUNNING",
    }
    logfile = out / "verification.log"
    # Do not inherit an enclosing repository/index or caller's Git config.
    # These are child-process settings only; no user configuration is changed.
    env = {key: value for key, value in os.environ.items() if not key.upper().startswith("GIT_")}
    env.update(GIT_CONFIG_GLOBAL=os.devnull, GIT_CONFIG_SYSTEM=os.devnull)

    def run(argv: list[str], cwd: Path | None = None) -> str:
        proc = subprocess.run(
            argv, cwd=cwd, env=env, stdout=subprocess.PIPE,
            stderr=subprocess.PIPE, text=True, encoding="utf-8",
            errors="replace", check=False,
        )
        record = {"argv": argv, "cwd": str(cwd) if cwd else None, "exit_code": proc.returncode}
        metadata["commands"].append(record)
        with logfile.open("a", encoding="utf-8", newline="\n") as log:
            log.write(json.dumps(record) + "\n" + proc.stdout + proc.stderr + "\n")
        if proc.returncode:
            raise RuntimeError(f"Command failed ({proc.returncode}): {argv}\n{proc.stderr}")
        return proc.stdout

    try:
        config_path = root / "cmake" / "aether_version.cmake"
        config_bytes = config_path.read_bytes()
        (out / "aether_version.cmake").write_bytes(config_bytes)
        config = config_bytes.decode("utf-8-sig")
        pin_match = re.search(r'set\(APPTRAVERSE_AETHER_OBJECTS_GIT_TAG\s+"([0-9a-f]{40})"\)', config)
        names_match = re.search(r"set\(APPTRAVERSE_AETHER_OBJECTS_PATCH_NAMES\s+(.*?)\)", config, re.S)
        if not pin_match or not names_match:
            raise RuntimeError("Pinned commit or ordered patch list is missing")
        pin = pin_match.group(1)
        names = [line.strip() for line in names_match.group(1).splitlines()
                 if line.strip() and not line.lstrip().startswith("#")]
        if not names or len(set(names)) != len(names):
            raise RuntimeError("Patch list is empty or contains duplicate names")
        metadata.update(pin=pin, patch_count=len(names),
                        source_config_sha256=hashlib.sha256(config_bytes).hexdigest())
        for name in names:
            src = root / "cmake" / "patches" / f"aether-objects-{name}.patch"
            snap = snapshots / src.name
            shutil.copyfile(src, snap)
            # Windows worktrees may check patches out as CRLF. GNU patch strips
            # these CRs; feed native git apply the equivalent LF patch bytes.
            normalized = apply_inputs / src.name
            normalized.write_bytes(snap.read_bytes().replace(b"\r\n", b"\n"))
            metadata["patches"].append({"name": name, "sha256": digest(snap),
                                        "apply_sha256": digest(normalized)})

        git = shutil.which("git")
        if not git:
            raise RuntimeError("git unavailable")
        source_git = [git, f"--git-dir={args.source_git_dir.resolve()}"]
        if run([*source_git, "cat-file", "-t", pin]).strip() != "commit":
            raise RuntimeError("Pinned object is not a commit")
        archive = out / "pristine.tar"
        run([*source_git, "archive", "--format=tar", f"--output={archive}", pin])
        with tarfile.open(archive) as tf:
            for entry in tf.getmembers():
                if not (tree / entry.name).resolve().is_relative_to(tree):
                    raise RuntimeError(f"Archive path escapes destination: {entry.name}")
            tf.extractall(tree)

        # Never let git apply discover an ancestor repository. The index records
        # the pristine archive; the final tree diff proves actual source changes.
        run([git, "init", "--quiet", str(tree)])
        local_git = [git, "-c", "core.autocrlf=false", "-c", "core.safecrlf=false",
                     f"--git-dir={tree / '.git'}", f"--work-tree={tree}"]
        toplevel = Path(run([*local_git, "rev-parse", "--show-toplevel"], tree).strip()).resolve()
        if toplevel != tree:
            raise RuntimeError(f"Wrong temporary repository root: {toplevel}")
        run([*local_git, "add", "--all"], tree)
        baseline = run([*local_git, "write-tree"], tree).strip()
        metadata["baseline_tree"] = baseline
        metadata["isolated_git_root"] = str(toplevel)

        patch = shutil.which("patch")
        if not patch and sys.platform == "win32":
            candidate = Path(r"C:\Program Files\Git\usr\bin\patch.exe")
            patch = str(candidate) if candidate.is_file() else None
        if args.apply_tool == "gnu-patch" and not patch:
            raise RuntimeError("GNU patch executable unavailable")
        for record in metadata["patches"]:
            name = record["name"]
            patch_path = apply_inputs / f"aether-objects-{name}.patch"
            paths = patch_paths(patch_path, tree)
            before = {relative: digest(tree / relative) for relative in paths}
            if args.apply_tool == "gnu-patch":
                run([patch, "-p1", "--batch", "--forward", "--input", str(patch_path)], tree)
            else:
                run([*local_git, "apply", "--verbose", "--whitespace=nowarn", str(patch_path)], tree)
            after = {relative: digest(tree / relative) for relative in paths}
            unchanged = [relative for relative in paths if before[relative] == after[relative]]
            record.update(files=paths, before_sha256=before, after_sha256=after)
            if unchanged:
                raise RuntimeError(f"Patch {name} did not change its expected files: {unchanged}")
            print(f"applied {name}: {len(paths)} changed files", flush=True)

        run([*local_git, "add", "--all"], tree)
        final_tree = run([*local_git, "write-tree"], tree).strip()
        metadata["final_tree"] = final_tree
        diff = run([*local_git, "diff", "--no-ext-diff", "--binary", baseline, final_tree, "--"], tree)
        (out / "applied-chain.diff").write_text(diff, encoding="utf-8", newline="\n")
        changed = run([*local_git, "diff", "--name-only", baseline, final_tree, "--"], tree).splitlines()
        metadata["changed_files"] = changed
        objdir = tree / "src" / "aether-objects" / "obj"
        domain = (objdir / "domain.cpp").read_text(encoding="utf-8")
        header = (objdir / "domain.h").read_text(encoding="utf-8")
        objptr = (objdir / "obj_ptr_base.cpp").read_text(encoding="utf-8")
        rejects = [str(path.relative_to(tree)) for path in tree.rglob("*.rej")]
        invariants = {
            "isolated_git_root": toplevel == tree,
            "tracked_diff_present": bool(diff and changed and baseline != final_tree),
            "every_patch_changed_expected_files": all(
                all(item["before_sha256"][path] != item["after_sha256"][path]
                    for path in item["files"]) for item in metadata["patches"]),
            "cycle_detector_remove_present": "cycle_detector.Remove(obj_id);" in domain,
            "pending_stored_layers_absent": "pending_stored_layers" not in domain,
            "no_patch_rejects": not rejects,
            "source_config_unchanged": config_path.read_bytes() == config_bytes,
            "source_patches_unchanged": all(
                digest(root / "cmake" / "patches" / f"aether-objects-{item['name']}.patch") == item["sha256"]
                for item in metadata["patches"]),
        }
        if "scoped-layer-hydration-v1" in names:
            invariants.update(
                scoped_load_storage_api="void SetLoadStorage(IDomainStorage* storage) { load_storage_ = storage; }" in header,
                scoped_storage_default="IDomainStorage* load_storage_{nullptr};" in header,
                scoped_retention_default="bool retain_loaded_references{false};" in header,
                scoped_storage_exclusive=bool(re.search(
                    r"DomainLoad DomainGraph::GetReader\([^)]*\)\s*\{\s*if \(load_storage_ != nullptr\) \{\s*//[^\n]*\n\s*return load_storage_->Load\(query\);\s*\}", domain)),
                scoped_reference_retention=bool(re.search(
                    r"else if \(meta.value.is_valid\(\) &&\s*archive.buffer\(\).domain_graph->retain_loaded_references\) \{\s*(?://[^\n]*\n\s*)*meta.value.cached_ = meta.value.domain_->Find\(meta.value.id_\);", objptr)),
            )
        metadata.update(invariants=invariants, rejects=rejects)
        if not all(invariants.values()):
            raise RuntimeError(f"Verification invariants failed: {[key for key, value in invariants.items() if not value]}")
        metadata["result"] = "PASS"
        print(f"full patch chain OK ({len(names)} patches, pin {pin}, {len(changed)} changed files)", flush=True)
        return 0
    except Exception as exc:
        metadata.update(result="FAIL", error=str(exc))
        print(str(exc), file=sys.stderr, flush=True)
        return 1
    finally:
        metadata["finished_at_utc"] = datetime.now(timezone.utc).isoformat()
        (out / "result.json").write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")
        print(f"Evidence: {out}", flush=True)


if __name__ == "__main__":
    raise SystemExit(main())
