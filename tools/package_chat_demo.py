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

    sha = git_sha(ROOT)
    out_dir = args.out_root / sha
    out_dir.mkdir(parents=True, exist_ok=True)

    manifest: dict = {
        "repo_sha": sha,
        "packaged_at": datetime.now(timezone.utc).isoformat(),
        "host": platform.platform(),
        "artifacts": [],
        "platforms": {},
        "dependency_pins": read_pins(),
    }

    win_exe = args.build_dir / "examples" / "chat_demo" / "windows" / "apptraverse_chat.exe"
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
