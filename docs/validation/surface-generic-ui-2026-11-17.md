# Legacy Surface generic UI migration — 2026-11-17

The three Windows targets (`win32_window_catalog`, `_v1`, `_load_only`), two
Linux targets (`linux_window_catalog`, `_load_only`) and `web_window_catalog`
now compose an independent Surface S0 application from standard desktop windows,
buttons, locale, layout and device components. They use the generic model/UI
sessions and native application hosts. They are not aliases of the modern
WindowCatalog application. Apple and dormant Android Surface sources are retained.

Each Surface retains Add, Close this window and RU/EN. Content Close removes
only its Surface; native caption X exits desktop applications while preserving
all surviving topology. Events author add/remove/current selection/full order;
native geometry and intrinsic measurements remain local. Initial placement and
orientation use the shared backend and constraint solver. Newly created nodes
inherit the live application's notifier and immediate-compaction policy. Actions
queue their required persistent application owner and window identity, making
already-queued double Remove harmless. Concrete NodeFor capture and live owner
references preserve derived role/target fields through Events and replay.

The v1 GUI remains S0: initial Surface 1 retains its historical blank suffix;
new Surface N receives ` [sN]`. Numbering remains maximum live number plus one.
Normal Debug is fresh. Explicit `--load-only --state-dir <profile>` on a Debug
producer supports matching-supply cold runs, including v1. The separate load-only
executables remain. Default GUI profiles use `surface_generic_s0_state` and
`surface_generic_v1_s0_state`; historical experimental profiles are not migrated.
USER and LOCAL are independent ordinary RAM stores, saved as `user.bin` and
`local/user.bin` at shutdown; Web also captures both at hidden/pagehide. Existing
LOCAL wins over an optional seed. The real OS baseline precedes initial publication.

Initial builders live in producer-only `surface_ui_supply.cpp`. Layout definitions
are immutable supply references; editable labels are ordinary mutable localized
Nodes, so their Event overrides survive cold load. Web shutdown asynchronously
releases the host and external mount handle after the lifecycle barrier.

Executed evidence on this host, incremental Debug with cached dependencies:

- Windows actual executable process smoke PASS, 7.39s: Add/remove, surviving
  HWND identity, RU labels/titles, native resize, native X, separate USER/LOCAL,
  cold placement/full z-order, and v1 cold suffix followed by another Add.
- GTK actual executable smoke PASS, 9.29s under private Xvfb/Openbox: pointer
  clicks located through AT-SPI, surviving XIDs, language, wide/portrait resize,
  native X, cold topology/placement/full z-order and subsequent Add.
- Chrome Surface smoke PASS: real DOM actions/current page, surviving element
  identity, RU, orientation, hidden USER/LOCAL, cold load, shutdown and zero emval
  handles after teardown.
- Final Windows common model probe PASS after the editable-label metadata
  correction: deterministic supplies, queued double Remove, stale identity,
  replay, derived enabled-Event owner/role retention, custom RU label cold restore,
  separate LOCAL and compatible layout default/override/Clear behavior.
- Four revived historical main/dynamic fixtures PASS, and both migrated
  main/dynamic Windows process smokes PASS in the final native selection.
- Existing full current Catalog and MinimalApp GTK and Chrome smokes PASS in
  their historical scopes. Later shared-core changes require aggregate reruns.

The native runs preceded the final editable-label supply-role correction; that
correction was verified by the final common model cold test. Native artifacts
must be incrementally refreshed for aggregate acceptance after the shared-core
hydration correction. This report does not convert earlier runs into proof of
those later source changes.

Specialized old schema/evolution/session commands now have explicit headless
Windows `win32_window_catalog_history` and `_v1_history` helpers. The original
Surface library and its S1 package remain separate from new GUI S0 profiles.
The registered historical version matrix and corruption guard PASS. The full
A→B→A→B lifecycle matrix remains BLOCKED / NOT PASS at A's refusal of an unknown
S1 Event (exit 3), with storage preserved. Its historical GUI-named harness now
checks the headless model/session boundary; actual GUI proof is the new process
test above. No rollback migration or unknown-Event policy was added.

Windows uses desktop MSVC v143 14.44 and Win7 API/subsystem flags in the isolated
`build-generic-legacy-win7` cache. Its final Debug links disable incremental-link
ILK regeneration after reclaiming exactly 19 owned cache ILKs (640.08 MiB), with
a private ledger; objects, libraries, compiler PDBs and executables were preserved.
No shared `build-window-layout-win7` flags were changed. Actual Win7, physical
mixed-DPI/accessibility matrices, Apple runtime and Release packaging are NOT RUN.

Final Windows aggregate refresh — 2026-11-17:

After the editable-label supply metadata, exact-layer/one-DomainGraph common-core
and Windows accessibility GUID corrections, the dedicated v143 14.44 Debug cache
incrementally rebuilt Surface v0/v1/load-only and Main/Dynamic consumers with
`/DEBUG /INCREMENTAL:NO`, one job. The final combined selection passed 12/12 in
12.82s. Surface common model passed in 0.93s and the actual Surface executable
GUI/cold/v1 scope passed in 4.17s; this closes the Windows native refresh noted
above. Main/Dynamic GUI, their independent supply consumers, generated consumer
compile graph and repaired historical probes passed in the same selection.
Private build/test logs and SHA-256 hashes of the actual seven GUI binaries are
in `.artifacts/main-dynamic-canon-final/`. GTK/Chrome and other runtime claims
retain their separately recorded source and executed-evidence boundaries.

The root's final static PE audit of the seven rebuilt Main/Dynamic/Surface GUI
executables passed subsystem 6.01 and found zero known post-Win7 direct imports
in the audited list. Actual Windows 7 execution remains NOT RUN.

Final GTK/Chrome and existing-consumer refresh — 2026-11-17:

The earlier pending native-refresh notes above are closed for Surface. The root
rebuilt the final shared source and ran the actual GTK Surface smoke successfully
in `.artifacts/baseline-linux-final-runtime.log`, including Add/Close/RU,
surviving XIDs, resize/orientation, native X and USER/LOCAL cold topology/placement.
The same log records current Catalog and MinimalApp historical GTK scopes PASS.
The final rebuilt Chrome Surface smoke passes in
`.artifacts/baseline-web-surface-final.log`, including DOM identity/current page,
language, orientation, hidden USER/LOCAL, cold state and shutdown. Current Web
Catalog and explicit load-only MinimalApp regressions also PASS, including native
lifecycle and handle release. The Windows existing-consumer selection passes
3/3 in 25.37s in `.artifacts/baseline-windows-final-ctest.log`.

The [final 30-rule audit](all-applications-ui-canon-audit-2026-11-17.md) records
source acceptance and exact evidence boundaries. These refreshed native results
do not change the historical S1 rollback failure or the Win7/Apple/hardware and
Release NOT RUN limits above.
