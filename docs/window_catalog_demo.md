# Window catalog demo (in progress)

Target: Windows `win32_surfaces_demo` with a main catalog window (ListBox + Add/Open/Delete + EN/RU), lazy child `CatalogWindow` graphs per `WindowHolder`, persistence under `{exe_dir}/surfaces_catalog_state`.

## Holder / ObjPtr contract

- `WindowHolder` extends `UiListRow`; `primary_text` is the list name; `window` is `CatalogWindow::ptr`.
- Open: loaded `holder.window` without `kUnloadedByDefault`. Closed: valid unloaded ref. Deleted: row removed from `CatalogListContainer.rows`.
- `next_window_number` is monotonic across delete/restart.

## Current tree state

- **Done (stage 2–3):** CMake targets `surfaces_demo_window_catalog` and `apptraverse_window_catalog_model_test` (CTest label `window_catalog`, not part of default full sweep). Model/graph compile; `tests/window_catalog_model_test.cpp` passes Release (`Add`/`Open`/`Close`/`Delete`, monotonic numbers, `DirectoryDomainStorage` roundtrip, 100 holders / 10 loaded, full reflection topology via `ForEachGraphEdgeObjPtrOn`, headless `SerializeInitialPublication` / `LoadInitialPublication` with closed child absent from model and UI). Main chrome window is `CatalogWindow` at fixed id 2 so initial publication hydrates the final stored class.
- **Done:** Stage 1 test revert (`27dee80`). List selection uses `UiListContainer.selection_index` (not locale index).
- **Not started:** `window_catalog_lifecycle`, `WinApp`/`main` switch, WM_CLOSE host wiring, acceptance test, `load_only` profile `surfaces_catalog_state`.

## Reflection

`WindowCatalog` groups ObjPtr fields into `WindowCatalogTexts`, `WindowCatalogButtons`, and `WindowCatalogPresenters` (`AE_REFLECT_MEMBERS`) so full `AE_OBJECT_REFLECT` topology fits MSVC; v0 `Load`/`Save` wire order is unchanged.
