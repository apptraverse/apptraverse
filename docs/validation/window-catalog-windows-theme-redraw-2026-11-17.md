# Windows WindowCatalog: visible appearance choices and quiet native projection

Date: 2026-11-17. Scope: Windows WindowCatalog and the shared Win32 native
presenters used by the previously verified Windows applications. GPT-6.1 Sol
high owns implementation; GPT-6 Astra ultra performs the focused 30-rule Canon
audit. This change does not alter the portable model, S0 serialization or
publication format.

## Behavior and architecture

The main window has a visible native **Settings** menu with four mutually
exclusive choices: **From system**, **Light**, **Dark**, and **Custom dark**.
Russian labels follow the existing application locale. The custom choice is the
existing soft graphite palette (window RGB 32/32/32; controls RGB 43/43/43),
distinct from the regular dark palette. It is a named palette, without a color
picker. High Contrast retains its existing priority over preferences.

Each command invokes `UiDeviceState::SetAppearance` through `ModelObjectProxy`.
The menu checkmark and all open-window appearance follow the published mirror;
the UI does not mutate the canonical graph or speculate a new preference.
`kAuto`, `kLight`, and `kDark` select the existing theme values with
`soften_dark=false`; Custom dark selects `kDark` and `soften_dark=true`.
Preferences continue to reside in ordinary native LOCAL state, separate from
portable USER, with the existing accepted shutdown persistence boundary.
Changing language preserves the published radio selection.

The Windows presenter offers an optional native-menu creation hook and routes
native menu commands through the existing presenter command contract. Menu
presence is included in initial and requested client-to-outer-frame conversion.
LOCAL placement remains an outer-frame DIP observation, so it is not converted
again as a client frame.

## Flicker cause and correction

Dragging publishes LOCAL observations through the normal model → publication →
UI mirror path. A publication visits the active presenters. Previously those
callbacks wrote unchanged native titles and button captions and repeatedly
positioned every control. Full layout refresh also updated scrollbars with
redraw enabled even when their ranges, pages and positions were unchanged.
The list already compared its current strings with desired strings; a pure
position change did not require `LB_RESETCONTENT`.

Shared Win32 projection now compares actual native text, enabled/visible state,
child rectangles and scrollbar values before writing them. Requested scrollbar
pages and positions are normalized to the Win32 effective bounds before
comparison, including finite layout maxima below the viewport. A layout refresh
uses one final common-solver result for all child rectangles, following the
scrollbar passes, rather than solving again for every child. Native frame
observations, publication delivery and the common constraint engine remain in
place. Layout computation can still run for an unrelated publication; the
guarantee is that unchanged native presentation does not receive redundant
setter and positioning operations.

The native LISTBOX is created with `LBS_NOINTEGRALHEIGHT`: its default integral
item-height rule otherwise rounds the solver's rectangle and makes every later
native comparison request that rectangle again. A failure-only native probe
confirmed this mismatch as 140 position writes to control 5001 during the drag
case, with the other observed controls quiet. The correction preserves a real
LISTBOX and lets the common constraint solver own its exact outer dimensions.

Applying a resolved LOCAL placement also refreshes native font/theme resources
only if destination DPI actually changed. Ordinary same-monitor drag
observations do not increment the resolved placement revision; this separate
placement refresh was not the per-step drag cause. Actual OS settings
notifications and published appearance changes retain their native refresh path.
The post-placement native observation remains mandatory even without a DPI
change, so a resolved/clamped frame is acknowledged immediately rather than
being discovered by a later settings notification.

## Validation

Executed on this Windows host in `build/windows-win7-debug`, incremental Debug,
desktop v143/MSVC 14.44 with the existing local dependency sources and Windows 7
target definitions. `BUILD_TESTING`, examples and the bounded legacy examples
are enabled; the optional chat SDK/Aether link remain disabled. A short `W:`
drive alias for the same worktree/cache avoided an MSVC long-object-path C1083;
it did not change sources or toolchain.

| Final native test | Result | Seconds |
| --- | --- | ---: |
| `apptraverse_ui_text_control_win32_test` | PASS | 0.51 |
| `apptraverse_window_catalog_win32_acceptance_test` | PASS | 13.82 |
| `apptraverse_window_catalog_win32_chrome_adaptive_test` | PASS | 12.15 |
| `apptraverse_minimal_app_win32_acceptance_test` | PASS | 1.98 |
| `apptraverse_surface_generic_win32_smoke_test` | PASS | 3.60 |
| `apptraverse_main_window_win32_smoke_test` | PASS | 0.81 |
| `apptraverse_dynamic_objects_win32_smoke_test` | PASS | 1.22 |

The final modern selection passes **4/4 in 28.51s**; the final legacy native
selection passes **3/3 in 5.64s**, using the same final shared library. These are
seven distinct test executables/scenarios, without counting diagnostic reruns
as new passes. Logs: `.artifacts/windowcatalog-theme-final4-ctest.log` and
`.artifacts/windowcatalog-legacy-final-ctest.log`; build records are the adjacent
final build logs. `git diff --check` passes.

The updated load-only application was then launched with the user's preserved
matching S0 profile, `debug-run-2186904895/user`, and remained open as PID 51816,
HWND 11144102, title **Window Catalog**, with empty stderr. This is launch and
profile-continuity evidence; manual drag perception is not a separate claimed
test. Records: `.artifacts/windowcatalog-final-launch.*`. The launched load-only
executable SHA-256 is
`53D42407B573329ED7CF44A96A2960911FF8943A947BECDCAD7FE754CE31874C`.

The expanded native chrome regression covers:

- Actual menu commands, queued model execution, published radio checks, all four
  appearance choices, distinct custom palette, propagation to an open child,
  independent-session isolation and Russian menu localization.
- A real scrolling LISTBOX populated through model operations, one visible
  child, native child move messages and eight intervening model publications.
  HWND subclass probes count `WM_SETTEXT`, `WM_SETFONT`,
  `WM_WINDOWPOSCHANGING` and list reset/insert/delete/add messages. The moved
  top-level child is allowed its actual position changes; unchanged main-window
  controls, title and font must receive zero such writes.
- LOCAL generation advancement while the list HWND, selection, top index and
  keyboard focus remain unchanged.
- A finite root maximum below the native viewport, verifying the native clamped
  page and zero repeated nonclient paint during unchanged refresh/publication.
- Existing adaptive resizing, constraint override/clear, system-setting
  notification and missing-monitor resolution cases.

The existing process acceptance now selects Custom dark through the native
command, waits for its published radio check, and closes normally. Its native
LOCAL file probe asserts the saved preference before changing only stale OS
baseline data. Existing load-only and cold runtime processes verify the Custom
dark native menu radio, as do the existing absent-LOCAL seed and existing-LOCAL
precedence processes. No new lifecycle or persistence format is introduced.

The existing generic native text-control fixture has moved from the optional
chat example to `tests/`, with its target registered for Windows testing
independently of the optional chat SDK. Its test logic is unchanged.

Earlier diagnostic runs found the omitted post-placement observation, the
LISTBOX integral-height mismatch and a test-ordering error that checked the
appearance-only USER generation after an intentional language Event. The final
run preserves the original no-extra-work and zero-native-write assertions, and
checks unchanged USER generation immediately after appearance commands, before
the separately asserted language change.

The focused [30-rule Canon audit](windows-window-catalog-theme-redraw-audit-2026-11-17.md)
retains the distinction between source review and executed native evidence.

Actual Windows 7 execution, physical mixed-DPI movement, a full accessibility
hardware matrix, Apple runtime and Release packaging remain NOT RUN. Other
backends are unchanged by this Windows-only presentation correction; historical
cross-platform results retain their recorded scope.
