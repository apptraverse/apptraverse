#!/usr/bin/env python3
"""Summarize MSVC /MAP symbol sizes: by .obj, by category, top symbols."""

from __future__ import annotations

import argparse
import re
from collections import defaultdict

from map_function_sizes import parse_map


def norm_mod(module: str) -> str:
    if ":" in module:
        module = module.split(":", 1)[1]
    return module.replace(".obj", "")


def bucket_name(name: str) -> str:
    if name.startswith("??$CaptureBaseStateInto"):
        return "reflection: CaptureBaseStateInto<T>"
    if name.startswith("??$TryInsertSharedImpl"):
        return "reflection: TryInsertSharedImpl<T>"
    if name.startswith("??$HandlePointerSeri"):
        return "serialization: HandlePointerSeri<T>"
    if name.startswith("??$?R") and "Node@" in name:
        return "graph: lambda walk (templates)"
    if name.startswith("??$Load@"):
        return "serialization: LoadVersion<T>"
    if name.startswith("??$Save@"):
        return "serialization: SaveVersion<T>"
    if "object_serialization" in name or "SerializeObject" in name:
        return "serialization (named API)"
    if "DirectoryDomainStorage" in name or "__std_fs" in name or "filesystem" in name:
        return "filesystem / DirectoryDomainStorage"
    if "WinListContainerPresenter" in name or "WinPushButtonPresenter" in name:
        return "Win32 presenters"
    if "Presenter" in name and ("OnLoad" in name or "SyncFromMirror" in name):
        return "presenters (OnLoad / SyncFromMirror)"
    if "WindowCatalogModelSession" in name and "Run" in name:
        return "WindowCatalogModelSession::Run"
    if "WindowCatalogWinApp" in name and "Run" in name:
        return "WindowCatalogWinApp::Run"
    if "WindowCatalog" in name:
        return "WindowCatalog (other)"
    if name.startswith("??0") or name.startswith("??1"):
        return "ctors/dtors"
    if "MSVCRT" in name or "msvcprt" in name:
        return "CRT / STL runtime"
    return "other"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("map_file")
    parser.add_argument("--top-symbols", type=int, default=40)
    parser.add_argument("--top-modules", type=int, default=30)
    args = parser.parse_args()

    symbols = parse_map(args.map_file)
    total = sum(s.size for s in symbols)

    by_mod: dict[str, int] = defaultdict(int)
    by_bucket: dict[str, int] = defaultdict(int)
    for sym in symbols:
        by_mod[norm_mod(sym.module)] += sym.size
        by_bucket[bucket_name(sym.name)] += sym.size

    print(f"Symbols: {len(symbols)}  |  .text (gap sum): {total:,} B ({total/1024:.1f} KiB)")
    print()
    print(f"--- By translation unit (top {args.top_modules}) ---")
    print(f"{'bytes':>10}  {'%':>6}  module")
    for mod, sz in sorted(by_mod.items(), key=lambda x: -x[1])[: args.top_modules]:
        print(f"{sz:10,}  {100*sz/total:6.1f}  {mod}")

    print()
    print("--- By category (heuristic on mangled names) ---")
    print(f"{'bytes':>10}  {'%':>6}  category")
    for b, sz in sorted(by_bucket.items(), key=lambda x: -x[1]):
        print(f"{sz:10,}  {100*sz/total:6.1f}  {b}")

    print()
    print(f"--- Top {args.top_symbols} symbols ---")
    symbols.sort(key=lambda s: s.size, reverse=True)
    for sym in symbols[: args.top_symbols]:
        mod = norm_mod(sym.module)
        name = sym.name if len(sym.name) <= 90 else sym.name[:87] + "..."
        print(f"{sym.size:6}  {mod:<38}  {name}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
