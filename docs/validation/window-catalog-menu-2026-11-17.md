# Windows WindowCatalog menu acceptance — 2026-11-17

Scope: current Windows Debug, v143 14.44 / Win7 minimum target, matching fresh
S0 supplies. Other platforms, MinSizeRel and benchmarks were not executed.

## Result

Only Theme and Language groups remain in the Windows application menu. Theme
offers From system / Light / Dark / Soft dark; Language offers English / Русский.
All settings/demo windows and local font/size/bold/keyboard-cue overrides were
removed, including their classes, Events, registration, builders and native
presenter hooks. System-derived font/layout behavior remains automatic.

The menu definition, labels and semantic commands are distilled model objects.
Windows only projects HMENU and sends model intentions through ModelObjectProxy.
Appearance and language use existing Events and LOCAL storage. The menu builder
can omit Theme for a native-following adapter; this is a tested authoring policy,
not a claim that an Apple/Android/Web port was implemented.

## Executed

- Incremental build: `win32_window_catalog_demo`,
  `win32_window_catalog_demo_load_only`, menu model, native chrome and Windows
  acceptance targets PASS, including embedded supply generation.
- `apptraverse_window_catalog_menu_model_test`: PASS. Adapter-applied and
  native-following menu definitions, LOCAL language scope, active-language lazy
  strings, immediate Event folding, invalid absent-theme command, and native
  high contrast palette priority over every appearance choice.
- `apptraverse_window_catalog_win32_chrome_adaptive_test`: PASS, explicit process
  exit 0 and `window_catalog_win32_chrome_adaptive_test OK`. Includes native menu
  checks after publication, independent client preferences, localized menu,
  absence of the language content HWND, adaptive layout, native notifications,
  placement, and preservation of native controls during position-only changes.
- `apptraverse_window_catalog_win32_acceptance_test`: PASS, explicit process exit
  0 and `window_catalog_win32_acceptance_test OK`. Includes fresh Debug policy,
  embedded load-only restore, existing window lifecycle/focus cases and required
  object/schema negatives. New menu scenario verifies actual process cold
  restore of Russian + soft dark in LOCAL, absence of language in portable USER,
  and a USER-only destination preserving windows while starting with its own
  default language/theme. Matching supplies are used throughout.
- Embedded Debug consumer PDB symbol inspection: ExecuteMenuCommand is linked;
  ComposeCatalogMenu and BuildWindowCatalogGraph are absent. No runtime menu
  builder was added to that consumer.
- Manual native UI: application menu has Theme + Language; no language content
  button or settings window. F10, arrows and Enter select Light, Russian, and
  From system. Both menu headings, all four theme labels and existing content
  update in Russian. Tab and Shift+Tab return through list/Add; menu dismissal
  restores content focus. Native accessibility inspection exposes application
  menu/menu-item roles, localized names and disabled Open/Delete controls; the
  selected menu item is visibly marked and checked states are asserted natively.

## Evidence and boundaries

Local ignored artifacts: `.artifacts/windowcatalog-menu-build.log`,
`menu-chrome-stdout.log`, `menu-acceptance-stdout.log`,
`menu-acceptance-stderr.log`, and `menu-embedded-symbols.log`.

Initial verification found unloaded menu strings missing from the first UI
publication. The fix materializes the active menu language in the existing model
prepare stage before publication; no presenter fallback or graph repair remains.
Old acceptance assumptions of four content buttons / six settings entries were
updated to three content buttons / four theme entries.

Narrator speech: NOT RUN for the new menu. Actual OS text-size/theme/high-contrast
changes: NOT RUN; OS settings were not changed. Notification delivery and palette
priority tests do not certify a live accessibility configuration. Actual Windows
7, other platforms and physical mixed-DPI testing: NOT RUN. Existing Windows dark
buttons retain the earlier adapter painting path; this result does not claim
that classic Win32 now natively paints every control dark.

Porting structure and primary platform documentation:
[platform menus](../window-catalog-platform-menus.md).
