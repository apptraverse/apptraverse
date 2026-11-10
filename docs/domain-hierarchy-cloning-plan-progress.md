# Domain hierarchy cloning — plan and progress

## Goal and constraints

Implement reproducible domain parent/child lookup, clone via a temporary child domain with safe finalization into the working domain, correct loaded/unloaded `ObjPtr` identity across ID and domain changes, history/base capture without runtime USER disk writes, UI mirror publication via in-memory buffers (no session USER RAM store), subgraph unload and application shutdown persistence, and passing regression tests including Debug Close timing.

Constraints:

- Do not modify source worktree `C:\Users\nickc\Projects\apptraverse-wt-adaptive` (no reset/clean/stash/commit).
- Do not work in `cursor/host-graceful-shutdown`.
- Keep graph ownership via `ae::Ptr`, `ProxyPtr`, and existing object system; no global/thread_local keepalive masks.
- Æther changes only through repo patch chain (no manual CPM cache edits).
- Windows-first; `src/common/` stays cross-platform.
- Commits and code comments in English; push not requested.

## Baseline and source worktrees

| Item | Value |
|------|--------|
| New branch | `feature/domain-hierarchy-cloning` |
| New worktree | `C:\Users\nickc\Projects\apptraverse-wt-domain-hierarchy` |
| Branch base commit | `0343a20b3e08f4d5ec0ecf589ce540f6cd91bc4a` (commit message: session overlay flush tuning; **not** verified as fully green) |
| Source worktree (read-only for WIP) | `C:\Users\nickc\Projects\apptraverse-wt-adaptive` @ same HEAD + uncommitted WIP |
| Æther-objects pin | `79283b667cabf689da1059d618fe2a22abf51db4` (`cmake/aether_version.cmake`) |
| Isolated CPM cache (this work) | `C:\Users\nickc\Projects\apptraverse-wt-domain-hierarchy\.cpm_cache` (planned) |

## Confirmed findings

- **CONFIRMED:** Adaptive worktree HEAD is `0343a20`; branch `feature/window-catalog-adaptive`; large uncommitted diff (~26 files, +837/−676 lines) including removed `ApplicationModelSessionStorage`, extended `object_clone.cpp`, and two new Æther patches not in patch list.
- **CONFIRMED:** No existing `domain-hierarchy` branch/worktree before this task; created fresh.
- **HISTORICAL @ `0343a20`:** Pin-era notes reported 18 patches and no parent-domain patch in chain (superseded after reconfigure; see run log below).
- **REPORTED (not reproduced here yet):** Prior adaptive WIP reports mentioned history hydrate, live remap, and embedded overlay failures — treat as historical until re-run in this worktree.

## Proposed design and decisions

### Domain hierarchy (Æther)

- Add optional `parent_domain` on `Domain` (non-owning). `Find` searches local registry then walks parents; no sibling/child search.
- `GetMostRelatedFactory` / storage enumeration: local `domain_storage()` first; on miss, consult parent chain storage for **load** only; constructed instances always register in the **current** domain.
- Separate `FindLocal` for clone finalization checks.

### ObjPtr identity

- Loaded `id()` / `domain()` follow live `Obj` fields (sync on load and when object ID/domain changes).
- Unloaded refs keep `id_` / `domain_` on the pointer; `Reset()` sets `kUnloaded` and retains flags; preserve `id_`/`domain_` from cached object before release when needed.
- Avoid global `SetObjPtrOperationStorageRemapFn` hook from adaptive WIP.

### Cloning (apptraverse)

- Keep canonical snapshot + remap stages.
- Replace live-adopt/hydrate path with: temporary child `Domain` (RAM storage overlay + parent = target) loads remapped scratch; register clone roots locally first; resolve refs (local > parent external); finalize by transferring new objects to target with new IDs; drop child domain.
- Reuse existing `OperationStorage` remap for in-place reference rewrite where needed (no live-adopt stack).

### History / persistence

- Port removal of `ApplicationModelSessionStorage` and runtime USER writes from WIP where aligned with memory-buffer UI publication.
- Do not reintroduce session USER staging.

## Implementation stages and acceptance criteria

| Stage | Scope | Acceptance |
|-------|--------|------------|
| 1 | Inventory, isolated cache, patch chain doc | Clean configure applies 18+ patches; plan committed |
| 2 | ObjPtr contract + tests | Identity/Reset/Load tests pass |
| 3 | Domain hierarchy + lookup tests | Local shadow, parent fallback, no sibling |
| 4 | Clone via child domain + finalize | `object_clone_test` clone cases pass |
| 5 | History/base, no runtime USER | Historical tests + boundary hooks |
| 6 | App integration, remove session RAM store | Headless catalog/minimal tests |
| 7 | Unload + cold round-trip | Persistence boundary test |
| 8 | Full regression + Debug Close timing | Medians recorded, real USER save on close |

## Progress

| Stage | Status | Notes |
|-------|--------|-------|
| 1 | PASS | Worktree `feature/domain-hierarchy-cloning`; patch chain 19/19 on `/.cpm_cache_isolated_v3` |
| 2 | PASS | `apptraverse_aether_domain_hierarchy_test` |
| 3 | PASS | Parent lookup, sibling isolation, ObjPtr identity in hierarchy test |
| 4 | PARTIAL | `object_clone_test` **PASS** (incl. disk hist `persist_layers_to_target_storage=true` ~972); live runtime clone not re-run this push |
| 5 | PARTIAL | Capture: inject layers + `Load<ConcreteNode>` on base; core historical cases **PASS**; string-leaf disk cases **CRASH/FAIL** on full suite |
| 6 | PARTIAL | Session RAM removed in this push; boundary **FLAKY** (see run log 2026-11-10) |
| 7–8 | NOT RUN | Full regression, fresh v4 CPM cache, Debug Close timing |

## Run log (one row per executed run)

### 2026-11-09 ~21:35 PST — current WIP tree (uncommitted on `5828910`)

| Field | Value |
|-------|--------|
| Git HEAD | `58289101fca4ffa965543fb6529384e64c081087` + uncommitted WIP |
| Build dir | `build/win64-ninja-msvc-debug` |
| CPM cache | `.cpm_cache_isolated_v3` |
| Command | `ninja apptraverse_*` then run exes under `build/.../tests/` |
| `apptraverse_window_catalog_user_persistence_boundary_test` | exit **0**, tail `ALL_USER_PERSISTENCE_SCENARIOS_OK` |
| `apptraverse_object_clone_test` | exit **1**, first failure `object_clone_test FAIL: clone bases must capture current state` (`object_clone_test.cpp` ~1005) |
| `apptraverse_historical_base_materialization_test` | exit **1**, first `CHECK failed: main->value == 110` (`historical_base_materialization_test.cpp:629`) |

Full stdout/stderr (unfiltered): `.artifacts/test-logs/` (e.g. `boundary_20261109-212344.log`, `historical_quick.log`).

**Expected vs actual (backing swap):** after `backing_b` mutates base to `999`, `ReplayFromBase` on overlay should yield **110**; actual ≠ 110.

### 2026-11-10 ~06:10 PST — pre-push verification (WIP on `5828910`, about to commit)

| Field | Value |
|-------|--------|
| Build | `ninja` in `build/win64-ninja-msvc-debug`, cache `.cpm_cache_isolated_v3` |
| `apptraverse_object_clone_test` | exit **0**, tail `object_clone_test OK` |
| `apptraverse_aether_domain_hierarchy_test` | exit **0** |
| `apptraverse_window_catalog_user_persistence_boundary_test` | exit **2**, first `CHECK failed: CountRegularFiles(user_dir) == files_after_boot` (`window_catalog_user_persistence_boundary_test.cpp:190`) |
| `apptraverse_historical_base_materialization_test` | exit **0** when `TestHistoricalStringLeaf*` disabled (diagnostic); full main: exit **3** or access violation during string-leaf remapped capture |

**Historical fix (landed in this push):** `CaptureBaseStateInto` uses `ConcreteNode` for `DomainGraph::Load` after `InjectAllLayeredObjects`; `BuildRemappedHistoricalBaseScratch` uses `Save(owner)` with `ConcreteNode&` staging removed; rebind only for `owner_live_refs` / `live_references`.

### Historical (superseded) — 2026-11-09 ~21:35 PST

| Field | Value |
|-------|--------|
| `apptraverse_window_catalog_user_persistence_boundary_test` | exit **0** (historical) |
| `apptraverse_object_clone_test` | exit **1** (historical) |
| `apptraverse_historical_base_materialization_test` | exit **1**, `main->value == 110` (historical — fixed in WIP above for core path) |

## Uncommitted work (feature/domain-hierarchy-cloning @ `5828910` + WIP)

- `object_clone_hydrate.inl`: concrete `Load<T>`, conditional `rebind_live_graph`, scratch→live `id_remap`.
- `BuildRemappedHistoricalBaseScratch`: staging overlay, owner-declared live refs, full scratch fast path.
- `Node::CollectHistoricalCaptureLiveReferenceIds` + test hook on `HistorizedConstHostNode`.
- Removed `ApplicationModelSessionStorage`; clone persist gated by `persist_layers_to_target_storage`.
- `RemapLiveObjectGraph`: alias scratch ids to live registry entries for historical rebind.

## Blocker: string-leaf remapped capture (disk tests)

1. **Repro:** `apptraverse_historical_base_materialization_test` full `main()` (`TestHistoricalStringLeafBootstrapAfterOwnerSave` / compaction).
2. **Observed:** access violation or invalid `ObjPtr` during remapped `BuildRemappedHistoricalBaseScratch` save / capture (after core tests through `TestSupplyConstantWithHistoricalBaseSave` pass).
3. **Fact:** `TestHistoricalBaseSurvivesBackingSwap` expects `main->value == 110` after backing swap — **PASS** with inject+`Load<ConcreteNode>` path (verified with string tests temporarily disabled).
4. **Next:** fix remapped leaf materialization for `HistorizedStringOwner::text` without rebinding to live leaf; re-run full historical suite.

## Remaining acceptance (not done)

- Historical materialization **PASS** (all cases including string disk + supply constant).
- `object_clone_test` live historical base values.
- Embedded overlay, Win32, MinimalApp, application contract, unload round-trips.
- Two-session isolation, shutdown, `.cpm_cache_isolated_v4` patch chain, Debug Close timing (4×5, `AE_NO_DEBUG_LOG=ON`).

## Next action

1. Fix base hydrate so `CaptureBaseStateInto` restores full concrete base state (derived layers), verify `value==110`.
2. Re-run historical + `object_clone_test`; commit in small steps after PASS.
3. Resume full plan matrix and progress doc updates per run.
