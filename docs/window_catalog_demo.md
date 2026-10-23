# Window catalog demo (in progress)

Target: Windows catalog host with a main window (ListBox + Add/Open/Delete + EN/RU), lazy child `CatalogWindow` graphs per `WindowHolder`, persistence under `{exe_dir}/surfaces_catalog_state`.

## Holder / ObjPtr contract

- `WindowHolder` extends `UiListRow`; `primary_text` is the list name; `window` is `CatalogWindow::ptr`.
- Open: loaded `holder.window` without `kUnloadedByDefault`. Closed: valid unloaded ref. Deleted: row removed from `CatalogListContainer.rows`.
- `next_window_number` is monotonic across delete/restart.

## Current tree state

- **Done (stage 2–3):** CMake `surfaces_demo_window_catalog`, `apptraverse_window_catalog_model_test` (headless reflection + initial publication).
- **Done (stage 4):** `win32_window_catalog_demo` and `win32_window_catalog_demo_load_only` — Model thread loads `WindowCatalog`, initial/structural publication into UI Domain, library `WinListContainerPresenter` + four `WinPushButtonPresenter` on ~640×480 main HWND. Default state `{exe_dir}/surfaces_catalog_state`; `--state-dir` override. Main chrome is `CatalogWindow` at fixed id 2. WM_CLOSE on main window shuts down cleanly (model save, presenter unload).
- **Not started:** child-window close → `UserCloseWindow`, full Add/Open/Delete acceptance, switch `win32_surfaces_demo` to catalog.

## Reflection

`WindowCatalog` groups ObjPtr fields into `WindowCatalogTexts`, `WindowCatalogButtons`, and `WindowCatalogPresenters` (`AE_REFLECT_MEMBERS`) so full `AE_OBJECT_REFLECT` topology fits MSVC; v0 `Load`/`Save` wire order is unchanged.
