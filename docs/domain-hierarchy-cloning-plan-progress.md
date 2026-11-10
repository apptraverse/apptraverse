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
