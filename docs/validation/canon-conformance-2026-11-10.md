# Canon conformance audit — 2026-11-10

Source baseline: `06efdcbc2789df4ee84d53e24291d11c2319a866`.
Scope: Windows WindowCatalog and MinimalApp, their shared library/model paths,
and headless tests. The [updated canon](../architecture/AppTraverse_Architecture_RU.md)
records accepted user decisions; findings below were not turned into exceptions
merely because they exist in the code. No production or test source was changed
by this audit. Guidance and documentation were updated.

## Open findings

Status update after the audit: C1 was resolved by the runtime-tracing removal
iteration below. C2 and C3 were subsequently reproduced and fixed in the
[WindowCatalog-only iteration](window-catalog-canon-2026-11-10.md), with 18/18
Debug checks passing. C4 and other-example build issues are excluded by the
user's current scope. The original baseline evidence is retained here.

### C1 — P2: optional runtime clone tracing remains

`src/object_clone.cpp:73–80` checks `APPTRAVERSE_CLONE_FAIL_TRACE` with `getenv`
and reports the `std::source_location` of each failure. This is also compiled
in MinSizeRel. It contradicts canon §25.24 (no optional runtime debugging
infrastructure). Remove the diagnostic switch/location plumbing while retaining
normal `CloneError` results. Fatal-boundary error reporting in
`src/node_history.cpp:222,236,248` is a different responsibility and must not be
removed with it. This finding is from source inspection, not an observed crash.

### C2 — P2: a headless prefab needs post-clone business-link repair

`examples/surfaces_demo/common/window_catalog_graph_mobile_prefab.cpp:41–43`
deliberately leaves `close_button->catalog` unset despite receiving `catalog`.
`window_catalog_model.cpp:64–68,657` fills it in `WireWindowCloseButton` after
Clone and reassigns the existing window/presenter links. This is a persistent
business reference: `window_catalog_close_button.cpp:18–20` uses it to call the
catalog's close action, rather than a native host/proxy activation binding.

This conflicts with complete distilled prefab links and automatic preservation
of internal/external clone references (canon §§7,12 and §25.1,15).
`TestMobileCloseButtonAndAdaptiveLayout` in
`tests/window_catalog_headless_test.cpp:936–968` distills this prefab but checks
its links only after `AddWindow` performs the repair. Complete the catalog link
in the builder, test direct Clone before AddWindow, then remove redundant
wiring. The test is in the current headless scope; no Android device work is
needed. The incomplete graph is repaired before publication, so this is not a
demonstrated user-visible failure. Ordinary Windows desktop prefabs omit this
optional in-content Close button and do not exercise the defect.

### C3 — P2: unknown ancestor classes can evade factory rejection

Source-confirmed control-flow risk; **no runtime reproduction was executed**.
Tracked implementation: `cmake/patches/aether-objects-domain-hierarchy-v1.patch:84–118`.
Effective dependency: `src/aether-objects/obj/domain.cpp:243–305` in the verified
26-patch source tree used by this build.

`KnownStoredClasses` recursively obtains known ancestor classes and filters out
unregistered IDs. `GetMostRelatedFactory` leaves `storage_domain = this` when
that recursive call returns a nonempty list, then enumerates the child's storage
for its unknown-class rejection check. With an empty child and an ancestor
containing a known base plus an unknown derived class, the factory selection
can accept only the known base. This undermines all-or-nothing class loading
and the existing reject-unknown-stored-class contract (canon §§6,25.2).

Before upstreaming Æther, add parent and grandparent fixtures with a known base
and unknown stored derived ClassId. Require rejection at the actual layer-source
boundary, retaining legitimate local shadowing and known-class version handling.
The existing valid-graph hierarchy test passes but does not cover this case.
Do not report it as an observed failure of the valid Windows application supply.

### C4 — P2: CI does not exercise the current delivery contract

`.github/workflows/window-catalog-validation.yml:6` only triggers on the old
branch, has no PR trigger, and omits `examples/minimal_app/**` from its path
filter. Lines 25–34 configure Release/MinSizeRel without first preparing Debug
supply. Catalog's `examples/surfaces_demo/windows/CMakeLists.txt:106` rejects that
configuration when the required header is absent. Local PASS does not establish
reproducible CI. Update the workflow to the current branch/PR policy and build
Debug supply before its runtime consumers; include both Windows examples.

## Documentation corrected in this change

- Canon §26 no longer presents limitations of `8c863bc` as current facts.
  Current accepted rules and evidence are distinct; old source observations
  remain available in the immutable prior revision and historical §29 map.
- AGENTS, Cursor rules and PLAN point to the new canon and Windows/headless
  scope. The seven-platform roadmap remains a long-term goal.
- MinimalApp README now describes ordinary Debug as always fresh, embedded
  load-only startup with optional USER, the actual Windows model-session file,
  and RAM USER/single-file shutdown persistence. Its old directory-storage and
  required-existing-profile statements were obsolete documentation, not runtime
  defects.

## Checklist and evidence

P numbers below refer to canon §25. “Consistent” is a bounded source-review
result, not a proof about every path or platform.

| Principles | Reviewed implementation / evidence | Assessment |
|---|---|---|
| P1–4: graph completeness, initialization, lifecycle | Builders, prepare functions, `ui_mirror.cpp:132–135,194–202`, Windows native creation; current localization reads | Consistent in active Windows paths; C2 headless exception |
| P5–7: model input/output and shared template | `ui_push_button_presenter.cpp:12–16`, `ui_presenters.cpp:16–66`; common model runtime, UI session and WinApplicationHost | Consistent; independent application-contract test passes |
| P8: registration | Catalog `window_catalog_main.cpp:185`, MinimalApp `minimal_app_main.cpp:38`; dependency calls inside entry points | One entry point per application; internal module calls are valid |
| P9: Debug preparation | Catalog lifecycle `:68–106`, MinimalApp Windows session `:22–29`, Debug-only supply commands | Fresh graph and no old USER; load-only cold-test consumers distinguished |
| P10: release/static runtime | Root CMake runtime policy, example target source selection, binary inspector | Prior binary contracts pass; named exclusions only, not a proof of globally minimal linking |
| P11–13: USER/storage/serialization | `single_file_user_storage.cpp`; Catalog lifecycle `:96–124`, MinimalApp Windows session `:21–38`, session lifecycle `:44–46` | Plain RAM USER; one shutdown file; binary archives, no persistent sidecar |
| P14–17: ownership, domains and clones | `object_clone.cpp:177–219,297–343,776–884`; hierarchy/scope patches and tests | Local owners and rebind retained; no public boundary_ids; C3 uncovered boundary |
| P18: history/compaction | `model_runtime.cpp:32–34,119–124`, `node.h:535–570,677–681`, `node_history.cpp` | Immediate application compaction; typed Save/scratch, replay/version guards retained |
| P19: unload | `ui_presented_window_lifecycle.h:18–45`, Catalog close `window_catalog_model.cpp:593–610`, MinimalApp auxiliary lifecycle | Presenter private state saved to RAM before ownership release; lazy links retained |
| P20: localization | `ui_localization_string.cpp:166,174,225,249,254,275` | No required-current-slot fallback; history conditions retained; only active language remains resident after operation |
| P21: geometry and queued identity | Windows presenters `:173–196`, `ui_desktop_window.cpp:120–144`, catalog Windows graph `:22–35` | OS placement/work-area events, signed coordinates, model centering and identity-addressed commands |
| P22: headless | application contract, Catalog and MinimalApp headless suites | Same common model; C2 is covered only after repair and needs a more precise fixture |
| P23–24: prototype and failure boundaries | S0 application types, stream/archive/native result checks | Real-boundary checks are valid; C1 tracing is not |
| P25–28: components, build scope, deferred work | Deleted unused controls, compiler policy, current CMake and documented scope | Other platforms not audited; C4 CI remains open |
| P29–30: evidence/publication | Current logs, prior validation reports, guidance links | New source review and test rerun separated from historical results |

Multiple RamDomainStorage instances for USER/BACKING/scratch are valid. Cold
ancestor bytes may materialize a child-domain object; this differs from Find
returning a live ancestor object and is deliberately tested. Constant metadata
and the existing supply-ID storage policy are distinct; replacement of that
registry remains deferred. Temporary historical capture may materialize inactive
leaves, then restore their lazy residency. Do not remove legitimate history,
queue membership, optional component or I/O boundary conditions as fallbacks.

## Executed validation

Fresh execution on existing Windows Debug binaries: **11/11 PASS**, **35.41 s**,
log `.artifacts/canon-audit-debug.log`. Production/test source did not change;
no rebuild was needed for these documentation changes.

Tests: hierarchy, object clone, scope storage, historical-base materialization,
UI control materialization, desktop geometry, application-contract headless,
single-file USER, Catalog persistence boundary, Catalog headless, MinimalApp
headless. The suite does not contain the missing C2 direct-Clone or C3 unknown
ancestor fixtures, so PASS does not close those findings.

Prior evidence at this code tree: 82/82 Debug, the corrected native focus test,
17/17 MinSizeRel and three additional native repeats, recorded in
[the size/refactoring report](node-history-template-sharing-2026-11-10.md).
No new MinSizeRel build/run, native UI acceptance, latency benchmark, standalone
Æther test suite, other-platform build/device or external Google Doc edit was
performed during this audit. Æther's own tests are disabled by the AppTraverse
dependency configuration; upstream publication still needs their separate run.

## Follow-up: remove permanent runtime tracing — 2026-11-10

Starting revision: `d0739d1`. Removed the optional Clone environment switch and
source-location plumbing (C1), the unused initial-publication diagnostic stages,
fields and formatter, and the old Surfaces shutdown tracing subsystem. The
trace-only instrumented storage wrapper and timing/statistics snapshots were
removed with it. Required fatal messages, error return values, accepted-work
draining, shutdown Save and failure exit codes are preserved. The UI-session
test checks observable rejection/no presenter activation rather than an internal
diagnostic stage. Cross-platform edits only remove references to the deleted
source or adapt the Android probe to the simplified common signature.

Incremental Windows Debug (`AE_NO_DEBUG_LOG=ON`) active application/test build:
PASS. Focused CTest: **16/16 PASS**, **108.50 s**, including Clone/history,
publication/model runtime, headless applications, USER persistence, unload
round-trips, both Windows application acceptance tests and native fatal handling.
Logs: `.artifacts/remove-runtime-trace-active-build.log` and
`.artifacts/remove-runtime-trace-tests.log`. No MinSizeRel or other-platform
build/run and no fresh latency benchmark was performed.

An additional attempt to link the old `win32_surfaces_demo` failed on missing
`EnsureSurfacesDemoWin32Registration` and `AttachSurfacesDemoWin32SurfacePresenters`.
Their implementation files were already excluded from that target's dependency
sources at the starting revision; this change does not alter those registrations.
The touched old host/lifecycle source files compiled, but the legacy executable
is not build-verified. Log: `.artifacts/remove-runtime-trace-build.log`.

The follow-up source audit also found that `APPTRAVERSE_BUILD_LEGACY_EXAMPLES=OFF`
does not exclude the old Surface Windows executables and separate exporter from
default `all`, including MinSizeRel. This is a build-scope issue, not evidence of
those objects being linked into Catalog/MinimalApp. Gating those historical
targets remains separate work, as do C2–C4.
