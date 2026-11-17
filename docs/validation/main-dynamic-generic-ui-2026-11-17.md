# Main-window and dynamic-object generic UI migration — 2026-11-17

Scope: the existing Windows main-window and dynamic-object GUI executables,
including their load-only variants. Apple is outside this slice. The separate Surface migration and its executed
boundaries are recorded in [Surface validation](surface-generic-ui-2026-11-17.md).

The two demos now share an application model composed from UiDesktopWindow,
UiLayoutState and UiDeviceState through ComposePresentedWindow. The main-window
mode intentionally has an empty vertical layout. The dynamic mode preserves
independent numbered Items, Add, per-item Remove, persistent topology and stable
identities of unaffected Items. Each visible control uses the standard Windows
push-button presenter. Topology Events carry the prepared object; replay adopts
the same identity. Editable layout overrides use UiLayoutState Events. A concrete
RuntimeItemLayoutState expands immutable Add/Item constraint templates over the
published live Item identities. Its typed application link remains live through
historical capture; its projected constraint tree is a runtime cache. Add/Remove
changes topology without authoring a style override, so Clear retains every live
Item slot after the original Item is removed. The common UI-local solver and
native measurement own actual control rectangles.

The application hosts use ApplicationModelRuntime, RunClientLocalApplicationModelSession /
RunEmbeddedApplicationModelSession, and WinApplicationHost. There is no demo
model loop, mirror implementation, native geometry feedback loop or polling timer
in the active GUI targets. Desktop observations and native placement use the
standard device-local graph. Startup observes the real OS before publication.
USER and LOCAL are ordinary independent RAM stores persisted as user.bin and
local/user.bin at accepted shutdown. Normal Debug creates a unique fresh profile;
load-only restores the matching embedded supply. The Debug supply producer uses
Domain::ObjectIdSequence so its repeated graph construction retains identity.
These new schemas use distinct names and default profile directories; historical
experimental profiles are not migrated. Initial graph construction now lives in
producer-only runtime_ui_supply.cpp; only the regular Debug target compiles it.
The load-only target and non-Debug regular target compile only runtime_ui_model.cpp
and consume a previously prepared matching Debug supply. Non-Debug builds require
APPTRAVERSE_MAIN_RUNTIME_DEBUG_SUPPLY_HEADER or
APPTRAVERSE_DYNAMIC_RUNTIME_DEBUG_SUPPLY_HEADER; no non-Debug build was run.
The layout definition is marked supply-constant, its IDs are collected from native
supply metadata, exported in the embedded header and supplied to the Debug overlay.
Mutable labels retain their ordinary USER role.

The optional shared header apptraverse/application_persistence.h provides
CompactApplicationNodesForUserOverlay(Node&). It compacts reachable journals
before accepted overlay persistence and restores each runtime retention policy.
It is not installed as a mandatory lifecycle step for RAM-only applications.

Executed on this Windows host with desktop MSVC v143 14.44, incremental Debug,
WINVER/_WIN32_WINNT=0x0601, in the isolated build-generic-legacy-win7 cache. No
flags were changed in the shared build-window-layout-win7 cache. Dependencies
used the existing source caches, including the already patched object source.

- All four GUI executable targets built.
- CTest apptraverse_main_window_win32_smoke_test and
  apptraverse_dynamic_objects_win32_smoke_test passed, 2/2 in the final combined run (1.11s and 2.23s).
- The main native process created its window, accepted native move/resize,
  persisted USER and separate LOCAL on native Close, and restored the outer frame
  in a new load-only process.
- The dynamic native process created its Add and first Item controls, created
  Items 2 and 3, removed Item 1 while retaining controls 2 and 3, then persisted.
  The cold process retained Items 2/3 and the removed state, created Item 4,
  removed Item 2, and restored native placement. The test checked nonempty native
  control rectangles as well as native control identities.

The previous custom-host smoke sources are retained as historical source; the
registered current GUI smoke targets exercise the actual migrated executables.
Historical headless model probes remain separate. Enabling the formerly disabled
legacy configuration exposed nonexistent custom-test target references; their
real targets were restored rather than hidden. Four revived historical fixtures
also expected test presenter subtypes that their old builders never installed.
This was a pre-existing fixture wiring defect. The scoped correction explicitly
injects the fixture presenter factories into the authored initial supply and the
dynamic Item factory after load. Lifecycle counters isolate the loaded model and
mirror from the separate supply producer. The window-changed probe separately
asserts the initial presenter refresh before counting incremental refreshes.
Identity, ownership, unload/destruction, publication, replay and stale-removal
assertions remain active. The corrected four historical CTests passed 4/4 in
5.60s on this Windows host. They are headless fixture evidence, separate from the
actual migrated GUI process checks above. The old missing-load negative remains
a separate registered test.

The optional dynamic button adapter now queues actions through its required
persistent application owner and item identity, so a queued second Remove can
remain a genuine stale action after the original button leaves the graph.
Concrete NodeFor capture and a live owner historical-reference policy preserve
its owner/number/presenter fields during generic button Events and replay.

An adjacent Android host-side test dependency was corrected to the real CMake
target wc_android_exporter; its executable output name remains unchanged.

Actual Windows 7 execution, physical mixed-DPI/accessibility matrices and
non-Debug packaging are NOT RUN in this slice. Other applications and platforms
retain the boundaries in their separate reports; this slice alone is not a claim
of every possible environment or runtime scenario.


Final Canon correction and frozen-core refresh — 2026-11-17:

- Independent consumers compile the model without any initial graph builder and
  load the actual Debug-exported embedded headers. Both passed: Main 0.19s and
  Dynamic 0.50s. They check immutable definition metadata/export, absence of any
  UiLayoutDefinition class layer under any USER or LOCAL identity, compatible
  supply defaults, retained overrides, Clear and cold reload. The dynamic case
  adds Items 2/3, removes initial Item 1, and checks all remaining live slots and
  typed owner identity through layout replay and cold runs. Compatible supply B
  changes default padding/gap and Item-template minimum width; Clear adopts all
  of these defaults without restoring removed slots.
- The actual generated load-only compile-graph test passed (0.08s): both consumers
  compile runtime_ui_model.cpp and omit runtime_ui_supply.cpp. Non-Debug policy
  was reviewed in source; no non-Debug configuration/build is claimed.
- The consolidated Windows selection passed 12/12 in 12.82s against the frozen
  exact-layer/one-DomainGraph core and final Windows accessible-label backend.
  It includes the two new supply consumers, generated build graph, Surface model,
  Surface actual GUI (4.17s), Main actual GUI (0.92s), Dynamic actual GUI (1.47s),
  four repaired historical fixtures and the missing-load negative.
- The final native tests preserve their earlier business, identity, USER/LOCAL,
  shutdown and cold-frame boundaries. Standard native presenters now consume
  RuntimeItemLayoutState through virtual UiLayoutState::EffectiveRoot; no native
  presenter subclass or pixel geometry was added for repeat expansion.
- The final incremental build used the dedicated v143 14.44 Debug cache, one job,
  `/DEBUG /INCREMENTAL:NO`, with existing dependency caches. A pending Windows
  MSAA GUID linker issue was resolved by its owner before the successful final
  build and selection. Private build/test logs, cache link settings and SHA-256
  hashes of the seven actual Main/Dynamic/Surface GUI executables are retained
  under `.artifacts/main-dynamic-canon-final/`; binaries remain in the build cache.

This refresh does not claim actual Win7, physical mixed-DPI/accessibility,
non-Debug packaging or any unexecuted Apple/runtime scope.

The root's final static PE audit of the seven rebuilt GUI executables passed:
all declare Windows subsystem 6.01, with zero known post-Win7 direct imports in
the audited list. This is a binary/import check, not actual Windows 7 execution.

Final shared-consumer regression evidence is now complete on the checked desktop
and browser backends. Current Windows Catalog/chrome/MinimalApp passes 3/3 in
25.37s; rebuilt GTK Surface/Catalog/MinimalApp and Chrome
Surface/Catalog/MinimalApp pass their separate historical native scopes.
These shared-library regressions supplement the actual migrated Main/Dynamic
process tests above; they do not claim new Main/Dynamic Linux/Web applications.
Exact logs, source corrections R1–R5 and all 30 dispositions are in the
[final Canon audit](all-applications-ui-canon-audit-2026-11-17.md); Surface-specific
results are in [its validation report](surface-generic-ui-2026-11-17.md).

The final test-only generator correction registers the `build.ninja` policy
fixture only for Ninja generators. Both independent native supply-consumer tests
remain available on every supported Windows generator. The existing Ninja cache
regenerated and the focused policy test passed 1/1 (0.10s test, 0.15s total);
`build-generic-legacy-win7/Testing/Temporary/LastTest.log` was independently read
by the auditor. Product sources and the already-tested binaries did not change.
