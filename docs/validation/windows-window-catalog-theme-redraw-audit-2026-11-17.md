# Windows WindowCatalog theme and redraw: scoped Canon audit

Date: 17 November 2026. Baseline: `6f37a363`, branch
`maintenance/rename-window-catalog-example`. Scope: the Windows WindowCatalog
visible theme selector and Windows native-update amplification during window
movement, including the affected generic backend and independent MinimalApp
regression boundary. This is not a new audit of network, unrelated applications,
other platform adapters or all possible environments.

The assigned Astra ultra auditor read `AGENTS.md`, the 30 mandatory rules in
[Canon §25](../architecture/AppTraverse_Architecture_RU.md), the LOCAL contract,
the changed source and relevant analogous call sites. Sol high owns all code and
test changes. The auditor changed only this report and ran no builds or native
tests, made no implementation edits and performed no commit, push or merge.

## Source verdict

No remaining confirmed architectural blocker was found in the corrected source.
The final **seven distinct native test scopes passed**: Catalog acceptance,
chrome/adaptive layout, native text controls, independent MinimalApp, Surface,
Main Window and Dynamic Objects. This is source-conformance acceptance with the
executed scope below, not certification of every environment or hardware matrix.

The cause is native projection work on unrelated publications. The common
publication path intentionally calls `OnModelChanged` on live presenters. Before
this change, Windows repeatedly set unchanged window/button text, refreshed
scrollbars and called `SetWindowPos` for every child. A position-only native move
produced an ordinary LOCAL observation and publication, thereby repeating those
native operations across the interface. Catalog row labels contain window number
and open/closed status, not native position, so a drag does not require replacing
their text.

`ObserveWindow` does not increment a placement request revision on every drag.
Only monitor-resolution Events issue a new placement revision. The former
unconditional settings refresh at the end of placement application was an
additional conditional amplifier, not the cause on every ordinary drag step.

The fix belongs in the Windows backend: compare actual native text, state and
child rectangles before invoking setters; update scrollbar metadata only when it
changes; solve against the final viewport after scrollbar adjustments and reuse
that result across children. Placement only refreshes native settings when the
window DPI changed, while still reporting its resulting native geometry when
DPI is unchanged. The common solver, model Events, LOCAL observation stream,
structural publication and required graph links remain intact.

This does **not** claim that publication visits only changed presenters or that
every unnecessary layout calculation has been eliminated. The correction avoids
redundant native mutations; `OnModelChanged` and layout computation still occur.

The main window now composes a standard native Settings menu with four exclusive
choices. They map to existing LOCAL data without a schema extension:

| Choice | Existing `UiAppearancePreferences` value |
| --- | --- |
| From system | `kAuto, false` |
| Light | `kLight, false` |
| Dark | `kDark, false` |
| Custom dark | `kDark, true` |

The last choice exposes the existing softened charcoal palette; it is not a new
arbitrary color editor. `WM_COMMAND` queues `UiDeviceState::SetAppearance` through
the model proxy. Applied checks/colors are taken from the published mirror.
High Contrast retains its existing priority. English/Russian menu text follows
the active locale, and rebuilding translated labels invalidates the cached check
state so the current radio selection is restored.

## Review corrections and source boundary

The following issues found during implementation review were corrected before
source acceptance:

- Reusing the last scrollbar-pass layout could use dimensions from before the
  final native scrollbar adjustment. A final solve now uses the actual client
  viewport, preserving the previous geometry contract without solving per child.
- Replacing menu labels on a language change could clear native radio checks
  while cached appearance suppressed their restoration. The translation update
  now resets that cache, and the regression asserts the translated menu/check.

The coordinator subsequently identified a third bounded issue in the new
scrollbar guard, confirmed by this audit: a valid finite root maximum can make
content smaller than the viewport. Windows clamps `nPage` and `nPos`; comparing
the raw requested page against that effective native value repeats
`SetScrollInfo(..., TRUE)` on unchanged publications. The shared Windows helper
now normalizes both values before comparison. A bounded finite-root-maximum test
also monitors `WM_NCPAINT` during repeated settled layout/publication updates;
the original geometry/message counter alone did not cover this repaint path.
The exact bounds were checked
against Microsoft's [SetScrollInfo documentation](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setscrollinfo).

The existing system-settings runtime regression also exposed a lost side effect
when unconditional placement settings refresh was removed: that refresh had
reported the final native frame. Omitting the report left the model's observed
frame stale until a later settings notification. The corrected path explicitly
reports after same-DPI placement and retains refresh/report for a DPI change.
It does not turn the observation into a new preferred-placement request. The
original no-extra-work assertion was retained rather than weakened.

The native no-write regression then isolated another cause: LISTBOX control
5001 received 140 positioning messages while its text, font and row content were
unchanged (`windowcatalog-probe3-ctest.log`). Its default integral-item-height
behavior rounded the solver's requested height, defeating an actual-rectangle
equality check. Creation now includes `LBS_NOINTEGRALHEIGHT`, so the common
solver owns the exact rectangle while the control keeps native list rendering,
selection, scrolling, keyboard behavior and accessibility. This follows
Microsoft's [list-box style contract](https://learn.microsoft.com/en-us/windows/win32/controls/list-box-styles).
The temporary diagnostic was removed; the original zero-write assertion remains.

The new native menu is included when converting semantic client size to outer
size, both on initial creation and explicit `RequestFrame`. The three existing
Catalog native resize helpers were updated accordingly. The analogous generic
frame/routing and MinimalApp helpers remain correct because those windows have
no application menu.

Reviewed implementation paths are `win_ui_frame.{h,cpp}`,
`win_ui_presenters.{h,cpp}`, `win_ui_push_button_presenter.cpp`,
`win_ui_text_control_presenter.cpp` and Windows
`window_catalog_presenters.h`. Required layout/native binding checks, genuine
mounting/lazy lifecycle conditions, object registration, S0 serialization and
the generic LOCAL startup/persistence path are not replaced or bypassed.

## Evidence assessment

The changed `window_catalog_win32_chrome_adaptive_test` contains meaningful
native-boundary assertions: it creates a scrolling LISTBOX, preserves selection,
top index and focus, moves a real child HWND through eight positions while
pumping model/publication work, and requires LOCAL generation to advance. Native
subclass probes require zero unrelated caption, font, child-frame or list-content
writes. The same test exercises all four menu commands through `WM_COMMAND`,
checks that neither canonical state nor UI mirror changes before model work,
checks published menu radio state, and checks Russian labels and retained checks.
This is programmatic native-message testing, not physical mouse testing.

The auditor inspected the initial and second retained CTest logs in `.artifacts`.
`windowcatalog-theme-first-ctest.log` records Catalog/Minimal acceptance passing
but chrome failing its existing pending-work assertion; the post-placement
observation correction above addresses that failure.
`windowcatalog-theme-final2-ctest.log` records native text-control, Catalog and
Minimal acceptance passing, with chrome still failing `frame_writes == 0`.
These failed attempts remain evidence. A further
`windowcatalog-theme-final3-ctest.log` run reached and passed the earlier drag and
finite-scrollbar checks, but then failed a newly extended test's expectation that
portable generation remain unchanged after intentionally changing language.
The final test asserts unchanged generation immediately after appearance-only
actions, then asserts increased generation after the separate language action;
the appearance isolation invariant was retained.

The existing independent native scopes in `windowcatalog-legacy-ctest.log` were
matched to their Passed lines: `surface_generic_win32_smoke_test`,
`main_window_win32_smoke_test` and `dynamic_objects_win32_smoke_test`, **3/3 PASS,
6.26 seconds**. Their final same-library rerun is **3/3 PASS, 5.64 seconds** in
`windowcatalog-legacy-final-ctest.log`; the associated final build reports no work
to do. A short `W:` filesystem alias was used for the same current build
cache to avoid a compiler path-length failure; this is not a different source
checkout, a cache clean or evidence of an actual Win7 execution.

The process acceptance extension was also reviewed: it selects Custom dark with
an actual menu command, waits for published checks, closes normally and verifies
the persisted LOCAL preference before modifying only the stale OS-baseline test
data. Previously the fixture injected the preference directly. The two existing
cold launches and the two initial/existing-LOCAL seed launches now inspect native
menu checks as well. This final extension passed within Catalog acceptance.

The auditor independently matched every final Passed line in
`.artifacts/windowcatalog-theme-final4-ctest.log` and
`.artifacts/windowcatalog-legacy-final-ctest.log` to the following seven records.
The first four completed together in 28.51 seconds; the latter three in 5.64
seconds. `.artifacts/windowcatalog-theme-final4-build.log` shows the final test
compile/link; the production fix had already been built for the preceding run.
All use the current `build/windows-win7-debug` Debug cache.

| Native test scope | Final result | Seconds |
| --- | --- | ---: |
| `ui_text_control_win32_test` | PASS | 0.51 |
| `window_catalog_win32_acceptance_test` | PASS | 13.82 |
| `window_catalog_win32_chrome_adaptive_test` | PASS | 12.15 |
| `minimal_app_win32_acceptance_test` | PASS | 1.98 |
| `surface_generic_win32_smoke_test` | PASS | 3.60 |
| `main_window_win32_smoke_test` | PASS | 0.81 |
| `dynamic_objects_win32_smoke_test` | PASS | 1.22 |

The final chrome result includes zero redundant native writes during the measured
drag/publication scenario, preserved list identity/selection/top/focus, and zero
repeated nonclient paints for the settled finite-maximum case. Native OS
repainting caused by exposure is not prohibited or measured as a universal
zero-paint guarantee. The finite-maximum test acknowledges startup placement
before exercising its steady-state boundary. Ordinary non-High-Contrast theme
coverage does not certify a full physical accessibility matrix.

The auditor independently computed SHA-256 for the four current application
executables (under the corresponding examples directory in that build cache):

| Executable | SHA-256 |
| --- | --- |
| `win32_window_catalog_demo.exe` | `1F54F5AF2B4217382F61A8F6444CC88ACFF4219058D990E95D594538C3CB857F` |
| `win32_window_catalog_demo_load_only.exe` | `53D42407B573329ED7CF44A96A2960911FF8943A947BECDCAD7FE754CE31874C` |
| `win32_minimal_app.exe` | `5775F02808855C010CB9DC8D83A5400F14FA22300BC871D1456C31B81105E2FD` |
| `win32_minimal_app_load_only.exe` | `1F190741ADEBEA2CDD8AF6E5573A9153D3C83A6C0F53CAAEA14740EC84C0AD1D` |

## All 30 mandatory Canon dispositions

`Aligned` means no confirmed violation was found in the examined change; it does
not certify every existing application or runtime condition.

| Rule | Scoped disposition |
| --- | --- |
| 1. Complete graph | Aligned: required layout/device/presenter links remain direct; no repair, defaults or silent skips added. |
| 2. All or nothing | Aligned: required native menu/text/geometry operations fail at their producing boundary; final native layout validation remains. |
| 3. Explicit persistent defaults | Aligned: both appearance Event fields are explicitly assigned; new menu caches are runtime-only and initialized. No persistent schema/default changes. |
| 4. Real runtime conditions | Aligned: optional application menu and child/main distinction are genuine composition choices; mounting, closed/lazy and disabled conditions remain distinct from missing links. |
| 5. Model authors state | Aligned: native menu input goes through proxy/Event/publication; no speculative persistent mutation or publication suppression. |
| 6. Thin presenters | Aligned: native menu projection, text/state comparisons, geometry application and measurement stay in Windows; the shared constraint solver retains geometry semantics. |
| 7. Common template | Aligned: generic backend fix has no Catalog dependencies; the optional menu hook leaves independent MinimalApp composition intact. Its native acceptance and the existing Surface/Main/Dynamic scopes passed. |
| 8. Registration | Unchanged in scope: no new serializable class or registration path. |
| 9. Fresh Debug | Unchanged: ordinary Debug starts a fresh graph; explicit load-only consumers remain distinct. |
| 10. Release supply consumer | Source/build policy unchanged; no Release packaging acceptance is claimed. |
| 11. Ordinary RAM stores | Unchanged: native-format USER/LOCAL/BACKING partitions remain ordinary independent RAM stores. |
| 12. Save boundaries | Aligned: menu/drag operations queue Events and do not write files; desktop lifecycle persistence is unchanged. |
| 13. One serialization | Aligned: existing `UiAppearancePreferences` and LOCAL Event/native storage format reused; no INI, sidecar or manual graph format. |
| 14. Object-system lifetime | Aligned: HWND/HMENU and comparison caches remain presentation lifetime state; no global keepalive or model-domain ownership workaround. |
| 15. Domains and Clone | Unchanged: no lookup, identity remapping or clone-closure changes. |
| 16. Constant semantics | Aligned: no supply mutation or copying supply bodies into USER; UI-only menu composition has no serialized state. |
| 17. Lazy graph | Unchanged: closed-window identities and ordinary link materialization remain; menu belongs only to active main presentation. |
| 18. History retained | Aligned: existing LOCAL Commit/Apply, compaction and replay are retained; no new external effect in Apply. |
| 19. Close/unload/delete | Unchanged: existing lifetime and lazy-open semantics remain; native window destruction owns its attached menu. |
| 20. Active language | Aligned in changed UI: required application localization remains; new menu explicitly follows both supported languages and refreshes radio checks after translation. |
| 21. Event placement | Aligned: observations remain LOCAL Events; semantic frame requests stay explicit; native menu height is accounted for at the Windows conversion boundary. |
| 22. Headless/shared model | Aligned: no alternate model path introduced; actual HWND/proxy/publication and native process/file boundaries passed. No new headless or other-platform run is inferred from those native results. |
| 23. S0 prototype | Aligned: no schema, migration, compatibility-repair or old-profile auto-load added. |
| 24. Normal environment | Aligned: no recovery/hardening subsystem or production tracing added; new side effects execute outside assert. |
| 25. Used scope/components | Aligned: Windows Catalog settings and affected generic native projection only; no resurrected controls, platform expansion or unrelated implementation. |
| 26. Technical policy | Aligned source: C++20/no compiler RTTI/exceptions policy unchanged; new native APIs are compatible with the existing Win7 boundary. Actual Win7 remains NOT RUN. |
| 27. Build policy | Coordinator uses the current incremental Debug cache; auditor performed read-only inspection and this report only. |
| 28. Separately authorized future work | Unchanged: no network/SharedNode/resource/cache/migration work added or re-audited. |
| 29. Scoped evidence | Aligned: source review, failures/corrections and final seven native PASS records are explicit; historical other-platform evidence remains unchanged, with no promotion to a full portability or hardware matrix. |
| 30. Ownership/publishing | Aligned: Sol owns code/tests, Astra audits; feature branch preserved. Auditor made no implementation edits, commit, push or merge. |

Actual Windows 7 execution, physical mixed-DPI monitors, screen-reader operation,
the full High Contrast/text-scale matrix, Release and Apple runtime remain
**NOT RUN** in this change. Linux/Web/Android implementation is unchanged; their
historical evidence, including the recorded Android Recents failures, is neither
rerun nor upgraded by this Windows-only correction.
