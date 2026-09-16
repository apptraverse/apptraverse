#!/usr/bin/env python3
"""Package chat demo artifacts under dist/chat-demo/<git-sha>/ (git-ignored)."""

from __future__ import annotations

import argparse
import hashlib
import json
import platform
import shutil
import subprocess
import sys
import tempfile
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_BUILD = ROOT / "build" / "win64-ninja-msvc-debug"


def git_sha(cwd: Path) -> str:
    out = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=str(cwd), text=True)
    return out.strip()


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def read_pins() -> dict[str, str]:
    pins: dict[str, str] = {}
    cmake = ROOT / "cmake" / "aether_version.cmake"
    if not cmake.is_file():
        return pins
    for line in cmake.read_text(encoding="utf-8").splitlines():
        if "GIT_TAG" in line and '"' in line:
            key = line.split("set(")[1].split()[0] if "set(" in line else ""
            val = line.split('"')[1] if line.count('"') >= 2 else ""
            if key and val:
                pins[key] = val
    return pins


def main() -> int:
    parser = argparse.ArgumentParser(description="Package chat demo")
    parser.add_argument("--build-dir", type=Path, default=DEFAULT_BUILD)
    parser.add_argument("--out-root", type=Path, default=ROOT / "dist" / "chat-demo")
    args = parser.parse_args()

    packaging_sha = git_sha(ROOT)
    win_exe = args.build_dir / "examples" / "chat_demo" / "windows" / "apptraverse_chat.exe"
    receipt = args.build_dir / "receipts" / "apptraverse_chat.json"

    binary_source_sha = packaging_sha
    if win_exe.is_file():
        with tempfile.TemporaryDirectory() as tmp:
            info_path = Path(tmp) / "build-info.txt"
            proc = subprocess.run(
                [str(win_exe), "--build-info-file", str(info_path)],
                capture_output=True,
                text=True,
                timeout=30,
                check=False,
            )
            if proc.returncode != 0 or not info_path.is_file():
                print(
                    "ERROR: cannot read --build-info from executable; refusing to package unlabeled bytes",
                    file=sys.stderr,
                )
                return 1
            for line in info_path.read_text(encoding="utf-8").splitlines():
                if line.startswith("binary_source_sha="):
                    binary_source_sha = line.split("=", 1)[1].strip()
        if receipt.is_file():
            data = json.loads(receipt.read_text(encoding="utf-8"))
            if data.get("sha256") != sha256_file(win_exe):
                print("ERROR: receipt sha256 mismatches executable; refusing package", file=sys.stderr)
                return 1
            emb = (data.get("embedded") or {}).get("binary_source_sha")
            if emb and emb != binary_source_sha:
                print("ERROR: receipt embedded sha mismatches --build-info", file=sys.stderr)
                return 1
            if (data.get("embedded") or {}).get("source_dirty") == "dirty":
                print("ERROR: dirty compiled source cannot be promoted silently", file=sys.stderr)
                return 1

    out_dir = args.out_root / binary_source_sha
    out_dir.mkdir(parents=True, exist_ok=True)

    manifest: dict = {
        "binary_source_sha": binary_source_sha,
        "packaging_repo_sha": packaging_sha,
        "packaged_at": datetime.now(timezone.utc).isoformat(),
        "host": platform.platform(),
        "artifacts": [],
        "platforms": {},
        "dependency_pins": read_pins(),
    }
    if win_exe.is_file():
        dest = out_dir / "windows" / win_exe.name
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(win_exe, dest)
        digest = sha256_file(dest)
        manifest["artifacts"].append(
            {"path": str(dest.relative_to(out_dir)), "sha256": digest, "bytes": dest.stat().st_size}
        )
        manifest["platforms"]["windows"] = "present"
    else:
        manifest["platforms"]["windows"] = "NOT_RUN (exe not built)"

    manifest["platforms"]["linux"] = "NOT_RUN (packaged on Windows host)"
    manifest["platforms"]["android"] = "NOT_RUN (APK not built on this host)"
    manifest["platforms"]["web"] = "NOT_RUN (WEB_NETWORK_BLOCKED)"

    (out_dir / "LAUNCH.md").write_text(
        """# Two-participant chat demo launch (Windows)

Build (incremental MSVC Debug):

```powershell
powershell -File tools/build_chat_demo.ps1
```

Participant A:

```powershell
& .\\dist\\chat-demo\\<sha>\\windows\\apptraverse_chat.exe `
  --state-dir \"$env:LOCALAPPDATA\\AppTraverseChatDemoA\" `
  --peer-admin-id \"partner\" `
  --peer-aether-uid \"<B-aether-uid>\"
```

Participant B (separate profile):

```powershell
& .\\dist\\chat-demo\\<sha>\\windows\\apptraverse_chat.exe `
  --state-dir \"$env:LOCALAPPDATA\\AppTraverseChatDemoB\" `
  --peer-admin-id \"partner\" `
  --peer-aether-uid \"<A-aether-uid>\"
```

Replace `<sha>` with the git SHA directory name. Supply each peer's Aether UID
explicitly until AeroAdmin resolution contract is available
(`docs/aeroadmin_resolution_missing_contract.md`).

Do not copy real profile directories, keys, or credentials into packages.
""",
        encoding="utf-8",
    )

    (out_dir / "manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    print(f"Packaged to {out_dir}")
    print(json.dumps(manifest["platforms"], indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
