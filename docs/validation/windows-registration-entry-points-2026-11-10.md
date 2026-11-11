# One registration entry point per Windows application

Date: 2026-11-10. Base: `1e620be4da67e7d43b5f601ce644e0fa9b54d0ad`.
Branch: `feature/domain-hierarchy-cloning`.

## Startup contract

Each Windows executable calls its application registration function exactly once
from `main`, before argument dispatch, model startup or graph loading:

| Application | Entry point | Dependencies registered by that entry point |
|---|---|---|
| WindowCatalog | `EnsureWindowCatalogGraphRegistration()` | Core, desktop catalog model/actions/events, Windows controls and catalog presenters |
| MinimalApp | `RegisterMinimalApplicationTypes()` | Core, Windows controls and MinimalApp model/controls/events |

The Windows control registration already includes its common UI dependency, so
neither application also calls `EnsureUiControlRegistration`. Core registration
includes the noninteractive CRT setup previously repeated in `main`.

Normal startup, Debug `--distill-only`, and the load-only executable all use the
same entry point. Registration calls were removed from both model sessions,
both distillation functions and the Windows catalog graph builder. MinimalApp's
entry point is in the static `minimal_app_windows` library shared with its
acceptance tests; this library has no dependency on graph-building/distillation
sources. WindowCatalog reuses its existing platform graph-registration entry
point rather than adding another wrapper.

Internal library registration anchors and full registration APIs remain for
other consumers. The class selection is still explicit; this change does not
introduce automatic registration generation or alter serialization/schema.
Other platform implementations were not changed.

## Tests exercise the same contract

Windows catalog model, coalescing, chrome, disk-stage, persistence-boundary,
embedded-overlay and native acceptance executables now register once in `main`.
Their per-scenario full core/UI/model registration calls were removed. The two
MinimalApp Windows acceptance/export executables likewise use the production
entry point once. Cold-load probes therefore cannot silently compensate for an
incomplete application entry point with extra registration calls.

The four non-Debug binary-contract tests additionally reject the full
`object_registration.cpp` and `window_catalog_registration.cpp` archive members.
Existing checks still reject distillation/builders/directory-storage members,
the standalone `UiListRowPresenter` / `WinWindowPresenter` / mobile
`CatalogCloseButton` factories, and dynamic CRT/project-runtime imports.

## Executed validation

- Existing Windows Debug incremental build: **PASS**, `AE_NO_DEBUG_LOG=ON`,
  assertions enabled. Both Debug applications regenerated their embedded supply
  successfully through `--distill-only` after registration moved to `main`.
- Full Debug CTest suite: **82/82 PASS**, 212.49 seconds.
- Existing Windows MinSizeRel incremental build: **PASS**, static MSVC runtime.
- Targeted MinSizeRel CTest suite: **17/17 PASS**, 44.31 seconds. This includes
  four binary contracts, both native interaction/save/restart suites, application
  contract/headless, clone, scopes, single-file storage, embedded overlays and
  unload cold round-trips.
- `git diff --check`: **PASS**.

| Normal MinSizeRel executable | Bytes | Change from base | DLL imports |
|---|---:|---:|---|
| WindowCatalog | 684,544 (668.5 KiB) | 0 | KERNEL32, USER32 |
| MinimalApp | 622,592 (608 KiB) | 0 | KERNEL32, USER32 |

The normal and load-only EXEs all pass the binary contracts. Embedded supply
data and readers remain; distillation producer code and the excluded factories
do not enter these release binaries.

Logs and JSON inspection results are retained locally in the ignored
`.artifacts/registration/` directory: `debug-build.log`, `debug-ctest.log`,
`min-build.log`, `min-ctest.log`, `catalog-binary.json`, `minimal-binary.json`.
Tests used separate writable `.artifacts/td` and `.artifacts/tm` temporary roots.
Debug links used `_LINK_=/INCREMENTAL:NO` to avoid the previously observed stale
MSVC incremental-link state; compilation remained incremental. No clean build,
dependency replacement, user profile or other-platform build was used.
