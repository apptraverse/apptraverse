# WebAssembly WindowCatalog

WindowCatalog uses the shared `WebApplicationHost`, model session, publication
buffer, UI mirror and DOM presenters. The browser shell forwards native input
and viewport measurements to model Events. The model owns the active page,
localized text and control geometry. List commands capture holder IDs from the
displayed mirror; Wasm-to-JavaScript object IDs retain their unsigned 32-bit
identity.

## State and lifecycle

USER is one ordinary `RamDomainStorage` over the embedded BACKING. Add, child
Close, reopen, Delete, locale changes, paging and resizing do not write files.
The browser lifecycle boundary is `visibilitychange` to hidden, with `pagehide`
as a fallback. Accepted model commands finish before the model serializes its
current state into the same binary `user.bin` representation. The main browser
thread queues that file and its parent directory metadata in one transaction
against the already-open IDBFS database, and calls `commit()` before returning
from the lifecycle event. Returning to the visible page keeps the live model.
There are no object-layer files, checkpoint sidecars or gesture saves.

**Close application** queues this snapshot before requesting model shutdown,
so immediately closing/reloading the tab does not depend on the later worker
teardown callback. While the page remains alive, the existing asynchronous
shutdown path also saves, unloads the UI and awaits IDBFS completion before
displaying **Stopped**. A lifecycle commit request is not a synchronous durability
acknowledgement; crashes and termination without a lifecycle event remain outside
this boundary.

Normal Debug starts with fresh USER over the build-produced initial BACKING.
It never deserializes earlier USER; its shutdown file is
`/persistent/window_catalog_web_state/debug-run-<id>/user/user.bin`. Previous
profiles are left unchanged. The shell accepts a `state-dir` URL query for a
different profile parent. IDBFS is initially populated for both variants so a
fresh Debug run can preserve the earlier browser files.

Use **`web_window_catalog_demo_load_only.html`** for manual close/reopen checks.
It is a Debug consumer for cold-restart acceptance, reads `user.bin` from the
exact `state-dir` query directory (default `/persistent/window_catalog_web_state`) and
uses the same embedded BACKING as the normal Debug application. The separate
build-host producer runs under Node; builder and distillation definitions are
absent from both runtime application translation units. Non-Debug builds consume
the header prepared by Debug and do not build the producer. Non-Debug browser
packaging was not run in this port acceptance.

## Incremental build

The verified Windows toolchain is Emscripten 3.1.64 with its explicit Node
24.19.0 executable. Run from the repository root in an Emsdk environment; avoid
an unrelated Node executable earlier in `PATH`.

```powershell
cmake -S . -B build/wasm-ninja-debug -G Ninja `
  -DCMAKE_BUILD_TYPE=Debug `
  -DCMAKE_TOOLCHAIN_FILE=C:/Users/nickc/emsdk/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake `
  -DCMAKE_CROSSCOMPILING_EMULATOR=C:/Users/nickc/emsdk/node/24.19.0_64bit/node.exe `
  -DAPPTRAVERSE_BUILD_AETHER_DEMOS=OFF -DAPPTRAVERSE_BUILD_LINUX_GUI=OFF `
  -DBUILD_TESTING=OFF

cmake --build build/wasm-ninja-debug --parallel 6 --target `
  web_window_catalog_demo web_window_catalog_demo_load_only `
  apptraverse_window_catalog_web_embedded_session_test

& C:/Users/nickc/emsdk/node/24.19.0_64bit/node.exe `
  build/wasm-ninja-debug/examples/surfaces_demo/web/apptraverse_window_catalog_web_embedded_session_test.js
```

Change the toolchain and Node paths for another SDK installation. The verification
run additionally passed `-C .artifacts/android-dependency-sources.cmake` to reuse
existing dependency sources with the verified 27-patch Æther chain. That local
initial-cache file is not a required repository artifact. `AE_NO_DEBUG_LOG=ON`
was used; Debug assertions remained enabled.

## Browser acceptance

Serve with COOP/COEP headers; the pthread model worker requires cross-origin
isolation and `SharedArrayBuffer`. The repository server supplies those headers.

```powershell
python tools/serve_wasm.py build/wasm-ninja-debug/examples/surfaces_demo/web --port 8785

# Run in a second terminal. Uses installed Chrome through Playwright.
python tools/wasm_window_catalog_browser_smoke.py `
  --url http://127.0.0.1:8785/web_window_catalog_demo.html `
  --screenshot .artifacts/wasm-port-browser.png

# Real tab/window lifecycle, using an isolated persistent Chrome profile.
python tools/wasm_window_catalog_browser_lifecycle_smoke.py `
  --url http://127.0.0.1:8785/web_window_catalog_demo_load_only.html
```

The test uses a fresh browser context, native buttons/list input and real Chrome
touch swipes. It checks Add, paging, Close, reopen, Delete, RU/EN localization,
portrait/landscape model-derived Close geometry, and unsigned DOM object IDs.
During foreground working actions the persistent file tree must be unchanged. Shutdown
must create exactly one nonempty `user.bin` per profile. Two cold consumer
navigations verify restored children, locale and subsequent saving; a second
normal Debug launch must start empty and preserve the old file bytes and mtime.
There is no optimistic active-page mutation in the shell.

The separate lifecycle runner uses Playwright 1.62's `no_defaults` CDP attachment
and a private persistent Chrome profile. Its full window-close case currently
runs on Windows; `--chrome-executable` selects another installed Chrome path.
It checks real tab close/reload/navigation, hidden/visible transitions and native
browser-window close followed by a new browser process. Do not substitute CDP
`Browser.close` for that final action: the verified Chrome build skipped page
lifecycle callbacks through that automation API.

Verified on 2026-11-10: all three Debug build targets, Node embedded session test
and the complete headless Chrome acceptance above passed. The final artifact
passed in Chrome 157.0.8103.39 (9.830 seconds for the browser runner); the Node
test passed in 0.876 seconds. `llvm-nm -C` inspection of the normal Debug runtime
Wasm found no `BuildWebWindowCatalogGraph`, `BuildWindowCatalogGraph` or
`DistillRoot` symbols; this is Debug artifact evidence, not a Release size claim.
The Node test loads
the saved file into a fresh RAM storage/domain and checks stable IDs, closed/open
lazy state and exclusion of immutable localization bodies from USER. Other web
examples, release size and other browsers are not covered by this acceptance.
The historical 2026-11-10 result checked only explicit in-page shutdown.
[The 2026-11-11 follow-up](../../../docs/validation/window-catalog-mobile-web-lifecycle-2026-11-11.md)
records actual lifecycle acceptance (10.937 s), the functional rerun (8.005 s),
shared regressions and limits.
