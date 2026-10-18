# surfaces_demo acceptance artifacts

BMP screenshots are written by `apptraverse_surfaces_win32_smoke_test` when run
from a configured build (see `APPTRAVERSE_SURFACES_ACCEPTANCE_DIR` in
`tests/CMakeLists.txt`).

Release acceptance (2026-10-18, branch `cursor/surfaces-presenter-decomposition`):

- Clean CPM cache under `build-presenter-decomp-msvc-rel/` with aether-objects
  pinned at `79283b667cabf689da1059d618fe2a22abf51db4` and a single patch
  `cmake/patches/aether-objects-local-persistent-load.patch`.
- `apptraverse_surfaces_model_test`, `apptraverse_surfaces_win32_smoke_test`,
  and `apptraverse_directory_domain_storage_test` pass in Release (NDEBUG).
- `win32_surfaces_demo` / `win32_surfaces_demo_load_only` build without the
  legacy cf16 control cache.

Filenames from smoke:

- `ru_wide_surface1.bmp`, `ru_wide_surface2.bmp` — two windows on RU
- `layout_wide_three_buttons.bmp`, `layout_narrow_three_buttons.bmp` — chrome layout
- `load_only_restored_surface1.bmp`, `load_only_restored_surface2.bmp` — cold
  `win32_surfaces_demo_load_only` after RU session (no distill bootstrap).
  Smoke resolves the load-only binary via `$<TARGET_FILE:win32_surfaces_demo_load_only>`
  (Debug smoke runs the Debug exe, Release smoke runs the Release exe).

`TestLoadOnlyRestoresRuProfile` in `surfaces_win32_smoke_test.cpp`:

- Finds restored windows by **child PID + window class + title** (`Поверхность 1`
  and `Поверхность 2`), not the first visible surface HWND.
- Compares saved/restored window geometry, RU then EN chrome on both surfaces
  (button text via `WM_GETTEXT` on control IDs), adds Surface 3 on RU, then
  checks persisted `language_index` after a normal exit.

Use a dedicated `--state-dir` under `%TEMP%` for manual demo/load_only runs;
do not overwrite user profiles under the repo.
