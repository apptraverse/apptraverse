# Window catalog demo

Windows catalog host: main window (ListBox + Add/Open/Delete + EN/RU), lazy child `CatalogWindow` per `WindowHolder`, persistence under `{exe_dir}/surfaces_catalog_state` (override `--state-dir`).

## Executables

- `win32_window_catalog_demo` — bootstrap + run (`APPTRAVERSE_ENABLE_DISTILLATION`).
- `win32_window_catalog_demo_load_only` — load existing profile only.

Build (Release example): `build-presenter-decomp-msvc-rel/examples/surfaces_demo/windows/win32_window_catalog_demo.exe`

## Verified behavior (Release)

- EN/RU: `UiString` slots persisted before materialize; chrome and list rows show full text both ways; selection preserved across language switch.
- Titles: main `Window Catalog` / `Каталог окон` via `main_window_presenter->title`; child titles share `WindowHolder::primary_text`.
- Child WM_CLOSE: model `UserCloseWindow` → unload graph → structural publication → HWND destroyed; holder remains in list.
- Open: closed window loads and publishes; open window sets `pending_focus_window_id` → UI restore/focus after publication (no duplicate HWND).
- Restart: `SaveWindowCatalogPersistentSnapshot` before exit; load-only process restores open children’s HWND via initial publication.
- Tests: `apptraverse_window_catalog_model_test`, `apptraverse_window_catalog_win32_acceptance_test` (label `window_catalog`).

## Holder / ObjPtr contract

- `WindowHolder` in list until Delete; `window.is_loaded()` = open; valid+unloaded = closed.
- Main ListBox uses `primary_text` only (closed windows stay unloaded in model).

## Reflection

`WindowCatalog` groups ObjPtr fields (`WindowCatalogTexts`, `WindowCatalogButtons`, `WindowCatalogPresenters`) for full MSVC `AE_OBJECT_REFLECT`; v0 wire order unchanged.
