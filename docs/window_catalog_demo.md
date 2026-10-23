# Window catalog demo (in progress)

Target: Windows `win32_surfaces_demo` with a main catalog window (ListBox + Add/Open/Delete + EN/RU), lazy child `CatalogWindow` graphs per `WindowHolder`, persistence under `{exe_dir}/surfaces_catalog_state`.

## Holder / ObjPtr contract

- `WindowHolder` extends `UiListRow`; `primary_text` is the list name; `window` is `CatalogWindow::ptr`.
- Open: loaded `holder.window` without `kUnloadedByDefault`. Closed: valid unloaded ref. Deleted: row removed from `CatalogListContainer.rows`.
- `next_window_number` is monotonic across delete/restart.

## Current tree state

- **Done:** Stage 1 test revert (`27dee80`). List selection uses `UiListContainer.selection_index` (not locale index).
- **WIP (not wired to EXE):** `examples/surfaces_demo/common/window_catalog_model.*`, `window_catalog_graph.cpp`, localization, registration; model test source present but not in CMake until compile issues are fixed.
- **Not started:** `window_catalog_lifecycle`, `WinApp`/`main` switch, WM_CLOSE host wiring, acceptance test, `load_only` profile `surfaces_catalog_state`.

## Blocker

Catalog graph registration / reflect graph hits MSVC limits and namespace wiring; lifecycle and UI integration require the next iteration before a runnable EXE.
