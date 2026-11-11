# Close control belongs to presentation — 2026-11-11

Follow-up to `76a91ef` on `feature/cross-platform-user-state`.

## Change

Removed `UiPushButton::VisibleInCurrentPresentation` and the catalog button's
`mobile_pager` override. `UiPushButtonPresenter::EnabledForPresentation` now
asks its linked window presenter through `PresentsChildControl`.

The existing Windows/Linux/macOS catalog window presenters exclude the child
identified by `CatalogWindow::close_button_presenter`. This is the existing
prefab link, compared by actual object identity after clone/load, not a numeric
control ID. Native caption Close supplies the action. Android/Web/iOS retain
the default content-control policy. Both input paths retain the common model
Close action and existing stale-input handling.

No registration IDs, serialized fields, prefab composition or USER schema
changed. No runtime graph repair or manual wiring was introduced. Presenter
participation remains separate from parent readiness and button enabled state.

## Verified

- Windows incremental Debug: 4/4 focused tests (40.59 s), including control
  materialization, catalog headless, persistence boundary and embedded overlay.
- Android embedded graph on Windows: 1/1 PASS (1.12 s).
- Actual Windows GUI import from previous Android and Web USER: both PASS;
  absent in-content Close, native caption close, reopen, save and cold restart.
- Added actual desktop factory checks with both values of `mobile_pager`,
  ordinary main controls, and cold reloading. Headless generic presentation
  includes the button regardless of model mode. Android/Web embedded tests
  additionally assert their button remains enabled with desktop model mode.

- Web: actual Wasm under Node, browser functional Close with two cold restarts,
  and headed lifecycle suite PASS (13.000 s for lifecycle).

- Android x86-64 emulator: 7/7 instrumentation PASS (71.092 s). Original
  installed APK restored byte-identically; default USER hash unchanged.

- Four-platform transfer/edit/return matrix: 52/52 PASS, all 32 read-only
  checks retained their USER hashes.
- Generated supplies remain identical to the previous revision on all four
  platforms: canonical SHA-256
  `1a1ff30bffde0a2b008d17166fef7ed544e8b29f18f1e8ef1ca980fca9b4402e`.

- Linux incremental Debug: 16/16 PASS (53.91 s), including actual GTK
  close/save/cold restart acceptance (49.27 s). Test-owned Xvfb/Openbox and
  the Web test server were stopped after acceptance.

## Scope

This change removes platform selection of the Close **control** from the model.
It does not claim to remove all host-mode-dependent navigation: `mobile_pager`
still governs active-page selection, swipe navigation and predecessor selection
on Close. A broader redesign needs platform-neutral navigation semantics rather
than moving those business decisions into native presenters.

Apple source paths are updated consistently, but macOS/iOS execution is not
available on this host. MinSizeRel was not rebuilt for this change. The user's
running application and profiles are not used by these isolated tests.

Evidence: `.artifacts/presenter-close-policy-*` (build/test logs, native Windows
reports, supply hashes, platform matrix and emulator preservation records).
