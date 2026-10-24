#!/usr/bin/env python3
"""Estimate per-symbol .text sizes from an MSVC linker /MAP file.

Sorts public code symbols in section 0001 by RVA and uses gaps to the next
symbol as an approximate function size (same idea as bloaty -d symbols).
"""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass


@dataclass
class Symbol:
    rva: int
    name: str
    module: str
    size: int = 0


LINE_RE = re.compile(
    r"^\s+0001:(?P<secoff>[0-9a-fA-F]+)\s+(?P<name>\S+)\s+"
    r"(?P<rva>[0-9a-fA-F]+)\s+[fi]\s+(?P<module>.+?)\s*$"
)


def parse_map(path: str) -> list[Symbol]:
    symbols: list[Symbol] = []
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            m = LINE_RE.match(line)
            if not m:
                continue
            name = m.group("name")
            if name.startswith("$"):
                continue
            if name in {"__AbsoluteZero", "__safe_se_handler_count"}:
                continue
            module = m.group("module").strip()
            if module.startswith("*"):
                continue
            rva = int(m.group("rva"), 16)
            symbols.append(Symbol(rva=rva, name=name, module=module))
    symbols.sort(key=lambda s: s.rva)
    for i, sym in enumerate(symbols):
        if i + 1 < len(symbols):
            sym.size = max(0, symbols[i + 1].rva - sym.rva)
        else:
            sym.size = 0
    return [s for s in symbols if s.size > 0]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("map_file")
    parser.add_argument("-n", "--top", type=int, default=120, help="Top N symbols")
    parser.add_argument(
        "--min-size", type=int, default=8, help="Minimum estimated size (bytes)"
    )
    args = parser.parse_args()

    symbols = parse_map(args.map_file)
    symbols = [s for s in symbols if s.size >= args.min_size]
    symbols.sort(key=lambda s: s.size, reverse=True)

    total = sum(s.size for s in symbols)
    print(f"Estimated .text from {len(symbols)} symbols (total ~{total} bytes)")
    print(f"{'size':>8}  {'module':<48}  symbol")
    for sym in symbols[: args.top]:
        mod = sym.module if len(sym.module) <= 48 else "..." + sym.module[-45:]
        name = sym.name
        if len(name) > 96:
            name = name[:93] + "..."
        print(f"{sym.size:8}  {mod:<48}  {name}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
