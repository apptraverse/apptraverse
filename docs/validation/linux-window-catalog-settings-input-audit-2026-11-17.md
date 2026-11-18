# Linux WindowCatalog settings and input: scoped Canon audit

Date: 17 November 2026. Baseline: `37714e5e`, branch
`maintenance/rename-window-catalog-example`. Scope: the requested Linux/GTK
WindowCatalog Settings menu, reported WSLg Add freeze, and the affected shared
GTK presentation boundary. This is not a new certification of all platforms,
accessibility environments, unrelated applications or network functionality.

The assigned Astra ultra auditor read `AGENTS.md`, all 30 mandatory rules in
[Canon section 25](../architecture/AppTraverse_Architecture_RU.md), the
[LOCAL contract](../architecture/ClientLocalEnvironment.md), changed source,
analogous GTK call sites and the retained evidence identified below. Sol high
owns implementation and tests; the coordinator owns builds and native operation.
The auditor changed only this report, ran no builds or native tests, and made no
implementation edit, commit, push or merge.

The corrected source has no remaining confirmed architectural blocker in this
scope. Seven distinct final native scopes passed, including the new two-process
menu test, full historical Catalog, independent MinimalApp, legacy Surface and
the existing native chat/text consumer.
Actual WSLg X11 pointer/keyboard and cold menu checks passed separately. This is
bounded source/runtime acceptance, not repaired Wayland input or full platform
and accessibility certification.

## Source findings

The source correction belongs in shared GTK presentation. The common publication
path intentionally visits live presenters. Previously, window layout and each
control presenter called `gtk_fixed_move` even for unchanged coordinates. A
client-mount allocation called layout again, and layout queued another native
allocation. GTK 3.24.41's implementation confirms that a visible unchanged move
still queues resize; coordinate equality only suppresses property notification.
See the upstream [GtkFixed implementation](https://raw.githubusercontent.com/GNOME/gtk/3.24.41/gtk/gtkfixed.c).

The corrected shared helper compares GtkFixed child coordinates and native size
requests before writing them. Window layout uses one result from the existing
portable solver for all children and removes the duplicate refresh in the
allocation callback. Button, list and text presenters use the same helper.
Construction-only size requests remain. Unchanged titles/captions/labels avoid
native setters; application state and structural publication are not suppressed.

Appearance providers were also recreated recursively on unrelated publication
and child registration. They are now cached against preferences and observed
native settings, invalidated on a real GtkSettings notification, and applied
explicitly to new controls and new list rows. Native geometry observations are
deduplicated at the adapter while real changes and user-placement intent still
travel through LOCAL Events. This is not a claim that all presenter visits or
layout calculations have been eliminated.

The main Catalog composes a native GtkMenuBar with English/Russian Settings and
four exclusive choices, reusing existing LOCAL data:

| Choice | Existing appearance value |
| --- | --- |
| From system | `kAuto, false` |
| Light | `kLight, false` |
| Dark | `kDark, false` |
| Custom dark | `kDark, true` |

Custom dark exposes the existing slate palette, not a new color editor. Native
radio input queues `UiDeviceState::SetAppearance` through the model proxy.
Because GTK first changes its native check, the guarded callback restores the
published choice until the Event returns through the UI mirror. Both preference
fields are explicit; no persistent schema or direct mirror mutation is added.
The generic secondary-click menu now uses the same four combinations. High
Contrast retains the existing system priority.

The optional native menubar is chrome outside the content mount. Native measured
menu height is added to initial and explicit semantic client-size requests. LOCAL
still stores the complete outer frame in desktop DIPs; applying LOCAL does not
add menu height twice. Required typed layout/device links remain direct. Genuine
mounting, closed/lazy and optional-menu conditions remain separate from invalid
graph state, and final required-control validation is retained.

## Review corrections

Source review identified that ordinary container recursion omits a GtkMenuItem's
attached submenu. The coordinator then confirmed a light popup over a Custom dark
window in actual WSLg. This was a real gap in the new visible menu, despite correct
main/child palettes and radio behavior. Both provider removal and application now
traverse attached submenus. Custom CSS includes menu/menubar; the generic transient
popup receives current providers too. Native menu controls and their accessibility
remain. The upstream [GtkMenuItem implementation](https://raw.githubusercontent.com/GNOME/gtk/3.24.41/gtk/gtkmenuitem.c)
and [per-widget style-provider contract](https://raw.githubusercontent.com/GNOME/gtk/3.24.41/gtk/gtkstylecontext.c)
support that diagnosis. A bounded popup-palette assertion was added to the native
menu test.

The first historical geometry run exposed fixture conversions that assumed the
application content occupied the entire GTK client. The corrected helper adds
menu height when requesting content dimensions and subtracts it when decoding
outer LOCAL observations. The independent raw-frame reader translates the
content viewport origin through GTK. Exact geometry comparisons and settling
requirements remain; production outer-frame persistence is unchanged.

## Distinct WSLg outcomes

The initial interactive process used `GdkWaylandDisplay` on `wayland-0` with no
default GDK seat and startup seat/keyboard criticals. The coordinator's actual
pointer clicks did not reach `LinuxPushButtonPresenter::OnClicked`; no child was
created. The auditor read `.artifacts/windowcatalog-wsl-hang-initial-stack.log`,
`windowcatalog-wsl-add-stack.log` and `windowcatalog-wsl-add-ui-stack.log`. The
captured model thread waits for work; the UI samples show GLib polling or native
painting. They do not establish a model Add deadlock. The observed host input
failure and the separate source-confirmed allocation feedback must not be merged
into a claim that one application change repaired Wayland.

With explicit `GDK_BACKEND=x11`, the same pre-correction executable accepted an
actual native Add click and created Window 1. The coordinator also exercised the
corrected executable through actual WSLg native input: Custom dark, Add with a
matching child palette, F10/Escape, keyboard Add activation and Russian Settings
choices/checks. This is separate from programmatic GTK signal tests. The final
load-only process restored the matching profile's two rows, selection, Russian
locale and Custom dark. Its actual F10 popup was dark with light text and the
correct localized radio selection. The coordinator observed process 2363; its
stderr file `.artifacts/windowcatalog-wsl-final2.log` was independently checked
to be empty. Sampled CPU time remained idle; this is a bounded observation, not
a performance benchmark. The existing profile was preserved, not migrated.

The interactive session uses explicit X11 launch configuration. No forced backend
selection, automatic application fallback or speculative recovery was added.
This host's seatless Wayland input remains **NOT PASS**. Physical Wayland hardware,
working-seat Wayland sessions and full WSLg placement behavior are not certified.

## Evidence assessment

The new `apptraverse_window_catalog_linux_menu_add_test` exercises actual native
menu activation and GtkButton actions, then waits for normal model publication.
It asserts that the UI mirror and native radio remain on published state before
the round trip, covers all four choices and distinct native/custom palettes,
checks the attached popup, adds three children and checks inherited appearance and
surviving widget identity, changes locale, and verifies an exact 600 by 320 content
viewport with the menu present. After settling, repeated unchanged presenter
callbacks must not restart client-mount allocation. Those final callbacks visit
the native boundary directly; they are not falsely counted as fresh model Events.
A second native process verifies separate LOCAL appearance and USER topology/
locale restoration. Profiles are isolated and freshly created by the wrapper.

The auditor read `/tmp/windowcatalog-linux-menu-final-ctest.log`: client mount,
live environment and the first menu test passed, while full Catalog acceptance
failed at cold native z-order after geometry conversion passed. The subsequent
`windowcatalog-linux-menu-probe-ctest.log` instead recorded cold dimensions
smaller by Openbox's 2 by 25 decoration extent. These failed attempts remain
evidence. The coordinator established that the initial run lacked a live window
manager and Openbox appeared only partway through the later run; comparison
across those decoration/stacking environments violated the historical fixture's
precondition. No production stack workaround or relaxed geometry assertion was
added. The final run used detached Openbox for the complete process sequence,
with its presence checked before and after using `wmctrl -m`.

The auditor independently matched all four final Passed lines in
`.artifacts/windowcatalog-linux-menu-final2-ctest.log`, **4/4 PASS in 54.86
seconds**. The final compile/link is retained in
`.artifacts/windowcatalog-linux-menu-final-build.log`:

| Final native scope | Result | Seconds |
| --- | --- | ---: |
| `apptraverse_linux_ui_client_mount_test` | PASS | 0.04 |
| `apptraverse_window_catalog_linux_local_environment_test` | PASS | 0.35 |
| `apptraverse_window_catalog_linux_menu_add_test` | PASS | 2.73 |
| `apptraverse_window_catalog_linux_acceptance_test` | PASS | 51.73 |

The final menu result includes the submenu-palette correction. Full historical
Catalog covers fresh Debug, native placement, identity/activation, localization,
Close/reopen/delete, content geometry and native stacking across cold cycles.
It uses GTK 3.24.41, private Xvfb/Openbox and explicit X11 in the current
incremental Debug cache `/home/nickc/apptraverse-layout-linux`.

The independent consumers were then relinked against the final shared GTK bytes
and rerun. The auditor read their final evidence:

| Existing independent native scope | Final result and retained evidence |
| --- | --- |
| MinimalApp embedded lifecycle, two native cold cycles | PASS; `.artifacts/windowcatalog-linux-minimal-final-smoke.log` |
| Legacy Surface native Add/Close/Russian, stable surviving XIDs, resize/orientation, caption Close, USER/LOCAL cold topology/placement and next number | PASS; `.artifacts/windowcatalog-linux-surface-final-smoke.log` |
| Existing native chat/text consumer: keyboard/input/caret/remount, Enter/button, RAM restart and LOCAL cold restore | PASS; `.artifacts/windowcatalog-linux-text-final-test.log` |

MinimalApp/Surface relink evidence is
`.artifacts/windowcatalog-linux-consumers-final-build.log`. The existing native
chat/text target was incrementally rebuilt in
`/home/nickc/apptraverse-generic-chat-linux`; its final build is retained in
`.artifacts/windowcatalog-linux-text-final-build.log`. The auditor independently
read its final PASS line and build/link completion. This seventh scope exercises
the affected shared GTK text-control boundary; it is not a new network or
SharedNode protocol audit. These seven distinct native scopes close the affected
historical regression boundary. AT-SPI located Surface controls for native
pointer input, and the text-test log records an active accessibility bus; neither
is screen-reader operation.
The test-only diagnostics for a missing native stack remain failure diagnostics,
not production tracing or a replacement for the original assertions.

## All 30 mandatory Canon dispositions

`Aligned` means no confirmed violation in the examined source change. Runtime
acceptance remains bounded by the evidence above and the separate Wayland
failure.

| Rule | Scoped disposition |
| --- | --- |
| 1. Complete graph | Aligned: required typed layout/device/presenter links remain direct; no graph repair or defaults added. |
| 2. All or nothing | Aligned: GTK construction and final required native-control boundary are retained; missing layout control remains fatal. No partial activation workaround. |
| 3. Explicit persistent defaults | Aligned: both appearance Event fields assigned explicitly; menu pointers and comparison caches are runtime-only and initialized. |
| 4. Real runtime states | Aligned: optional native menu, initial mapping, pending placement and closed/lazy windows remain real conditions rather than missing-link fallbacks. |
| 5. Model authors state | Aligned: menu input uses proxy/Event/publication; native radio is returned to published state while pending; no direct canonical or mirror mutation. |
| 6. Thin presenters | Aligned: native measurement, chrome conversion and idempotent projection remain backend work; the existing shared solver owns content geometry. |
| 7. Common application template | Aligned: shared GTK fix has no Catalog dependency; Catalog owns its menu composition. Final independent MinimalApp and Surface native regressions passed. |
| 8. Registration | Unchanged in scope: no new serializable class or application registration path. |
| 9. Fresh Debug | Aligned: ordinary Debug fresh policy retained; isolated explicit two-process load-only test is distinct. |
| 10. Release supply consumer | Source/build policy unchanged. No Release packaging proof claimed. |
| 11. Ordinary RAM stores | Unchanged: USER and LOCAL remain separate ordinary native-format RAM stores over supply. |
| 12. Save boundaries | Aligned: settings and Add actions queue Events; no save on click, resize or child Close. Existing desktop shutdown persistence retained. |
| 13. One serialization | Aligned: existing appearance/LOCAL Events and native `user.bin` partitions reused; no INI, sidecar or alternate graph format. |
| 14. Object-system lifetime | Aligned: GTK widgets, providers, subscriptions and caches remain presentation lifetime state; no global keepalive. Native menu destruction owns attached submenus. |
| 15. Domains and Clone | Unchanged: no lookup, identity-remapping or clone-closure changes. |
| 16. Constant semantics | Aligned: no supply mutation or supply-body copying into USER; menu composition adds no serialized state. |
| 17. Lazy graph | Unchanged: closed-window identities and ordinary lazy materialization retained. Main menu exists only with its native presentation. |
| 18. History retained | Unchanged: LOCAL Commit/Apply, compaction and replay stay intact; no new external effect in Apply. |
| 19. Close/unload/delete | Aligned: no semantic lifetime change; native subscriptions/providers/cache state reset at unload, and cold tests use the same saved identities. |
| 20. Active language | Aligned in the new visible UI: all four choices and Settings follow both supported Catalog locales. Existing generic context labels remain English. |
| 21. Event placement | Aligned: actual native observations remain LOCAL Events; explicit semantic frame intent remains separate; menubar height is accounted at GTK content/client conversion only. |
| 22. Headless/shared model | Aligned: no alternate business model introduced; new tests target the actual GTK/proxy/publication/process boundary. No new headless result inferred. |
| 23. S0 prototype | Aligned: no schema extension, migration, old-profile auto-load or compatibility repair added. |
| 24. Normal environment | Aligned: no recovery or production tracing subsystem; explicit X11 launch does not silently mask Wayland input. New test side effects execute outside assert. |
| 25. Used components/scope | Aligned: authorized Linux Catalog settings and analogous shared GTK projection only; no dormant control/platform implementation added. |
| 26. Technical policy | Aligned: C++20/no compiler RTTI/exceptions policy unchanged; native handles and caches are not persistent. |
| 27. Build policy | Coordinator uses the existing incremental Linux Debug cache as explicitly requested; auditor performs source/evidence review and this report only. |
| 28. Separately authorized future work | Unchanged: no network, SharedNode, resource-delivery, migration or compiled-cache work added or re-audited. |
| 29. Scoped evidence | Aligned: initial failures and their causes, source corrections, seven final native scopes, actual WSLg X11 input and Wayland NOT PASS remain distinct. |
| 30. Ownership/publishing | Aligned: Sol owns code/tests, Astra audits, coordinator performs operations; branch preserved and auditor performs no commit/push/merge. |

No physical screen-reader operation is established by a native menu or AT-SPI
tree/action test. Orca, real Windows Narrator, full High Contrast/text-scale and
physical mixed-DPI/monitor matrices remain **NOT RUN in this Linux slice**. The
broader WindowCatalog accessibility acceptance is a separate authorized step.
Apple implementation/runtime requires another host and remains NOT RUN. Windows,
Web and Android implementation is unchanged here; their historical results and
recorded limitations are neither rerun nor upgraded by this GTK-only correction.
