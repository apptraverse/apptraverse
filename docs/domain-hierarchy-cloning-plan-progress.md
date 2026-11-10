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
- **CONFIRMED:** Pinned patch chain has 18 patches; **no** parent-domain patch in chain yet.
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
| 1 | IN PROGRESS | Worktree created; plan file added |
| 2–8 | NOT RUN | |

## Validation log

| Command | Config | Exit | Result |
|---------|--------|------|--------|
| `git rev-parse HEAD` (new worktree) | `feature/domain-hierarchy-cloning` | 0 | `0343a20` |
| `git status -sb` (adaptive, read-only) | — | 0 | Dirty WIP present; untouched by agent |

## Remaining issues

- Full implementation and test matrix pending.
- Adaptive WIP live-adopt path explicitly excluded unless re-derived via child domain.

## Next action

1. Populate isolated CPM cache via CMake configure in new worktree.
2. Author `aether-objects-domain-hierarchy-v1.patch` and ObjPtr identity patch if not covered.
3. Implement child-domain clone finalization in `object_clone.cpp`.
4. Port selective WIP (session storage removal, persistence boundary tests) without live-adopt.
