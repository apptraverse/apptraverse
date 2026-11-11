# Remaining Windows and headless contract corrections

Date: 2026-11-10. Base: `06150513a740c065064732747d418afe1bc0b6fc`.
Branch: `feature/domain-hierarchy-cloning`.

## Changes

1. Moved the `SetWindowCatalogMobileActiveEvent` registrar and graph walker into
   a separate archive member. Full catalog registration includes that member;
   desktop registration does not. The headless mobile scenario retains its
   implementation and registration. No model schema or history mechanism changed.
2. Removed the constructor default for the serialized list-presenter selection
   notification flag. Both application builders already set it explicitly; the
   isolated UI command and native-failure fixtures now do so as well. Contract
   test model/event fields no longer have in-class defaults. The two test graph
   builders initialize model values before initial history capture.
3. Removed the headless contract presenter's silent return for a missing required
   desktop-window link. It now consumes the same complete-graph contract as the
   Windows presenter.
4. Headless Catalog, MinimalApp, and application-contract tests register once in
   main. Their entry points include core and required common UI registration.
   Removed per-scenario calls, registration from the headless Catalog builder,
   and the extra broad registration in the MinimalApp unload round-trip test.

## Binary regression evidence

Before rebuilding, the expanded binary check rejected the existing catalog EXE
from the base revision with exit 1:

    FAIL: Unused concrete factory: SetWindowCatalogMobileActiveEvent

After rebuilding, all four normal/load-only MinSizeRel binary contracts pass.
They reject the mobile registration member and factory in addition to the
previous distillation, builder, broad-registration, and unused-factory checks.
The success message now describes the specific forbidden objects/factories
checked, rather than claiming that every possible unused class was excluded.

| Normal application | Before | After | Difference | DLL imports |
|---|---:|---:|---:|---|
| WindowCatalog | 684,544 B | 681,984 B (666 KiB) | -2,560 B | KERNEL32, USER32 |
| MinimalApp | 622,592 B | 622,592 B (608 KiB) | 0 B | KERNEL32, USER32 |

This is a registration/link-boundary change. Shared model methods and historical
event support remain; this does not claim that all mobile-related type names or
all theoretically removable instructions disappear from the desktop binary.

## Executed validation

- Existing Windows Debug incremental build: PASS, `AE_NO_DEBUG_LOG=ON`, assertions
  enabled. Debug applications regenerated the embedded supply successfully.
- Full Debug CTest run: **82/82 PASS**, no skips, 288.88 seconds. This includes
  application-contract, both application headless suites, the headless mobile
  case, Windows native acceptance, Close, history, clones, localization and USER
  persistence boundaries.
- Existing MinSizeRel incremental build: PASS, static MSVC runtime retained.
- Targeted MinSizeRel CTest run: **17/17 PASS**, no skips, 59.58 seconds. Includes
  all four binary contracts and both native save/restart acceptance suites.
- `git diff --check`: PASS.

Logs and inspection JSON are in the ignored `.artifacts/principles-followup/`
directory: `debug-build.log`, `debug-ctest.log`, `min-build.log`,
`min-ctest.log`, `catalog-binary.json`, and `minimal-binary.json`.
Tests used separate `.artifacts/td` and `.artifacts/tm` temporary roots.
Debug links used `_LINK_=/INCREMENTAL:NO`; compilation remained incremental.
The MinSizeRel build overlapped part of the Debug test run; no performance
comparison is inferred from test duration or the Close diagnostic.

No clean build, dependency replacement, user profile change, adaptive WIP change,
or device-platform validation was performed. The four reported follow-up items
are complete; this is not a new repository-wide conformity certification.
