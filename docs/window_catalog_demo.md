# Window catalog demo

**Ветка:** **`integration/unified-application`**. Планы: [versioning/PLAN.md](versioning/PLAN.md), [versioning/PROGRESS.md](versioning/PROGRESS.md), [integration/application-versioning.md](integration/application-versioning.md).

Windows catalog host: main window (ListBox + Add/Open/Delete + EN/RU), lazy child `CatalogWindow` per `WindowHolder`, persistence under `{exe_dir}/surfaces_catalog_v0_state` (override `--state-dir`).

## Executables

- `win32_window_catalog_demo` — bootstrap + run (`APPTRAVERSE_ENABLE_DISTILLATION`).
- `win32_window_catalog_demo_load_only` — load existing profile only (no distillation; missing/empty profile is fatal).

Build (Release, unified worktree example):

`build/win64-ninja-msvc-release/examples/surfaces_demo/windows/win32_window_catalog_demo.exe`

Manual profile (U2): `--state-dir` e.g. `manual-test-state/window-catalog` under the worktree — **not** shared with CTest temp dirs.

## Layer / v0 contract (2025-10-23)

- `UiListContainer` v0: `rows` + `selection_index` (base layer only) — **selection restores on restart** with list rows and catalog `selected` holder ref.
- `CatalogListContainer` / `WindowHolder` derived Save/Load: `base_` + own fields only (`catalog`; `window_number`, `window`).
- ListBox titles come from stored `UiListRow.primary_text` (no `RepairHolderPrimaryText` on load).
- Shutdown: `SaveWindowCatalogPersistentSnapshot` (holder `primary_text` + open windows + `catalog.Save()`) then `SavePersistentModelSnapshotToStorage` (writes `persistent_model_checkpoint.bin` under `--state-dir`, including `AppendUnloadedCheckpointTargets` for closed child windows).
- Startup (`WindowCatalogModelSession::Run`): `LoadApplication` then, when the sidecar exists, `LoadPersistentModelSnapshot` from that file (same domain/storage/root) before localization repair and initial publication — restores Node `base`/journal so post-reload `Commit` (Add/Open) works.

## Verified (unified branch)

| Check | Status | SHA / notes |
| --- | --- | --- |
| **U1** `apptraverse_window_catalog_model_test` Release CTest | **PASS** | **`8835d26`** — tests use `LoadApplication` + checkpoint like production |
| **U2** `win32_window_catalog_demo` manual | **manual PASS** | User: Add/Open/Delete, EN/RU, geometry, restart; **not** full project regression |
| `apptraverse_window_catalog_win32_acceptance_test` | PASS (pre-unified reports) | load-only → Open → Add → restart; re-run on unified — **U6** |

## Removed 275e099 workarounds

- Duplicate list/holder serialization in derived classes, `SyncCatalogListTitlesFromMirror`, `RepairHolderPrimaryText`, `MarkRowsMaterialized`, distillation on load_only.

## Holder / ObjPtr contract

- `WindowHolder` stays in list until Delete; `window.is_loaded()` = open HWND path.
- Reopen closed holder: `OpenHolderWindow` uses `DomainGraph::LoadRoot` when `ObjPtr::Load()` does not hydrate after checkpoint reload.
