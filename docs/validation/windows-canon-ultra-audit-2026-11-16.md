# Windows/common Canon audit — 16 November 2026

## Scope and evidence

Read-only source audit by the delegated `canon_ultra_audit` agent, with ULTRA
reasoning as assigned by the parent. Baseline:
`c945f5d2a5f0e8d5ab530b0a6ab746f4324b355d`, branch
`maintenance/rename-window-catalog-example`. This report is the sole permitted
documentation write; no implementation was edited and no tests were executed by
this auditor. Line references below identify that baseline, not subsequent fixes.

Read: AGENTS.md; normative AppTraverse_Architecture_RU.md; the superseding
ClientLocalEnvironment.md contract; system-settings-accessibility-plan.md;
Windows presenters/environment adapter, WindowCatalog model/composition/lifecycle,
generic session and UI schemas, LOCAL routing and Events, and relevant tests.
Old runtime-only OS-baseline instructions are superseded. The current user task
also supersedes the old restriction deferring other non-Apple adapters.

## Findings that require correction

### A1 — P1: native observations still alter portable pixel geometry and layout

`src/platform/windows/win_ui_presenters.cpp:231`, `:309`, `:315` report native
frames through the old model path even though LOCAL now receives the same window's
observations. `examples/window_catalog/windows/window_catalog_presenters.h:102`
routes these to the catalog's frame/viewport queue. The old state remains
serialized: `include/apptraverse/ui/ui_desktop_window.h:41–51` stores global x/y and
pixel restore dimensions; `examples/window_catalog/common/window_catalog_model.h:270`
stores `main_client_viewport`. At `window_catalog_model.cpp:254–286`, the viewport
Event writes the measured width/height/density, runs
`ComputeWindowCatalogMainChromeLayout`, and writes computed rectangles into
serialized list/button presenters. `src/ui/ui_desktop_window.cpp:48–58` additionally
persists restore geometry when the native report requests it.

Consequences: resizing or changing the local presentation changes portable USER,
produces redundant model/publication work, and retains two placement authorities
(old portable desktop pixels and new LOCAL DIPs). Importing portable state can
carry another device's measurements even when destination LOCAL correctly wins.
The Windows local solver masking these old rectangles does not remove their
history or serialization.

Basis: settings plan §1 assigns viewport, scale, measurements and solved rectangles
to local presentation; §2 makes UI-local solving the geometry owner; the new LOCAL
contract assigns device observations and preferred/resolved placement to LOCAL.
Canon §§5, 14–15 and §25.5 require a single authoritative state path.

Fix: native viewport/layout measurement must remain in the presenter runtime;
persist the explicitly required window facts/preference only through LOCAL Events.
Keep portable semantic frame requests distinguishable from native observations.
Remove the obsolete model-pixel layout path for migrated backends rather than
maintaining both solvers. Preserve Apple source compatibility without treating
legacy serialized observation fields as the new design.

Verify: change native size, DPI and text scale and assert portable USER/history
does not acquire the device measurements; only LOCAL changes as required. Transfer
portable USER between devices with different existing LOCAL and assert the
destination placement remains authoritative. Re-run native and cold lifecycle
acceptance, updating tests whose prior pixel-persistence expectation is superseded.

### A2 — P1: layout and LOCAL support require WindowCatalog-specific integration

`include/apptraverse/platform/windows/win_ui_presenters.h:94–95` defaults
`LayoutRoot()` and `LocalDeviceState()` to null. Only the WindowCatalog subclass
provides them, by reaching through the application catalog
(`examples/window_catalog/windows/window_catalog_presenters.h:37–43`, `:85–87`).
`UiPresentedWindow` has only its presenter link; the standard window schema has no
layout/device links. A standard window therefore silently uses the old rectangle
path and receives no persisted local environment support.

LOCAL storage/load/seed/save is implemented only inside the example session
(`examples/window_catalog/common/window_catalog_lifecycle.cpp:84–114`). Startup
sampling is example-main wiring (`examples/window_catalog/windows/window_catalog_main.cpp:217–225`).
The generic `RunEmbeddedApplicationModelSession`
(`include/apptraverse/embedded_application_model_session.h:17–28`) still accepts
only one writable store. Extending each port by copying this example code would
retain the same architectural violation.

Basis: Canon §§1, 18 and §25.7; settings plan §10 explicitly requires an independent
composition of standard components to obtain these services through the ordinary
application host without WindowCatalog dependencies.

Fix seam: bind typed `UiLayoutState` and `UiDeviceState` references in the standard
presented-window composition, before history initialization/distillation. Expose
generic `UiWindowPresenter` access through those links. Keep omission of a content
Close control, when native caption supplies it, as explicit presentation policy.
Do not turn a missing required link into an optional feature or synthesize defaults
in a consumer. Add a common embedded-session composition accepting USER RAM,
LOCAL RAM and the graph; derive roots from the supply marker, route before Load,
and run model preparation before initial publication. Platform lifecycle owners
then save the two ordinary stores at the existing accepted boundary.

History/clone constraint for that fix: each child clone must keep the device root
and shared supply definition as external shared references; only child-specific
override state is cloned. Preserve those references in generic historical capture.
The current catalog-only hook at `window_catalog_model.h:315–320` protects the
catalog's device reference; moving links to base windows requires extending the
protection at the corresponding generic ownership boundary. Do not merely add more
application-specific ID-repair hooks.

Verify: independent MinimalApp/standard-component composition, with no inclusion
of WindowCatalog files, receives layout/settings/LOCAL persistence. Check clone,
capture/replay, immediate compaction, cold USER+LOCAL load, and absent/existing
LOCAL transfer. Non-Apple platform hosts must use the common composition.

### A3 — P2: placement observations can manufacture an unresolved `kFound` result

`src/ui/ui_device_state.cpp:54–70` commits a first/user window observation without
running `ResolveWindowMonitors`. Its Apply branch at `:122–135` sets both preferred
and resolved monitor to the observation's key and unconditionally sets `kFound`.
Resolution against the accepted environment occurs only in `ObserveEnvironment`
at `:41`/`:51`.

A queued observation for a monitor that has just disappeared can therefore run
after the device-environment Event and restore `kFound` for a key absent from that
environment. Likewise, a first observation accepted while the environment has no
monitors leaves `kFound` until an unrelated later environment observation. The
presenter then cannot find the selected monitor and defers placement. This is a
source-level ordering defect; no physical monitor-removal run was executed here.

Basis: ClientLocalEnvironment.md requires found/missing/unavailable choice through
model Events, with replay applying an already accepted choice and no nested Events
inside Apply.

Fix: observation updates facts/preference; the command path then resolves against
the latest accepted environment and commits an explicit resolution Event when
needed. Preserve the user's original preference during fallback. Do not call the
OS or commit recursively from Apply. Add a headless ordering regression: accept
monitor loss, then a stale/queued first or user window observation for that monitor;
assert missing/unavailable, fallback selection, preserved preference and replay.

### A4 — P2: persisted initial values are supplied implicitly by constructors

`include/apptraverse/ui/ui_device_state.h:104–106` initializes persisted baseline
flags/counters/reason in the constructor; nested persisted structs also provide
in-class default values. `include/apptraverse/ui/ui_layout_state.h:40` initializes
persisted `has_override` in the constructor. The builder creates these objects and
captures their history immediately (`window_catalog_graph_builder.cpp:121–123`,
`:153–156`, `:240–243`) without explicitly defining the corresponding initial
state. The device-state test's supply helper has the same dependency.

Basis: Canon §7 and §25.3 require persistent initial data to be explicitly provided
by builders/creators, Load or Clone; safe runtime initialization is a separate rule.

Fix: supply explicit bootstrap values at composition, including empty observation
state and no override, before InitializeRuntimeNode/FinalizeDistilledGraph. A
named library bootstrap helper taking the values is suitable; hidden fallback in
Load/presenter is not. Keep ordinary Debug fresh and application S0-only.

## Findings not promoted to confirmed violations

- Native-child readiness checks account for the real parent/sibling activation
  lifecycle; their mere existence is not proof of forbidden optional graph checks.
  Distillation should nevertheless validate required layout/control bindings,
  while runtime readiness only waits for native creation.
- `ApplyLocalPlacement` (`win_ui_presenters.cpp:445–450`) skips any first observed
  placement revision if a native report already exists. A coalesced later
  resolution could be skipped as though it were only the initial echo. Add a
  publication-order test during placement work; this audit did not execute one.
- Destination DPI negotiation (`:459–464`) moves with `SWP_NOSIZE` before reading
  destination HWND DPI. A very large source frame can still overlap another
  monitor more than the destination. Test mixed DPI and large-window transfers;
  this is an identified edge to verify, not a claimed reproduced native failure.

## Contracts that the examined implementation already follows

- Native environment calls occur on the UI side and pass value snapshots through
  Proxy/model commands. Startup comparison precedes first publication in the
  Windows example. No direct model mutation from Windows UI was identified in
  the examined new environment path.
- `ClientLocalState` has ordinary native class layers; its ownership scope is
  preserved into the historical base by `InitializeRuntimeNode`. `CommitLocal`
  assigns local ownership to Events. Routing uses ordinary RAM stores and ordinary
  user.bin serialization. This audit found no custom INI/JSON persistence in the
  new path.
- Existing LOCAL is preserved wholesale; absent LOCAL can be seeded. The loader
  avoids opening the seed when destination LOCAL already exists.
- Environment comparison normalizes monitor order; identical observations can be
  no-ops. Monitor-resolution Apply does not read the OS or commit nested Events.
  Automatic fallback observations preserve the preferred monitor.
- The new model tests cover LOCAL scope isolation, replay, startup no-op, seed
  precedence, loss/return and immediate compaction. The catalog partition test
  checks that LOCAL content does not leak into portable history. These are source
  observations about test coverage, not newly executed PASS results.

## Coverage of all 30 mandatory Canon rules

This matrix evaluates the current Windows WindowCatalog/common scope. `Source
aligned` means the examined path supports the rule, not a runtime PASS or proof
about every library feature. `Partial` separates an observed implementation from
an unexecuted acceptance boundary. `Superseded` identifies explicit later user
authorization, not a license to silently revise Canon. All 30 rules have been
considered; the confirmed defects are the four findings above.

| §25 rule | Assessment | Evidence and required follow-through |
| --- | --- | --- |
| 1. Complete graph | Violation A2 | New required concepts are absent from the generic presented-window graph; WindowCatalog overrides supply them. Existing title/locale/control paths otherwise use distilled links. Bind the new links in composition. |
| 2. All or nothing | Source aligned | Windows native creation failures call FatalWin32; load/clone failures terminate at the producing boundary. Native sibling readiness is a lifecycle condition. No claim of native-resource failure testing. |
| 3. Explicit persistent defaults | Violation A4 | UiDeviceState and UiLayoutState constructor defaults become distilled data without explicit builder assignment. |
| 4. Real runtime conditions | Source aligned, scoped | WM construction before GWLP_USERDATA, disabled controls, native sibling creation and closed/lazy children have distinct conditions. Catalog delayed child reports resolve against surviving catalog membership. Do not remove these checks while fixing A2. |
| 5. Model is author | Source aligned for routing; A1 state ownership violation | `ModelObjectProxy` captures ObjId/arguments by value and resolves in model Domain. New appearance/environment transitions use Events. Duplicate native-observation state remains portable under A1. |
| 6. Thin presenters | Partial; A2 | Measurement/painting and common layout execution are local, as the newer settings plan authorizes. WindowCatalog-specific device/layout discovery is unnecessary presenter coupling. Context-menu commands currently go through Proxy, not direct mirror mutation. |
| 7. Common application template | Violation A2 | Generic embedded session lacks LOCAL partitioning and the example owns its sampling/bootstrap. Independent standard-component adoption must become a verification case. |
| 8. One registration entry | Source aligned | Windows main calls EnsureWindowCatalogGraphRegistration once at line 189; graph registration composes model/UI/backend registration. No new registration call is required per observation. |
| 9. Fresh Debug | Source aligned | Windows CMake selects APPTRAVERSE_WINDOW_CATALOG_DEBUG_BOOTSTRAP; lifecycle builds fresh RAM supply and uses debug-run-* profile paths. Explicit load-only consumer remains separate. |
| 10. Release consumes supply/static runtime | Partial | Windows CMake separates fresh Debug from embedded consumer and compile policy uses static non-Debug CRT. No Release binary/link-map/import audit was executed; do not claim binary acceptance from source. |
| 11. Ordinary RAM stores | Source aligned; LOCAL exception authorized | ClientLocalDomainStorage routes to ordinary independent RamDomainStorage instances, beneath OverlayDomainStorage. It does not introduce a second object format. The local partition is explicitly authorized by the newer contract. |
| 12. Persistence lifecycle boundary | Source aligned in Windows; A2 portability gap | Child Close calls UnloadPresentedWindow/Save into RAM; Windows session writes USER and LOCAL on shutdown. Android/Web existing save boundaries must retain their semantics when generic LOCAL support is added. |
| 13. One serialization | Source aligned | ClientLocalState, UiDeviceState, Events and ownership metadata use native reflected layers; both files use SaveUserStorageFile. There is no new appearance INI path in the examined implementation. |
| 14. Object-system lifetime | Source aligned, scoped | ApplicationUiSession owns its separate Domain/storage; runtime historical-base creation retains canonical Domain ownership. WindowCatalog's temporary prefab domain passes clones into the long-lived target through CloneCanonicalRoot; no new global/thread_local keepalive was found in the examined Windows/UI code. |
| 15. Domains and Clone | Source aligned, with A2 fix constraint | AddWindow uses a temporary child domain with parent lookup and CloneCanonicalRoot; app code supplies stable origin/prefab identities rather than boundary_ids. Generic Clone performs remap/rebind. New device links must remain external shared references. Full deferred clone/version matrix was not rerun. |
| 16. Constant instance semantics | Source aligned | Overlay uses supply constant IDs and object-scope metadata; UiLayoutState retains the definition by identity during historical capture. Existing supply-default versus override tests are relevant, but not newly executed here. |
| 17. Lazy graph | Source aligned | Prepare explicitly loads presentation dependencies; holder.window remains a lazy identity when closed; LoadPresentedWindow materializes through ordinary links. No new Ensure/Declare repair was found in the examined path. |
| 18. History retained/retention zero | Source aligned | ApplicationModelRuntime sets immediate compaction for live nodes at line 72; history APIs remain. UiDeviceState resolution Apply does not create nested Events. A3 concerns command ordering, not replacement of history. |
| 19. Close/unload/delete distinction | Source aligned | CloseHolderWindow commits closed state, unloads/saves window and saves holder; DeleteSelected separately removes catalog topology. UnloadPresentedWindow retains presenter private state until RAM save completes. |
| 20. Active localization | Source aligned, scoped | Prepare loads required text links and materializes selected language; Windows title/list/button reads use those links. Theme menu text uses application language; layout-menu localization remains a product gap, not evidence that mandatory graph links are absent. No new cold language test was executed. |
| 21. Placement through Events | Violation A3; A1 overlap | LOCAL found/missing/return Events and DIP-to-native projection implement the newer placement contract, but ObserveWindow can bypass resolution and restore a stale kFound. Legacy portable native-pixel state must be retired. |
| 22. Headless same model | Source aligned; verification pending | UiDeviceState and UiLayoutState have no Win32 dependencies; headless tests exercise their Events/replay. Native adapter correctness still needs native tests; model tests cannot substitute for destination-DPI or UI-action verification. |
| 23. Prototype S0 | Source aligned | New application/local/layout schemas remain S0. No migration is introduced. Generic schema fixes must continue to use fresh compatible current supplies and preserve old experimental profiles separately. |
| 24. Normal environment/no hardening | Source aligned, scoped | Examined new persistence and native operations execute outside assert and process failures at their boundary. No new recovery/checkpoint/sidecar subsystem is introduced. This audit did not expand into corrupt-storage testing. |
| 25. Used components/scope | Source aligned with explicit scope update | Examined UI composition uses Window/List/Button/localization; it does not restore removed Bitmap/EditBox/Label. The user now explicitly authorizes non-Apple adapters, superseding their old deferred status. |
| 26. Technical policy | Source aligned; runtime acceptance partial | CMake sets C++20; common compile policy disables compiler RTTI/exceptions, rejects unsupported newer MSVC baseline and targets Win7. HWND/HFONT/WinRT subscription handles remain runtime members. Actual Win7 and final imports remain unexecuted acceptance. |
| 27. Incremental Debug by default | Followed by audit | No build, clean, populate, dependency download or Release action was performed by this auditor. Required implementation checks should use existing Debug builds and explicit current port authorization. |
| 28. Network/future mechanisms | Superseded in explicit separate scope | AGENTS.md contains later authorization for opt-in SharedNode work. This does not make network transport part of WindowCatalog or justify auditing/reworking it in this task. Deferred resources/cache/migration remain deferred. |
| 29. Evidence with scope | Followed | This report distinguishes source inspection, test coverage seen in source, potential ordering/hardware edges, and unexecuted tests. Historical PASS records were not rewritten. |
| 30. Publishing/work ownership | Followed | No implementation edits, commit, push, merge, force-push or deletion by this auditor. Only this specifically requested audit artifact was authored. Coding is assigned by the user to GPT-6.1 Sol high; Astra remains the Canon auditor. |

## Acceptance boundary

Do not add migration, crash recovery, corrupt-file handling, arbitrary linear
constraint solving or a full SharedNode audit to these fixes. Preserve Win7's
optional newer APIs and compatible toolchain; actual Win7 execution remains a
separate unavailable-environment boundary. Real screen reader/High Contrast,
mixed-monitor hardware and Apple runtime acceptance must retain their actual
NOT RUN/BLOCKED scope unless executed. The present document is a source audit,
not a replacement for the required non-Apple build/runtime/portability matrix.

## Post-fix review — 17 November 2026, provisional

Read-only Astra ultra review of the working-tree changes following baseline
`c945f5d2`; implementation remains assigned to Sol high. No native tests were run
by this auditor. The coordinating agent requested this provisional checkpoint to
release the worker slot for corrections B1–B3 below. It is **not final acceptance**.

### Original findings

- **A1 resolved in the examined migrated observation paths.** Windows no longer
  reports native frames/screen to the portable chrome pipeline; GTK, Web and Android
  similarly route local facts to UiDeviceState. The old semantic/Apple-compatible
  APIs remain, but migrated native callbacks do not use them for measurement.
  B3 below is a newly exposed explicit-request edge of this separation.
- **A2 resolved structurally.** UiPresentedWindow has typed layout/device links;
  generic presenter access, explicit caption-close policy, dual-RAM session routing
  and pre-publication preparation are shared. MinimalApp builders and non-Apple
  hosts adopt these without WindowCatalog dependencies. Generic historical capture
  collects nested live-reference policies and excludes its unhydrated destination;
  the device root and layout definition retain their identities.
- **A3 resolved.** ObserveWindow commits the observation, then resolves against the
  current environment in the command path. Apply remains replay-pure. The new test
  covers queued first/user reports after loss/no monitors with and without immediate
  compaction, and replays the accepted resolution.
- **A4 resolved at the application composition boundary.** Catalog and independent
  MinimalApp explicitly Bootstrap device/layout values before initializing history.
  Constructor initialization remains harmless because builders no longer depend on
  it to supply the persistent initial state.

### Confirmed additional corrections

**B1 — P2, Android required-control completion (Canon 1–2).** At review time,
`src/platform/android/android_ui_window_presenter.cpp`, RefreshLocalLayout's
`if (!mounted(mounted, *LayoutRoot())) ... return` silently retained old native
rectangles indefinitely. There was no eventual completed-construction validation.
A structurally valid override Event with an absent control ID therefore publishes
successfully while Android ignores the new layout. Unlike Windows/GTK/Web, the
adapter did not distinguish initial sibling mounting from a missing required
binding after presentation. The Sol Android owner is adding an explicit completion
boundary/failure plus regression; legitimate construction/closed lifecycle conditions
must remain. This is not corrupt-profile hardening.

**B2 — P2, GTK outer/client frame mismatch (Canon 21 and common schema).** At review
time, Linux HandleNativeFrameChanged put scroller/client dimensions into
UiWindowEnvironment.frame_dip, while ApplyLocalPlacement passed those dimensions
directly to gtk_window_resize. The shared field explicitly describes an outer frame,
and Windows writes GetWindowRect dimensions. A native Windows LOCAL seed therefore
became a larger GTK outer window; GTK-to-Windows could shrink content. Same-platform
GTK cold restore could pass because both sides used the same incorrect interpretation.
The Sol GTK owner is correcting capture to outer extents and converting requested
outer size to native client size only at GTK's boundary, with an actual decorated
window/seeded-size regression. Android/Web observations describe their borderless
logical app surface; they do not move the decorated physical browser/Activity window.
No analogous confirmed decorated outer/client conversion error was found there.

**B3 — P2, same-value explicit RequestFrame after a native move (Canon 5/21).**
`src/ui/ui_desktop_window.cpp:94–103` still returns when requested values equal the
portable semantic frame. With A1 fixed, a native drag no longer updates that frame.
Consequently an explicit request to restore the original semantic position/size
does nothing. Existing new tests request different values and miss this case.
Preserve explicit request delivery (for example, the runtime publication request
marker and materialized-change notification) even when its semantic data is
unchanged; do not restore native-to-portable feedback or introduce replay effects.
Add drag/resize followed by the same semantic RequestFrame on a real HWND, plus the
same shared-boundary regression where applicable. Sol Windows/common will implement.

### Evidence assessment

The original Windows regression replacement is adequate for its stated native
boundary: the helper uses real HWNDs, SetWindowPos and explicit WM_ENTERSIZEMOVE /
WM_EXITSIZEMOVE messages; fifty moves, a real resize, delayed/ABA publications,
fresh-Domain object reload and a recreated HWND are asserted. This is programmatic
native message/geometry testing, not a claim of physical mouse-input execution.
The focused helper's cold reload uses native RAM object storage; the independent
MinimalApp process acceptance separately checks actual USER+LOCAL files and cold
native restoration. New assertions keep portable frame/generation unchanged.

All **28** records in outputs/canon-fixes/final-windows-test-results.json were
matched by this auditor to their Passed lines in the named build-directory logs.
The four retained executable SHA-256 values were recomputed and match the correction
report. PE import/header artifacts were inspected; their scope remains static,
not actual Win7 execution. These records precede B1–B3 and do not prove those fixes.

MinimalApp headless coverage proves generic dual stores, prepared initial publication,
local identity/history, cold native files and exclusion from USER. Native MinimalApp
provides a separate automatic-adoption check. Linux/Web's current report records
actual GTK/browser lifecycle, both absent/existing LOCAL transfers from Windows,
return-file semantic inspection and a separately distilled compatible Web supply
update. Its initial GTK size coverage did not catch B2. Android final instrumented
and APK update evidence was still being completed at this checkpoint.

### All 30 rules: post-fix matrix delta

This supplements the baseline table without rewriting its historical findings.

| Rules | Current assessment |
| --- | --- |
| 1–2, graph/completion | Generic A2 fixed; Android B1 still requires completion evidence. |
| 3, initial data | A4 fixed by explicit composition. |
| 4, real lifecycle conditions | Preserved; B1 must distinguish initial mounting from missing final binding. |
| 5, model author | Proxy/Event/publication preserved; A1 fixed; B3 explicit request delivery remains. |
| 6–7, presenters/template | Shared typed links and dual stores adopted by independent MinimalApp; no WindowCatalog lookup required by generic backend. |
| 8, registration | No new per-event registration path identified; ordinary entry/composition remains. |
| 9, fresh Debug | Retained in current non-Apple builders/hosts; load-only profiles remain explicit. |
| 10, release | Source policy retained; this work provides Debug binaries, not new Release packaging acceptance. |
| 11–13, storage/boundary/format | Ordinary USER/LOCAL RAM and native files; Windows shutdown, Android onStop and Web lifecycle barriers preserved. Web captures both partitions at one model barrier and commits them in one IDB transaction. |
| 14–16, lifetime/clone/constants | Generic live-reference capture and typed links reviewed; model/clone tests recorded PASS. No new lifetime/global keepalive violation identified. Independently distilled Web supply-update evidence exercises default/override/Clear. |
| 17–19, lazy/history/close | Existing mechanisms retained; generic LoadPresentedWindow now loads its required services; immediate-compaction and cold-lifecycle coverage retained. |
| 20, localization | Mandatory locale/title links preserved; Windows/GTK/browser scenario evidence includes localization. |
| 21, placement | A3 and Windows coalesced-placement/staged-destination fixes present; B2 and B3 remain confirmed corrections. Physical mixed-monitor behavior is NOT RUN. |
| 22, headless | Same model and native-format state; headless tests supplement real HWND/GTK/browser coverage, not replace it. |
| 23, S0 | Current schemas remain S0; fresh matching supplies; no profile migration introduced. |
| 24, normal environment | No new recovery/sidecar/hardening mechanism required; observed side effects remain outside assert. |
| 25, scope/components | Authorized non-Apple adapters and independent MinimalApp; no removed generic controls or Apple implementation work added. |
| 26, technical policy | C++20/no compiler RTTI/exceptions and v143 Win7 import policy retained; actual Win7 and accessibility hardware boundaries remain explicit. |
| 27, builds | Incremental existing Debug builds recorded; this auditor ran read-only source/log/hash checks only. |
| 28, separately authorized future work | No network/cache/resource/migration expansion in this correction scope. |
| 29, evidence | PASS source/log matches verified; provisional pending B1–B3 and Android final evidence. No 52-process matrix or physical-monitor PASS inferred. |
| 30, ownership/publishing | Astra review only; Sol implementation; no audit commit/push/merge. Raw profiles/logs/binaries remain outside staged source/docs. |

Final sign-off must revisit B1–B3 source and their executed regressions. The original
unavailable-hardware and Release/Apple limitations remain unchanged.

## Final source-conformance review — 17 November 2026

**Verdict: A1–A4 and B1–B3 are closed in the reviewed implementation. No remaining
confirmed architecture blocker was found in the scoped Windows/common and migrated
non-Apple paths.** This is source-conformance acceptance with the executed evidence
below, not a blanket claim that every platform/hardware/accessibility matrix passed.
The final Android functional evidence is **7/9 baseline cases PASS, two Recents
dismissal cases NOT PASS**, plus a separate native disabled-palette check PASS.
This supersedes the earlier pending-suite cutoff; the evidence-only addendum below
records the exact scope. The original failed attempts remain historical failures.

No implementation code was changed and no native tests or controls were executed
by this auditor. The only audit write is this report. All coding was performed by
the assigned Sol high workers. Review covered the final source changes, relevant
test assertions, retained logs and binary hashes; no further audit breadth was added.

### Confirmed closure evidence

**B1, Android:** the posted Java UI-dispatch callback enters
`NotifyAndroidWindowClientSizeFromJava`, which calls
`RefreshLocalLayout(..., true)`. Required missing bindings then fail explicitly;
synchronous parent/child construction still calls the non-final mode. The actual
negative test constructs a valid override Event containing absent control 424242,
saves through native object storage, and launches the current no-trace APK. Retained
`../outputs/android-acceptance/missing-binding.log` contains the exact unmounted
native control diagnostic, SIGABRT and `ANDROID_MISSING_BINDING_ABORT_PASS`.
This closes the silent partial-presentation path without adding profile recovery.

Android's current no-trace OS evidence also passed: live settings at density 480,
text scale 1300 and night mode on produced revision 33; unchanged cold restart
remained 33. Settings changed while stopped to density 500, text scale 1500 and
night mode off produced revision 36; another unchanged restart remained 36.
`os-final.log` ends `ANDROID_TRANSFER_OS_ACCEPTANCE_PASS`; matching values are in
settings-evidence.json. Startup prepare still compares before initial publication;
later native configuration settling may overwrite `last_reason` with
SystemNotification, so that final field alone is not presented as evidence of
which earlier Event ran. Actual compatible APK supply default 12→24 and retained
override 31 passed in supply-update.log. These are recorded executions, separate
from the final seven passing baseline functional cases and two unresolved Recents
dismissal cases recorded below.

**B2, GTK:** `ReadNativeOuterFrame` uses GDK frame extents. Placement waits for a
mapped window, obtains native decoration size, and subtracts it only when calling
the client-size API. Programmatic configure acknowledgements preserve preference;
new published placement revisions trigger application. A separate flag suppresses
only revision 1 originating from this native window's initial observation, so its
delayed publication cannot overwrite a newer immediate resize. Seeded/existing
LOCAL and later monitor-resolution revisions remain applicable.

The final `.artifacts/linux-outer-followup.log` was inspected: actual Windows seed
333×276 outer creates GTK client 331×251, stores LOCAL outer 333×276 and preserves
it across a fresh process; existing LOCAL outer 222×215 remains authoritative and
creates client 220×190. Live font/motion/theme, immediate resize with LOCAL-only
observations and MinimalApp two-cycle lifecycle pass. The final full historical
Catalog GTK smoke also passes in linux-outer-smoke.log, after both timing fixes and
the final B3 common implementation. The Linux/Web report retains the corresponding
scope and unavailable-hardware boundaries.

**B3, shared/Windows:** an equal-valued RequestFrame now marks the runtime request
pending and notes materialized change; pending duplicates coalesce. Persistent
semantic values and journal do not change. Replay Apply does not manufacture an
external request. The real HWND test moves away, requests the original exact
100/100/640/480 target and verifies native position/size return with unchanged
journal size. Headless tests cover generation/notification, one-shot consumption,
coalescing and replay. Six affected tests pass in canon-b3-tests.log.

All **29** final Windows per-test records were matched again to their named Passed
log lines. The four B3 executable SHA-256 values were recomputed and match the
updated correction report. Refreshed PE artifacts show the expected 6.01 Catalog /
6.00 MinimalApp subsystem versions and no searched mandatory newer DPI/WinRT or
CreateFile2/CopyFile2 imports. This remains static inspection, not a Win7 run.

The earlier real-HWND move/resize/delayed-publication tests and independent
MinimalApp file-based cold restore remain appropriate replacement coverage for
the retired portable-native-pixel assertions. Generic MinimalApp composition,
dual stores, history/live references, seed precedence and USER isolation retain
their model/native tests. GTK/Web/Android adapter evidence is not promoted to the
historical 52-process full portability matrix.

### Final disposition of all 30 mandatory rules

`Aligned` means no confirmed scoped source violation remains, with the evidence
and limits in this report. It does not assert every possible runtime scenario.

| Rule | Final disposition |
| --- | --- |
| 1. Complete graph | Aligned: typed required layout/device links; B1 final native-binding boundary fixed. |
| 2. All or nothing | Aligned: missing final native bindings fail; Android actual negative verified. |
| 3. Explicit persistent defaults | Aligned: application builders explicitly Bootstrap initial values before history. |
| 4. Genuine runtime conditions | Aligned: initial mount, closed/lazy presentation, pending publication and native acknowledgements remain distinct. |
| 5. Model authors state | Aligned: Proxy/Events/publication retained; measured geometry is LOCAL/runtime; B3 equal-data intent is runtime publication metadata. |
| 6. Thin presenters | Aligned: native measurement/projection and UI-local common solver; application discovery overrides removed. |
| 7. Common template | Aligned: shared two-store sessions and standard typed composition; independent MinimalApp adoption verified. |
| 8. Registration | Aligned in examined paths: application entry/composition registration, no new per-observation registration. |
| 9. Fresh Debug | Aligned: fresh profiles and explicit load-only consumers remain separate. |
| 10. Release consumption | Source policy retained; no new Release binary/packaging acceptance claimed. |
| 11. Ordinary RAM stores | Aligned with authorized LOCAL partition: ordinary USER/LOCAL/BACKING stores. |
| 12. Save boundaries | Aligned: child operations stay RAM; platform lifecycle persists the two native files. |
| 13. One serialization | Aligned: native object layers/scopes/Events/history; no INI or bespoke state format. |
| 14. Object-system lifetime | Aligned in scoped changes: owned domains/stores and generic live-reference capture, no new global keepalive workaround. |
| 15. Domains/Clone | Aligned: child override state remains local to clone; shared device/definition identities retained; relevant model/clone tests pass. |
| 16. Constant semantics | Aligned: definitions remain supply-owned; actual compatible Web and Android supply-update/override evidence. |
| 17. Lazy graph | Aligned: required services materialize through ordinary links; closed window identities retained. |
| 18. History retained | Aligned: immediate compaction and replay retained; LOCAL resolution Apply and B3 replay have no nested external effects. |
| 19. Close/unload/delete | Aligned: distinct topology/lifetime paths and retained cold-lifecycle coverage. |
| 20. Localization | Aligned in existing required-link paths; localized application scenarios retained. |
| 21. Event placement | Aligned in reviewed paths: A3 ordering, B2 outer dimensions and initial-echo handling, B3 request intent fixed. Physical monitor matrix remains NOT RUN. |
| 22. Headless model | Aligned source: shared model tests plus separate HWND/GTK/browser/Android adapter evidence. Android baseline functional evidence is 7/9 PASS; two Recents cases remain NOT PASS. |
| 23. S0 prototype | Aligned: current matching supplies/fresh profiles; no experimental-profile migration introduced. |
| 24. Normal environment | Aligned: failures handled at producing boundaries; no new recovery/checkpoint subsystem and no identified side effects hidden in assert. |
| 25. Used scope/components | Aligned: authorized non-Apple work and independent MinimalApp; removed controls not restored and Apple implementation work excluded. |
| 26. Technical policy | Aligned source/toolchain and static PE evidence; actual Win7/physical hardware/accessibility limits remain. |
| 27. Build policy | Existing incremental Debug builds; auditor used read-only source/log/hash checks. |
| 28. Future mechanisms | Explicitly separate SharedNode authorization preserved; no unrelated resource/cache/network expansion. |
| 29. Evidence scope | Failures/reruns, source versus runtime, Android 7/9 baseline PASS plus separate palette PASS, two Recents NOT PASS and unavailable hardware remain explicit. |
| 30. Ownership/publishing | Astra audited; Sol coded. Auditor made no implementation edit, commit, push or merge. Raw local artifacts remain outside source/docs staging. |

Actual Win7, physical mixed-DPI monitor transfer/unplug, real screen-reader and full
High Contrast/text-scale matrices, Wayland/other browsers, ARM devices, Release
packaging and Apple runtime remain limited exactly as the individual reports state.
The coordinator must preserve the final source/evidence pairing and carry forward
the two Android Recents failures without claiming complete functional acceptance.

### Final evidence-only addendum — Android cutoff

Reviewed [Android acceptance](window-catalog-android-settings-2026-11-17.md), its
final raw result summaries, the independent Recents diagnosis and the small native
palette correction. No new code investigation or test execution was performed.
The 30 source dispositions and closure of A1–A4/B1–B3 remain unchanged.

- **Seven of nine baseline functional cases passed across the documented runs.**
  The full load-only run had six passes and three failures; the subsequent narrow
  run established the appearance/LOCAL persistence case. This is not a 9/9 pass.
- **The native disabled-state palette check passed.** Final
  `instrument-appearance-final.log` records `OK (2 tests)`, 52.929 seconds: the
  existing appearance/LOCAL case and the added native disabled-color comparison.
  The palette edit retains the native disabled foreground color/alpha when enabled
  text is softened. It changes only Java presentation colors, preserving the
  existing Proxy/Event/publication and storage boundaries. It does not add another
  baseline-suite success or convert the Recents failures to passes.
- **Home/Recents and Back/Recents dismissal remain NOT PASS.** Actual current task
  removal assertions still failed. Independent Sol checks reproduced the same
  failure on stock Settings task 911 using matching native card geometry and
  explicit input/display gestures; restoring animations and restarting Pixel
  Launcher did not resolve it. This supports an emulator launcher/input boundary
  explanation but does not establish the exact cause or prove an OS-only defect.
  No target task was removed by task APIs to manufacture acceptance.
- The actual failed Recents scenarios had completed onStop saves for both native
  files; retained file evidence shows USER 6531 bytes and LOCAL 1186 bytes. Those
  saves and separately tested cold loads do not prove Recents dismissal itself.
- Actual Windows→Android portable-only, seeded and existing-LOCAL transfers;
  compatible APK/supply 12→24 with override 31 retained; live/restart comparison
  33→33 and 36→36; the B1 native negative; independent MinimalApp; and narrow fresh
  Debug checks retain their independently executed PASS scope.
- **No OS image upgrade was performed.** System-setting changes, process restarts
  and compatible application APK replacement are not evidence of Android OTA or
  system-image-upgrade compatibility. Other hardware/accessibility limits above
  remain unchanged.

The source-conformance verdict therefore remains accepted within the reviewed
scope, while complete Android functional acceptance remains explicitly limited by
the two unresolved Recents cases. Raw artifacts remain outside source/documentation
staging; this audit does not authorize deleting or concealing their failure evidence.
