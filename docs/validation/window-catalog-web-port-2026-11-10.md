# WindowCatalog WebAssembly acceptance — 2026-11-10

Base shared/mobile work: `06bd94f`, branch `feature/domain-hierarchy-cloning`.
Scope: WindowCatalog's Emscripten build, Node embedded-session test and real
Chrome browser acceptance. Other Web examples, Apple branches and release
packaging were not modified or tested.

## Port contract

The browser uses the existing WebApplicationHost, model worker, publication
buffer, UI mirror and thin DOM presenters. List callbacks capture stable holder
IDs before enqueueing commands. Swipes, keyboard paging and page buttons send
model intent; visibility changes only after publication. Actual viewport sizes
reach model Events. Child Close layout uses the shared model implementation
verified during Android work, without CSS overriding its rectangle.

USER is RamDomainStorage over immutable embedded BACKING. Working actions,
child Close, locale changes and browser visibility/pagehide notifications do
not save files. The explicit **Close application** action drains/stops the
model, serializes one user.bin and awaits IDBFS completion before reporting
Stopped and releasing the worker. This is an orderly in-page shutdown contract;
closing/killing a browser tab is not a guaranteed asynchronous save boundary.

Normal Debug starts with empty USER in a new debug-run profile and leaves
previous files unchanged. The separate Debug load-only executable restores
USER over the same generated BACKING. The Debug Node producer owns distillation
and generates one supply consumed by both executables. Runtime registration
and graph construction are separate objects; runtime targets have no
distillation definition or builder source. Non-Debug configuration consumes
the Debug-produced supply, but a non-Debug build was not run in this iteration.

## Browser defect caught by execution

The first Add failed to display its child because a uint32 ObjId crossed the
Wasm i32 boundary into JavaScript as a negative number. The DOM tag contained
`-958260728`, while the model ring contained `3336706568`. Looking up the active
page consequently returned null. The DOM tag now converts the incoming value
to unsigned uint32, matching the model's identity. No empty-title or missing-page
fallback was restored. Browser acceptance checks positive unsigned DOM IDs.

## Executed checks

Environment: Windows agent; installed Emscripten 3.1.64; SDK Node 24.19.0;
Chrome 157.0.8103.39 through Playwright. Debug assertions enabled and
AE_NO_DEBUG_LOG=ON. Existing verified dependency sources were reused. After the
first configure, builds were incremental; no other worktree/cache was modified.

| Check | Result |
|---|---|
| Debug normal and load-only browser executables | Build PASS |
| Debug Node supply producer | Build/export PASS |
| Final Node embedded-session test | PASS, 0.876 s |
| Final Chrome browser acceptance | PASS, 9.830 s |
| Runtime Wasm symbol inspection | No BuildWebWindowCatalogGraph, BuildWindowCatalogGraph or DistillRoot |
| Python syntax and scoped diff whitespace | PASS |

The browser acceptance runs against actual served Wasm with COOP/COEP headers.
It verifies Add, touch swipes in both directions, page navigation, child Close,
reopen, selection, Delete, stable row activation, EN/RU on catalog and child,
portrait/landscape model layout, no working USER writes, exactly one nonempty
user.bin per saved profile, two cold load-only restarts and a second fresh Debug
launch preserving the earlier profile's bytes/timestamp. It also verifies that
all mounted pages are removed after orderly shutdown and reports page errors.

The Node test loads saved USER into fresh storage/domain state with the same
BACKING and checks lazy topology, stable IDs and exclusion of immutable supply
bodies. This is independent of the browser's IndexedDB cold-restart test.

Shared changes were already covered by the Android checkpoint: Linux 15/15;
Windows 24/25 batch plus the remaining native focus test passing unchanged on
focused rerun; Android 5/5 per Debug variant. This Web checkpoint changes no
shared model, Windows, Linux or Android sources. Its only generic Web presenter
change removes the title/locale fallback forbidden by the distilled graph
contract. No claim is made about other browsers, mobile-browser devices,
Release, arbitrary abrupt termination or production deployment.

## Reproduction and evidence

[Web README](../../examples/surfaces_demo/web/README.md) gives build, server,
Node and browser commands. Build directory: `build/wasm-ninja-debug`.

Local evidence under `.artifacts/`:

- `wasm-port-configure.log`, `wasm-port-build.log`
- `wasm-port-node-test.log`, `wasm-port-browser.log`
- `wasm-port-browser.png`, `wasm-port-browser.failed.png`
- `wasm-port-runtime-symbols.txt`

The local server was left running at
`http://127.0.0.1:8785/web_window_catalog_demo.html` for manual inspection.
