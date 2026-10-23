# Window catalog demo

Windows catalog host: main window (ListBox + Add/Open/Delete + EN/RU), lazy child `CatalogWindow` per `WindowHolder`, persistence under `{exe_dir}/surfaces_catalog_state` (override `--state-dir`).

## Executables

- `win32_window_catalog_demo` — bootstrap + run (`APPTRAVERSE_ENABLE_DISTILLATION`).
- `win32_window_catalog_demo_load_only` — load existing profile only (`APPTRAVERSE_ENABLE_DISTILLATION`).

Build (Release example): `build-presenter-decomp-msvc-rel/examples/surfaces_demo/windows/win32_window_catalog_demo.exe`

## Verified behavior (Release, 2025-10-23)

- Shutdown: `BeginSaveSession` → `SaveWindowCatalogPersistentSnapshot` → `SavePersistentModelSnapshotToStorage`; process exit code 0 required.
- Persistence v0: `CatalogListContainer` saves `rows` + `selection_index`; `WindowHolder` saves `primary_text` / list row fields with `window_number` + `window`.
- Load repair: `RepairWindowCatalogEmbeddedStrings` rebuilds holder list titles from `window_title_prefix` + `window_number` when UTF-8 slots are empty after load.
- Model restart path: `TestCatalogRuntimeSavePreservesOpenWindowLoadedState` (bootstrap → load → add/close → shutdown save → reload) **PASS**.
- Win32 acceptance: restart scenario (close child 2, load_only, open 2, add 4, second demo) **FAIL** at load_only ListBox (see boundary below).
- EN/RU + focus: covered in `RunLanguageAndFocusAcceptance` (separate temp profile); unchanged from prior commit when run after restart block.

## Known gap (first proven boundary on FAIL)

- **After shutdown save**, read-only `LoadApplication` probe shows **model** `list->rows.size()==3` and stable ObjIds.
- **load_only** UI after `LoadInitialPublication` + presenters: ListBox does not show full `Window 1/2/3` row set (boundary **E**: UI mirror / list presenter vs complete model list on subprocess restart).

## Holder / ObjPtr contract

- `WindowHolder` in list until Delete; `window.is_loaded()` = open; valid+unloaded = closed.
- Main ListBox uses `primary_text` (repaired on load when needed).

## Reflection

`WindowCatalog` groups ObjPtr fields (`WindowCatalogTexts`, `WindowCatalogButtons`, `WindowCatalogPresenters`) for full MSVC `AE_OBJECT_REFLECT`; v0 wire includes list `rows` and holder `primary_text`.
