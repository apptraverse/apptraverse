#!/usr/bin/env python3
"""Create the App Traverse MCP venv and register the User-level Cursor MCP config."""

from __future__ import annotations

import json
import os
import subprocess
import sys
import venv
from pathlib import Path

VENV_DIRNAME = ".venv-apptraverse-mcp"
SERVER_KEY = "apptraverse"
REQUIREMENTS_REL = Path("tools") / "mcp" / "requirements.txt"
SERVER_REL = Path("tools") / "mcp" / "apptraverse_mcp.py"


def repo_root() -> Path:
    return Path(__file__).resolve().parents[2]


def user_mcp_config_path(home: Path | None = None) -> Path:
    base = home if home is not None else Path.home()
    return base / ".cursor" / "mcp.json"


def venv_python(root: Path) -> Path:
    venv_dir = root / VENV_DIRNAME
    if os.name == "nt":
        return venv_dir / "Scripts" / "python.exe"
    return venv_dir / "bin" / "python"


def ensure_venv(root: Path) -> Path:
    venv_dir = root / VENV_DIRNAME
    python = venv_python(root)
    if not python.is_file():
        venv.create(venv_dir, with_pip=True)
    if not python.is_file():
        raise SystemExit(f"venv python missing: {python}")
    return python


def pip_install(python: Path, requirements: Path) -> None:
    subprocess.run(
        [str(python), "-m", "pip", "install", "-r", str(requirements)],
        check=True,
        shell=False,
    )


def verify_mcp_sdk(python: Path) -> None:
    probe = (
        "import mcp\n"
        "from mcp.server import MCPServer\n"
        "print('mcp_version', getattr(mcp, '__version__', 'unknown'))\n"
        "print('MCPServer', MCPServer)\n"
        "print('has_run', hasattr(MCPServer, 'run'))\n"
        "print('has_tool', hasattr(MCPServer, 'tool'))\n"
    )
    subprocess.run([str(python), "-c", probe], check=True, shell=False)


def load_json_object(path: Path) -> dict:
    if not path.is_file():
        return {}
    try:
        payload = json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError:
        return {}
    return payload if isinstance(payload, dict) else {}


def apptraverse_server_entry(command: Path, server_script: Path) -> dict:
    return {
        "type": "stdio",
        "command": str(command.resolve()),
        "args": [str(server_script.resolve())],
    }


def merge_user_mcp_config(
    root: Path,
    command: Path,
    server_script: Path,
    config_path: Path | None = None,
) -> Path:
    path = config_path or user_mcp_config_path()
    path.parent.mkdir(parents=True, exist_ok=True)
    data = load_json_object(path)
    servers = data.get("mcpServers")
    if not isinstance(servers, dict):
        servers = {}
        data["mcpServers"] = servers
    servers[SERVER_KEY] = apptraverse_server_entry(command, server_script)
    path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
    return path


def main() -> int:
    root = repo_root()
    requirements = root / REQUIREMENTS_REL
    server_script = root / SERVER_REL
    if not requirements.is_file():
        raise SystemExit(f"missing {requirements}")
    python = ensure_venv(root)
    pip_install(python, requirements)
    verify_mcp_sdk(python)
    config_path = merge_user_mcp_config(root, python, server_script)
    sys.stderr.write(f"venv_python={python}\n")
    sys.stderr.write(f"user_mcp_config={config_path}\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
