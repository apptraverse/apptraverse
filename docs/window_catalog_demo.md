# Window catalog demo

Windows catalog host: main window (ListBox + Add/Open/Delete + EN/RU), lazy child `CatalogWindow` per `WindowHolder`, persistence under `{exe_dir}/surfaces_catalog_state` (override `--state-dir`).

## Executables

- `win32_window_catalog_demo` — bootstrap + run (`APPTRAVERSE_ENABLE_DISTILLATION`).
- `win32_window_catalog_demo_load_only` — load existing profile only (no distillation; missing/empty profile is fatal).

Build (Release example): `build-presenter-decomp-msvc-rel/examples/surfaces_demo/windows/win32_window_catalog_demo.exe`

## Layer / v0 contract (2025-10-23)

- `UiListContainer` v0: `rows` + `selection_index` (base layer only).
- `CatalogListContainer` / `WindowHolder` derived Save/Load: `base_` + own fields only (`catalog`; `window_number`, `window`).
- ListBox titles come from stored `UiListRow.primary_text` (no `RepairHolderPrimaryText` on load).
- Shutdown: `SaveWindowCatalogPersistentSnapshot` (holder `primary_text` + open windows + `catalog.Save()`) then `SavePersistentModelSnapshotToStorage` (writes `persistent_model_checkpoint.bin` under `--state-dir`, including `AppendUnloadedCheckpointTargets` for closed child windows).
- Startup (`WindowCatalogModelSession::Run`): `LoadApplication` then, when the sidecar exists, `LoadPersistentModelSnapshot` from that file (same domain/storage/root) before localization repair and initial publication — restores Node `base`/journal so post-reload `Commit` (Add) works.

## Verified (Release, incremental build)

- `apptraverse_window_catalog_model_test` — **PASS** (checkpoint reload + Add via `NotifyClicked` after reopen closed holder).
- `apptraverse_window_catalog_win32_acceptance_test` — **PASS** (load-only → Open → Add → restart; geometry + holder ObjId checks).

## Removed 275e099 workarounds

- Duplicate list/holder serialization in derived classes, `SyncCatalogListTitlesFromMirror`, `RepairHolderPrimaryText`, `MarkRowsMaterialized`, distillation on load_only.

## Holder / ObjPtr contract

- `WindowHolder` stays in list until Delete; `window.is_loaded()` = open HWND path.
- Reopen closed holder: `OpenHolderWindow` uses `DomainGraph::LoadRoot` when `ObjPtr::Load()` does not hydrate after checkpoint reload.
