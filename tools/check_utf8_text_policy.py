#!/usr/bin/env python3
"""Fail if forbidden wide-text tokens appear in shared AppTraverse sources."""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

SCAN_ROOTS = [
    ROOT / "include" / "apptraverse",
    ROOT / "src",
    ROOT / "tests",
]

SKIP_DIR_NAMES = {"build", "third_party", ".git", "windows"}

FORBIDDEN = [
    re.compile(r"\bstd::u16string\b"),
    re.compile(r"\bstd::u16string_view\b"),
    re.compile(r"\bchar16_t\b"),
    re.compile(r"\bstd::wstring\b"),
    re.compile(r"\bstd::wstring_view\b"),
    re.compile(r"\bwchar_t\b"),
    re.compile(r"\bMultiByteToWideChar\b"),
    re.compile(r"\bWideCharToMultiByte\b"),
]

ALLOW_LINE_SUBSTR = [
    "AppendUtf8AsUtf16Le",
    "ReadUtf16LeAsUtf8",
    "UTF-16LE",
]


def should_scan(path: Path) -> bool:
    if path.suffix not in {".h", ".hpp", ".cpp", ".cc", ".cxx"}:
        return False
    for part in path.parts:
        if part in SKIP_DIR_NAMES:
            return False
    return True


def line_allowed(line: str) -> bool:
    return any(token in line for token in ALLOW_LINE_SUBSTR)


def main() -> int:
    violations: list[str] = []
    for root in SCAN_ROOTS:
        if not root.exists():
            continue
        for path in root.rglob("*"):
            if not path.is_file() or not should_scan(path):
                continue
            text = path.read_text(encoding="utf-8", errors="replace")
            for lineno, line in enumerate(text.splitlines(), start=1):
                if line_allowed(line):
                    continue
                for pattern in FORBIDDEN:
                    if pattern.search(line):
                        violations.append(f"{path.relative_to(ROOT)}:{lineno}: {line.strip()}")
                        break
    if violations:
        sys.stderr.write("UTF-8 text policy violations:\n")
        for item in violations:
            sys.stderr.write(f"  {item}\n")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
