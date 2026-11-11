# Domain hierarchy cloning: plan and progress

## Current status

Implementation and Windows acceptance are complete: all 83 configured tests pass, including all historical string cases, cold persistence, hierarchy/clone ownership, version compatibility, and 20 Debug Close observations. This section supersedes the incomplete acceptance report at `580f36d7ddffd995d61eb026d1bca010d11582c7`.

| Item | Value |
| --- | --- |
| Branch | `feature/domain-hierarchy-cloning` |
| Worktree | `C:\Users\nickc\Projects\apptraverse-wt-domain-hierarchy` |
| Original branch base | `0343a20b3e08f4d5ec0ecf589ce540f6cd91bc4a` |
| Completion work started from | `580f36d7ddffd995d61eb026d1bca010d11582c7` |
| Tested code revision | `61ad37405532e826939ccf60d9af5b443d251654` (documentation-only commit follows) |
| Dependency pin | `79283b667cabf689da1059d618fe2a22abf51db4` |
| Repository patch chain | 22 patches; verified against a fresh archive of the pin |
| Platform/configuration | Windows x64, Ninja, MSVC 14.50.35717, Debug |
| Object logging | `AE_NO_DEBUG_LOG=ON`; assertions and Debug runtime checks remain enabled |
| Source worktree | `apptraverse-wt-adaptive` was not modified |

Commit and push of this branch are authorized. No history rewrite or transfer of adaptive WIP is required.

## Contract

- Domain lookup searches the local registry and then ancestors. It never searches children or siblings. Separate child domains may hold the same local ID.
- A loaded `ObjPtr` derives its identity from its object. `Reset()` retains that identity and flags for later lazy loading. Existing `ae::Ptr`/`ProxyPtr` graph ownership remains in use.
- Runtime clones receive IDs that do not collide with live or stored identities visible in the target hierarchy. Internal edges bind to copies; external boundary edges retain shared identity.
- Bootstrap, Add, geometry/language events, history capture/replay/compaction, and UI publication do not write USER. There is no serialized session USER RAM store or global/thread-local keepalive.
- Operation-local serialization buffers are allowed for cloning, history snapshots, and UI mirror publication. They are discarded after use; live objects and their owning edges retain state.
- Explicit subgraph unload and application shutdown save to USER over read-only BACKING. A closed window must load from disk plus BACKING before application shutdown, including lazy presenter state and private leaves.
- No S0 wire schema change, weakened regression checks, disabled historical string cases, or manual dependency-cache patching.

## Implementation and evidence

### Domain lookup and scoped hydration

The existing hierarchy patch provides local/ancestor registry lookup and loaded/unloaded pointer identity. The new `scoped-layer-hydration-v1` patch adds a graph-local load-storage override and optional retention of preconstructed references. The override does not change the owning domain. Ordinary graphs retain ordinary storage and lazy-loading behavior. Layer lookup now walks every ancestor, including through an empty intermediate domain.

Tests cover local shadowing, sibling isolation, same IDs in separate domains, identity after transfer/reset/reload, cold grandparent layer lookup, exclusive snapshot reads, clearing the read override, and non-leakage of hydration options between graphs.

### Clone completion

The snapshot reader preserves source ancestor access. The staging domain preconstructs the complete local copy set before deserializing fields, so internal references cannot accidentally resolve to live source objects. All stored class layers load in inheritance order, including the Obj origin layer. Finalization transfers copies to the target and rebinds raw ObjPtr edges as well as ObjectLink edges before temporary domains are destroyed. A clone-specific rebind mode preserves owning LocalPtr edges, including lazy local-only leaves and external boundaries, without changing ordinary remap/publication behavior.

Regression coverage includes two runtime clones, lazy edges after result keepalive release, live external sharing, ancestor-stored unloaded source leaves, and collisions with live and unloaded ancestor identities. Clone operations do not change source/target USER storage.

### History capture and replay

Capture hydrates the existing base in its stable owning domain from transient scratch bytes. It no longer injects snapshot layers into persistent USER. Replay uses private copies of mutable leaves, so replayed events cannot modify the historical base. Owner/self references and immutable origins retain their live identities.

Compaction updates only live state. A runtime-only `history_base_recaptured_` policy makes the next ordinary explicit persistent Save retain the Node header and base, even when an empty compacted journal makes the header byte-identical to old BACKING. The new `force-materialize-object-layer-v1` patch consults this optional policy before opening a layer writer. Untouched cold-loaded nodes remain differential; no dirty state is serialized.

Historical scratch includes every known materialized version, even if ordinary persistent saving leaves that version owned by the origin. This prevents replay from falling back to the already-modified live field. Partial compaction folds only the validated prefix into known base layers and retains unknown layers and the remaining journal; full compaction keeps the newer-schema guard.

Unloaded localization strings restore the compacted historical value before replaying retained events. Already materialized prefix values are preserved. Tests include disk string lifecycle, repeated replay, independent leaf IDs, immutable origins, BACKING replacement, zero runtime writes, and untouched-node differential saves.

### Close and application integration

`UnloadPresentedWindow` saves the presenter graph before dropping its owning link, then resets that link and saves/resets the window. This preserves never-persisted presenter-only state while keeping the persisted window-to-presenter link lazy. The helper signature and existing callers remain compatible.

Boundary tests measure Store calls from before bootstrap/Add/resize and before language/reopen operations. They check fresh and seeded USER, cold closed holder/window/presenter IDs, geometry, a previously unsaved presenter-only title, backlink identity, shutdown restoration, and two simultaneous sessions. MinimalApp and presenter-branch round-trips do not mask unload with a root Save.

The Windows MinimalApp demo, load-only executable, export test, and acceptance test now consume one generated BACKING header. Previously separate exporter runs produced incompatible random graph IDs. Its acceptance probe now keeps ProfileDomain alive while examining loaded objects.

The direct graph-mirror helper now uses MaterializedPublication and excludes model history, matching the normal UI publication path. The distilled surfaces test explicitly materializes the selected language and checks that other language leaves remain lazy. The routing fixture now sends the real enter/exit interactive-move messages before asserting restore geometry.

The Close diagnostic builds its demo dependency explicitly. `HARNESS_ORIGIN` identifies the historical harness baseline, not the current tested code revision.

## Acceptance plan

| Stage | Implementation | Verification |
| --- | --- | --- |
| 1. Isolated source and patch chain | Complete | 21/22 patches PASS |
| 2. ObjPtr identity | Complete | PASS (full regression) |
| 3. Hierarchy and ancestor storage | Complete | PASS (full regression) |
| 4. Child-domain clone and finalization | Complete | PASS (full regression) |
| 5. History without runtime USER writes | Complete | PASS (full regression) |
| 6. Application integration without session RAM | Complete | PASS (full regression) |
| 7. Unload and cold persistence | Complete | PASS (full regression) |
| 8. Full Windows regression and Close 4 x 5 | Complete | PASS: 83/83 tests; Close 4 x 5 |

## Verification environment and commands

The original local `/Zi` configuration failed with MSVC C1902 (PDB manager mismatch). This run uses `/Z7 /Ob0 /Od /RTC1`: debug symbols are embedded in objects, with assertions and runtime checks still enabled. This is a local build-cache setting, not a project configuration change.

All builds and tests use `.artifacts/tmp` for TEMP and TMP. This avoids unrelated profile permissions and keeps generated supplies and test USER directories isolated from user application profiles.

A normal fresh CPM populate was blocked by the environment's MSYS helper startup. The native verifier instead archives the exact pinned Git commit into an isolated repository, applies every repository patch, verifies changed source bytes and expected APIs, and retains evidence. The build consumes that exact fresh tree via the supported FetchContent override. Existing dependency caches were not manually patched.

Verified source: `.artifacts/patch-chain-verification/20261110T084449671798Z/tree`.
Evidence: `result.json`, `verification.log`, `applied-chain.diff`, and original patch snapshots in its parent directory. Result: **PASS**, 22 patches, 10 changed source files, no rejects; final source tree `69698644c506e2c5d4069cb96f8c03b625f5a9b1`.

From a Visual Studio x64 Developer PowerShell at the worktree root:

```powershell
$env:TEMP = Join-Path (Get-Location) '.artifacts/tmp'
$env:TMP = $env:TEMP
New-Item -ItemType Directory -Force $env:TEMP | Out-Null
python cmake/scripts/verify-aether-patch-chain.py `
  --source-git-dir .cpm_cache_isolated_v3/aether-objects/4f66/.git
# Use the tree path printed by this new verification run:
$verifiedTree = '<verification output directory>/tree'
cmake -S . -B build/win64-ninja-msvc-debug -DAE_NO_DEBUG_LOG=ON `
  '-DCMAKE_CXX_FLAGS_DEBUG=/Z7 /Ob0 /Od /RTC1' `
  '-DCMAKE_C_FLAGS_DEBUG=/Z7 /Ob0 /Od /RTC1' `
  "-DCPM_aether-objects_SOURCE=$verifiedTree" `
  "-DFETCHCONTENT_SOURCE_DIR_AETHER-OBJECTS=$verifiedTree"
cmake --build build/win64-ninja-msvc-debug --parallel 6
ctest --test-dir build/win64-ninja-msvc-debug -j 1 --output-on-failure `
  -E '^apptraverse_window_catalog_win32_close_timing_diagnostic_test$'
ctest --test-dir build/win64-ninja-msvc-debug -j 1 -V `
  -R '^apptraverse_window_catalog_win32_close_timing_diagnostic_test$'
```

The verifier's `--source-git-dir` may point to any existing clone of `aethernetio/aether-objects` containing the pinned commit. It reads that Git object database and never modifies it. The actual build selected the 85 executable targets required by all 83 configured CTest entries; their dependency closure includes the GUI demos, exporter tools, and fatal-test child processes.

## Run history

- Starting revision `580f36d`: full historical string cases failed; runtime history capture wrote USER; acceptance was incomplete. Earlier partial-suite PASS was not full acceptance.
- First focused acceptance: 17/22 PASS. Remaining issues were localization compaction, explicit post-compaction persistence, owner/self history references, and MinimalApp supply/lifetime defects.
- Second focused acceptance: 20/22 PASS. The remaining model persistence failure exposed the byte-identical empty-journal header problem. The new hierarchy fixture also needed explicit factory registration.
- First full regression: 73/82 PASS. Failures: event-version known-prefix compaction, four prefab replay matrices, distilled surfaces lazy text, two direct mirror frame tests, and an outdated routing drag fixture. The routing fixture is fixed and passes separately.

- Second full regression: 74/82 PASS. Clone boundary mappings inadvertently expanded recursive remap traversal into external graphs; clone finalization now visits each copied object explicitly, with a regression for external boundaries owning non-copy references.
- Final regression: **82/82 PASS**, followed by separate Close timing **1/1 PASS** (20/20 observations). No production changes followed these runs.

## Final results

| Check | Result |
| --- | --- |
| Fresh dependency patch chain | 22/22 PASS |
| Full Windows CTest suite | 83/83 PASS, no skips |
| Runtime USER writes | Zero until explicit persistence boundaries |
| Cold Close / shutdown restoration | PASS, including private presenter state and geometry |
| Debug Close HWND destroy medians | cold 679 ms; burst 830 ms; after settle 842 ms; saved 225 ms |
| Main process exit | 108–157 ms, all exit codes 0 |
| Timeout / OBJ_SYS output | None / zero lines |

The Close results include real persistence. This work establishes correctness and the requested write boundaries; Close still costs roughly 0.7–0.85 seconds for a newly created window in Debug. The earlier session-overlay measurements are not a performance baseline for this different implementation. `AFTER_SETTLE` means stable HWND geometry, not a proven publication drain. Native-window hiding was not observed separately.

See [complete test and timing evidence](validation/domain-hierarchy-cloning-2026-11-10.md) for every test, all 20 Close samples, boundary output, executable hashes, and artifact locations. No required implementation or Windows acceptance stage remains open. Linux, macOS, Android devices, release performance, and a separate CPU sampling profile were outside this Windows acceptance run.


## Follow-up completion — automatic boundaries and Debug preparation

Branch: `feature/domain-hierarchy-cloning`, follow-up to `166631a`. The final implementation and tests are recorded in [English validation](validation/window-catalog-automatic-cloning-2026-11-10.md).

- WindowCatalog no longer supplies `boundary_ids`. Explicit roots copy; parent-domain and semantic constant references remain shared. Common persistent scope metadata retains this partition after cold load, including nested scopes.
- Corrected temporary-domain storage shadowing and relocation of serialized raw ObjPtr fields absent from reflection. Regression tests cover live/cold lazy external references, private internal ownership, exact flags, no eager parent Load and zero runtime USER Store.
- Normal Debug regenerates the initial graph and uses a fresh USER each launch. Windows standalone catalog exporter removed; MinSizeRel embeds Debug-prepared supply and retains ordinary USER restore behavior. Separate object/layer files remain the disk format.
- Close uses shared save traversal and cheaper directory preparation. Private presenter state, lazy window links and ancestor storage ownership remain verified.
- Fresh **26/26 patch chain PASS**; **84/84 Debug CTest entries PASS** including Close timing; **4/4 targeted MinSizeRel tests PASS**. Assertions remain enabled in Debug, `AE_NO_DEBUG_LOG=ON`.
- Final Close HWND destruction medians, 5 repetitions each: **400 / 505 / 550 / 171 ms** (cold / burst / stable geometry / saved). Main exit 125–190 ms. Final model-only disk/RAM controls: 316/4 ms cold, 523/7 ms burst. Individual file commits remain the primary cost; no claim of instantaneous Close.

Full logs, fixed-supply A/B, final supply hash, exact test list and deferred scope are linked in the validation record. No source changes followed final acceptance. Adaptive WIP was not modified.


## Windows/headless principles implementation — 2026-11-10

Completed the Windows/headless implementation of the agreed principles on `feature/domain-hierarchy-cloning`, based on `2dadc6b`. Persistent model defaults moved into builders; required presenter-link fallbacks and the desktop Load repair were removed after completing the fixtures. Localization retains inactive languages lazily through compaction and direct history capture. Public manual clone boundaries and application-side prefab eviction were removed. USER uses plain `RamDomainStorage` with explicit single-file load/save functions. Catalog uses OS initial positioning; native geometry/work-area events drive model placement and saved-position restoration.

Final incremental Debug acceptance: **84/84 non-timing tests + 1/1 Close diagnostic PASS**. Child Close medians **204/208/216/118 ms**; main exit after child Close **45–78 ms**. RAM model Close control **4/6 ms**, zero disk bytes. Fresh-window HWND latency is higher than the earlier single-file run; this is recorded without claiming a controlled performance improvement. No MinSizeRel or platform-device build/run was performed.

See [implementation, limitations, all test results and 20 Close samples](validation/windows-principles-2026-11-10.md). History/replay remain intact; future distributed/platform work stays deferred.

## Remove unused controls and enforce required window links — 2026-11-10

Scope: Windows and headless, incremental Debug (`AE_NO_DEBUG_LOG=ON`). User explicitly requested removal of controls unused by WindowCatalog and MinimalApp, including their dedicated tests.

- Removed Bitmap, EditBox and Label model/presenter classes, extended registration/libraries, Windows implementations, and unused EditBox caret conversion helpers. Consequently the remaining Bitmap in-class persistent defaults are gone with the component rather than repaired in unused code.
- Removed three dedicated CTest targets (two Bitmap publication tests and EditBox headless). Preserved mixed presenter graph, localization, native command-routing and button/list/window coverage. Local commit journal scaling now uses UiWindow events; repeated-event materialization notification is checked on UiPushButton.
- Removed missing-graph fallbacks for desktop window, list container, parent presenter and locale in the common accessor/Windows presenters. HWND lifetime, native creation results, interactive move state and selection notification policy remain runtime conditions.
- No references to removed components remain in source/build files; historical reports retain their original evidence.
- Final incremental Debug build PASS; full CTest **82/82 PASS**, including WindowCatalog/MinimalApp Windows acceptance, headless, USER persistence boundary, clone/history, and Close diagnostic. Logs: `.artifacts/remove-controls-final-build.log`, `.artifacts/remove-controls-final-tests.log`. Three fewer tests than the previous 85 are the explicitly removed dedicated component tests, not skipped failures.
- No MinSizeRel, other platform device tests, migrations or adaptive worktree changes.

## Windows/headless follow-up: prepared graphs and MinimalApp Debug — 2026-11-10

Base: `e78ccdf`. Scope: Windows applications and headless; incremental Debug, `AE_NO_DEBUG_LOG=ON`. Other platform implementations and adaptive WIP were not edited.

- Removed silent missing-link skips in Catalog chrome layout, required Close localization and Windows z-order row traversal, and MinimalApp auxiliary unload/save. Closed/lazy/native lifecycle conditions remain.
- Regular Windows MinimalApp Debug now builds a new initial graph in a separate `debug-run-*` directory on every launch without reading existing USER. The same executable provides `--distill-only`; the separate Windows exporter is removed. The explicit load-only consumer restores USER; MinSizeRel is configured to embed the Debug-produced supply (not built/run in this task).
- Removed distillation normalization from Windows MinimalApp session preparation and its headless/Windows restore tests. Cold state must already contain valid history bases. The legacy normalization API remains for other platform callers outside this task.
- Removed global publication serialization statistics, Node/Event counter hooks, and the extra scratch-layer byte-count traversal. Normal UI load does not instantiate/format detailed load diagnostics. The optional explicit diagnostic-probe API remains compatible for Android; this is not a claim that every diagnostic API was removed repository-wide.
- Added headless auxiliary Close RAM-to-single-file coverage: no files after Close or model Save, exactly one file after shutdown write, cold geometry/presenter identity restored. Added Windows acceptance for two regular Debug launches over an existing profile: both start fresh, prior USER bytes stay unchanged, each run produces its own initial supply and shutdown USER.
- History tests now check actual child identity/value/history and existing publication payload semantics instead of production counters. Preliminary run: 81/82; the failed new assertion incorrectly expected a shared child reference to lose its journal. Corrected the test to preserve shared identity/history; no production history behavior was changed to satisfy it.
- Final incremental build PASS. Full final CTest: **82/82 PASS**, including new Debug fresh-launch and auxiliary RAM boundary cases, both Windows acceptance suites, history/clone/localization, and Close diagnostic. Logs: `.artifacts/principles-next-final-build.log`, `.artifacts/principles-next-final-tests.log`.
- Remaining audit items are not declared complete: row-index/identity command contract, strict removal of repeated native/load checks, platform-specific normalization/probe APIs, Android SyncPost lifetime, and mobile/web window hierarchy. No MinSizeRel or device-platform acceptance claimed.

## Windows/headless list commands use row identity — 2026-11-10

Base: `c8c815a`. Scope: WindowCatalog Windows/headless presenters and the common list presenter used by MinimalApp. Incremental Windows Debug, `AE_NO_DEBUG_LOG=ON`; no MinSizeRel or platform-device build/run.

- The presenter validates the native row index against the displayed UI mirror and queues its ObjId. No UI row index crosses the model command queue in these Windows/headless paths.
- On the model thread, selection and activation resolve that identity in the current list membership. A shifted row remains the target; a deleted row makes the queued action obsolete without changing selection, opening a neighbouring window, or reviving a retained object. This membership check handles a real asynchronous lifecycle transition, not a missing required distilled graph link.
- The common list implementation resolves identity before calling its existing model handlers. Catalog resolves a current WindowHolder before invoking its event-based selection/open operations. Existing indexed adapters remain for other platform/model callers; persisted events and the S0 schema are unchanged.
- Added queued-command coverage through a separate UI mirror and ModelObjectProxy: an earlier row is deleted before selection/activation executes; the selected row itself is deleted while still strongly retained; invalid native indices never enter the queue. Generic list selection and Catalog selection/activation are covered.
- Incremental build PASS. Full Debug CTest: **82/82 PASS**, including both Windows acceptance suites, new headless/list identity cases, clone/history/localization, USER persistence boundaries, and Close diagnostic. Test run: 196.86 seconds. Logs: `.artifacts/list-identity-build.log`, `.artifacts/list-identity-tests.log`.
- This closes the Windows/headless row-index/identity item. Repeated native/load checks and the separately recorded platform-specific work remain separate audit items; no repository-wide completion is claimed.

## Remaining Windows/headless principles: consolidated completion — 2026-11-10

Base: `3b5fe5c`. Scope: WindowCatalog and MinimalApp on Windows, their common model/library paths, and host-side headless tests. This completes the remaining identified items in this scope together, rather than leaving a queue of individual Windows fixes. Adaptive WIP and other platform implementations were not modified.

### Changes in this iteration

- Required model-proxy connections now receive desktop geometry/screen events directly. Removed silent skips in the Windows Catalog presenter and common desktop presenter. Two native presenter fixtures now explicitly queue these reports and verify that reports were produced; HWND persistence still leaves model storage bytes unchanged.
- Native desktop/list creation failure is fatal at the creation boundary. A presenter can no longer return from OnLoad with a null HWND and be marked active. Removed repeated HWND checks in active-presenter synchronization and teardown; parent readiness, pre-activation native messages and interactive movement remain genuine lifecycle states. Shared child-frame reading replaces the duplicated Windows button/list helper. Empty native list strings are preserved, with space reserved for their terminating character.
- Windows Close dispatch always uses the application's model callback. Removed the host-notification fallback that could bypass the application Close policy. Default-state-directory resolution no longer silently switches to the working directory.
- Catalog Open uses the selected model object directly. Required holder/window and supplied localization references no longer use invalid-link skips. MaterializeLanguage trusts the supported-language input contract, like Resolve and Apply; historical replay conditions remain intact.
- In-content Close is an intentional optional prefab component, absent from desktop prefabs. If it exists, its button/presenter/context links are required. Clone creation wires them once; restore/reopen loads the saved graph without repairing links. Removed duplicate loading and an unused string persistence helper. Added cold-domain coverage proving that the restored Close graph has the correct window, catalog, presenter and domain, closes only the cold copy, and does not require rewiring.
- Normal Catalog Debug no longer calls the USER loader. Removed the extra embedded-profile filesystem probe/log marker. Single-file USER loading opens the file directly; ordinary first-run absence is handled from the open result. Actual I/O/archive failures still propagate; no recovery, file migration or validation subsystem was added.
- Added subprocess regressions for desktop creation failure, list creation failure and failure of the required initial UI publication load. The normal publication entry point terminates on failure; the explicit Try/probe API remains available for its separate diagnostic callers.

### Full agreed-principles checklist in the current scope

| Principle | Current implementation / evidence |
| --- | --- |
| Explicit persistent initialization | Model fields are initialized by distillation/builders and complete fixtures. Runtime/presenter state retains normal initialization. Geometry, locale, clone and headless suites pass. |
| Trust the distilled graph | Required links are accessed directly; graph repair and empty/missing localization fallbacks are absent from the active Windows paths. Optional in-content Close and closed/lazy windows are explicit variants/lifecycle states. |
| All-or-nothing application/UI loading | LoadApplication, required initial publication load and native creation fail at their boundaries. New subprocess tests pass; no partially activated native UI is accepted. This is not a claim of transactional disk writes. |
| Thin presenters; model owns behavior | Native input goes through ModelObjectProxy/events. Open follows model selection; Close follows the application callback. Windows command-routing and application acceptance pass. |
| Queued selection uses identity | The preceding identity change remains covered: shifted rows keep their target, deleted rows produce no action. Windows/headless presenters do not queue stale UI indices. |
| Common application template | Both Windows applications use ApplicationModelRuntime / ApplicationUiSession / WinApplicationHost. Both native acceptance suites and the independent application-contract headless test pass. |
| One RAM storage class and a single USER file | USER/BACKING/scratch use ordinary RamDomainStorage instances. Child Close does not write files; shutdown writes user.bin. Single-file and both application persistence-boundary cases pass. No destructor/background flush. |
| Debug always starts fresh and prepares supply | Each regular Debug launch builds a new graph and does not load old USER. The same Debug executable supports distillation; Windows has no separate exporter. Both fresh-launch acceptance cases pass. Explicit restore consumers remain available for cold tests. |
| Automatic prefab cloning and external references | Public/application boundary_ids remain absent. Parent-domain ownership and constant scope determine shared references. Clone, hierarchy, scope-storage and cold round-trip suites pass. |
| Constant semantics and storage policy | ObjectScope::is_constant is semantic metadata; the existing supply-ID registry additionally selects BACKING-only storage and feeds the same constant classification. Its replacement remains a separate deferred library change, not an application-maintained clone boundary. |
| Active-language residency and formatted/empty text | Only active language leaves stay materialized after runtime capture/compaction. Localization/history tests pass; format placeholders and explicitly empty UiString values remain valid. |
| Immediate event collapse with history retained | Runtime retention remains zero events; base/journal/replay mechanisms and their versioning tests remain. No history subsystem was removed. |
| Initial window placement through model events | OS placement, native position/work-area reports, explicit placement and model centering remain covered. Signed negative coordinates remain valid; no monitor-selection field was added. |
| Current prototype schema | Application S0 stays unchanged. No update migration, recovery or extra file-format checks were introduced. Archive/I/O result handling remains at actual load/save boundaries. |
| Used components only | Bitmap, EditBox and Label remain removed with their dedicated tests. Button/list/window, localization and mixed library tests remain enabled. |
| Build and platform scope | Incremental Windows Debug only, AE_NO_DEBUG_LOG=ON and assertions enabled. No clean rebuild, new dependency population, MinSizeRel build or device/platform acceptance. |

### Acceptance and limitations

Final incremental build: **PASS**. Full CTest: **82/82 PASS**, no skipped or disabled tests, **187.87 seconds**. This includes Windows Catalog and MinimalApp acceptance, the expanded native-failure test, headless model/Close tests, single-file USER boundaries, clone/history/localization, and the Close timing diagnostic. No performance improvement is claimed from this correctness pass.

Logs: `.artifacts/principles-all-accepted-build.log`, `.artifacts/principles-all-accepted-tests.log`. Focused cold Close graph validation: `.artifacts/principles-all-close-graph-build.log`, `.artifacts/principles-all-close-graph-test.log`.

The preliminary run exposed an over-strict assumption that every desktop prefab has an in-content Close button. That regression was corrected by preserving the intentional optional component while requiring its internal links. The old-binary diagnostic was interrupted after this known failure; only the complete subsequent run above is final acceptance. No production source changes followed that final run.

No identified Windows/headless item from this audit remains open. The previously recorded other-platform normalization/probe callers, Android SyncPost lifetime and mobile/web window hierarchy are still outside the user's current platform scope. Distributed/resource-delivery/cache architecture and future supply-registry replacement remain deferred; this Windows acceptance does not certify them.

## Windows static release runtime and size — 2026-11-10 — complete

- Non-Debug MSVC configurations use static CRT/STL across application and dependency libraries; Debug keeps /MDd.
- Windows consumers select their required registration modules. The full library APIs remain compatible. Initial builders, bootstrap and legacy directory persistence are separated from runtime archive members.
- Final MinSizeRel EXEs import only KERNEL32/USER32 and pass four binary contracts excluding distillation, builder objects and unused concrete factories.
- WindowCatalog: 684,544 bytes (668.5 KiB); MinimalApp: 622,592 bytes (608 KiB). Pruning saves 24.5/18 KiB after static CRT integration. Static CRT is the reason for the net increase over the dynamic-runtime baseline.
- Validation: 82/82 Debug CTests and 17/17 targeted MinSizeRel CTests PASS, including native interaction/save/restart of the shipped applications. No clean build, user profile, dependency mutation or other-platform build.
- Full size attribution, A/B controls, temporary-directory correction and linker retry evidence: [validation report](validation/windows-static-runtime-size-2026-11-10.md).
