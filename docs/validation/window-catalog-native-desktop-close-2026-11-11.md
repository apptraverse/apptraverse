# Native desktop Close — 2026-11-11

Follow-up to `b32db56` on `feature/cross-platform-user-state`.

Desktop WindowCatalog windows use the native caption Close control. The
in-content Close button is presented only in paged mode (Android/Web/iOS).
Its model object and presenter remain in the portable graph. No serialized
fields, class identities or supply bytes changed.

`CatalogCloseButton::VisibleInCurrentPresentation()` reads the model's host mode.
The generic button presenter exposes that policy through
`EnabledForPresentation()`. The shared presentation lifecycle unloads disabled
presenters in reverse load order and activates them when enabled again.
`ReadyForPresentation()` still means a required parent is ready; enabled
presenters that cannot activate still fail the existing graph-contract check.
This is distinct from a disabled button's input state, which does not hide it.

The first implementation used readiness to suppress the button. The headless
test correctly rejected the remaining unactivated presenter; the implementation
was replaced with the explicit participation policy above.

## Verified

- Windows incremental Debug: control materialization (including conditional
  load/unload/reactivation), catalog headless and persistence boundary: **3/3
  PASS**. Headless checks desktop/paged policy before and after cold load.
- Actual Windows GUI, independently produced Android and Wasm USER: **both
  PASS**. The harness asserts no child control 5006, sends `SC_CLOSE` through
  the native window, checks the closed row, reopens and verifies save/restart.
- Linux incremental Debug: **16/16 PASS**, including GTK native close/save/cold
  restart acceptance (55.40 seconds total).
- Web actual Wasm embedded test, browser functional Close and full browser
  lifecycle: **PASS**. The in-page Close remains functional.
- Android x86-64 emulator, Debug load-only: **7/7 PASS**, 60.555 seconds, using
  `debugFresh=false`. An initial harness invocation omitted that argument and
  incorrectly expected fresh state; no application fix was needed. Original
  installed APK restored and default USER verified byte-identical afterward.
- Four-platform USER transfer/edit/return matrix: **52/52 PASS**; all 32
  read-only verifications retained their input hashes.
- All four generated supplies retain canonical SHA-256
  `1a1ff30bffde0a2b008d17166fef7ed544e8b29f18f1e8ef1ca980fca9b4402e`.
  Normalization is documented in the preceding portability validation.

Evidence is under `.artifacts/desktop-native-close-*`, including the matrix
`report.json`, Android `result.json` and instrumentation log, Windows native
reports and Linux/Web logs. The reusable Windows GUI harness is
`tools/window_catalog_portable_windows_test.py`.

The shared policy applies to macOS and preserves iOS paged presentation, but
Apple runtime tests were not executed on this host. No MinSizeRel build or
migration of older experimental profiles is claimed.
