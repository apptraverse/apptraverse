"""APPTRAVERSE_VERBOSE_LOG policy. Independent of APPTRAVERSE_RUNTIME_JSONL."""

from __future__ import annotations

import os
from typing import Mapping, Optional

TRUE_VALUES = {"1", "true", "yes", "on"}


def env_flag_enabled(name: str, env: Optional[Mapping[str, str]] = None) -> bool:
    source = os.environ if env is None else env
    raw = source.get(name)
    if raw is None:
        return False
    return raw.strip().lower() in TRUE_VALUES


def verbose_log_enabled(env: Optional[Mapping[str, str]] = None) -> bool:
    return env_flag_enabled("APPTRAVERSE_VERBOSE_LOG", env)


def runtime_jsonl_enabled(env: Optional[Mapping[str, str]] = None) -> bool:
    source = os.environ if env is None else env
    raw = source.get("APPTRAVERSE_RUNTIME_JSONL")
    return bool(raw)
