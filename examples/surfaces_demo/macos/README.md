# macOS WindowCatalog

Apple branch: `feature/apple-window-catalog`, based on
`feature/domain-hierarchy-cloning` at `b0c808d81b187b3fa890b0a59d25e5a7bf23c66a`.
The shared model, domain/clone/storage mechanisms and other backends are not
forked or copied into this port.

## Build and run

Requires macOS 13+, Xcode with its macOS SDK and Swift compiler, Ninja, CMake,
and a C++20 compiler. The tested toolchain uses MacPorts clang 20 for C/C++ and
Objective-C++, and Xcode Swift. From the repository root:

```sh
scripts/build_macos_window_catalog.sh
open build/macos-debug/examples/surfaces_demo/macos/macos_window_catalog_demo.app
```

The script builds only WindowCatalog, its native acceptance/lifecycle checks,
and the named common regression checks. It preserves the build/cache and uses
Debug. `CC`, `CXX`, `CMAKE`, `CMAKE_BUILD_PARALLEL_LEVEL` and the first positional
build-directory argument can override the defaults.

The normal Debug application creates a fresh graph on every run and prints its
fresh profile directory. It does not resume a previous USER. The default parent
directory is `~/Library/Application Support/AppTraverse/WindowCatalog`; pass
`--state-dir /absolute/path` to the executable inside the app bundle to select
another parent. Child Close stores into RAM; application Close or Cmd-Q drains
accepted work and writes one `user.bin`.

`macos_window_catalog_demo_load_only` is the explicit cold-load consumer of the
embedded supply prepared by the Debug application's `--distill-only` mode.
Start it with an empty `--state-dir`, close it, then restart with the same path
to resume its USER. Do not mix USER generated against a different distilled
supply. Application S0 profiles are experimental and are not migrated.

## Architecture and native behavior

The common `WindowCatalogPresenterFactory` builds the graph. The two catalog
presenters add catalog-specific list intents and main-viewport reporting to the
generic macOS presenters. The shared `WindowCatalogModelSession` owns the model
runtime, publication sidecars, RAM USER and shutdown persistence.

SwiftUI renders the mirror using model-provided rectangles: the same catalog
list, four rectangular buttons, localized labels and adaptive layout as Windows.
AppKit supplies independent titled, resizable windows, window placement, native
Close, Cmd-W/Cmd-Q and stacking. Row double-click opens or focuses a child.
Native handles and adapters remain transient. Model changes never originate by
editing the UI mirror.

Desktop coordinates use the primary screen's upper-left origin and signed
positions; dimensions are client dimensions in logical points. Native work-area
and frame observations travel through Events. Ordinary publications update text
and controls without reapplying stale geometry; only explicit model frame
requests move/resize a live window. Close, reopening and deletion retain their
shared-model semantics.

The native acceptance harness uses the real host, bridge, presenters, proxy,
model thread and publications. It checks Debug startup separately from an
embedded seed/cold process pair, and writes PNGs of its own window contents to
the build's `macos-acceptance` directory. This does not require screen-recording
or Accessibility permission. It is distinct from external mouse/keyboard UI
automation, which needs a compatible Computer Use service.

The iPhone port is deferred by the current user instruction; existing iOS
sources are not claimed to be verified by the macOS work.

## MinSizeRel size policy

`APPTRAVERSE_MACOS_MIN_SIZE_OPTIMIZATIONS` defaults to `ON` for macOS
`MinSizeRel` only. The shared `cmake/apple_min_size.cmake` policy applies `-Oz`,
ThinLTO, hidden symbols, function/data sections, no C++ exceptions or RTTI,
no Objective-C exceptions, linker dead stripping and post-link `strip -x`.
Swift retains `-Osize` whole-module optimization and its platform ABI.
The bitcode archiver and ranlib are selected from the active Clang toolchain;
this also supports MacPorts LLVM newer than Xcode's archive tools.
Debug and other platforms are unaffected. The iOS option remains independent.

Use a Debug-distilled supply from the matching model/schema revision with
`APPTRAVERSE_WINDOW_CATALOG_MACOS_DEBUG_SUPPLY_HEADER`. Preserve older
experimental profiles and select a separate `--state-dir` when validating a
new supply. See the macOS MinSizeRel validation report dated 2026-11-11.
