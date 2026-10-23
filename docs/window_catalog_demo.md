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
- Shutdown: `SaveWindowCatalogPersistentSnapshot` (holder `primary_text` + open windows + `catalog.Save()`) then `SavePersistentModelSnapshotToStorage` (includes `AppendUnloadedCheckpointTargets` for valid unloaded child windows, e.g. closed catalog children).

## Verified (Release, incremental build)

- `apptraverse_window_catalog_model_test` — **PASS** (incl. UI mirror row titles after `SerializeInitialPublication` / `LoadInitialPublication`).
- `apptraverse_window_catalog_win32_acceptance_test` — **FAIL** in this branch run: `ExpectListRows` after load_only and/or reopen closed window 2 (Open chrome vs list-activate race); restart list rows were observed **PASS** mid-fix before regressions tied to rebuild/link lock.

## Removed 275e099 workarounds

- Duplicate list/holder serialization in derived classes, `SyncCatalogListTitlesFromMirror`, `RepairHolderPrimaryText`, `MarkRowsMaterialized`, distillation on load_only.

## Holder / ObjPtr contract

- `WindowHolder` stays in list until Delete; `window.is_loaded()` = open HWND path.
- Reopen closed holder: `OpenHolderWindow` uses `DomainGraph::LoadRoot` when `ObjPtr::Load()` does not hydrate after checkpoint reload.
