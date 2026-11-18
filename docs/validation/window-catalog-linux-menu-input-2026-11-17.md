# Linux WindowCatalog: native appearance menu and input diagnosis

Date: 2026-11-17. Scope: Linux/GTK WindowCatalog and the affected generic GTK
presentation boundary. Sol high owns code and test changes; Astra ultra performs
the focused [30-rule Canon audit](linux-window-catalog-settings-input-audit-2026-11-17.md).
The portable model, native S0 schema and USER/LOCAL storage formats are unchanged.

## Visible settings

The main window now has a native Settings menubar with From system, Light, Dark
and Custom dark radio choices, localized to Russian with the application.
Custom dark retains the existing GTK slate palette (`#29323c`, text `#e8edf2`),
distinct from native Adwaita dark. High Contrast retains its system priority.
The existing generic context menu now selects the same four exact preference
combinations instead of an independent soften toggle.

Menu input queues `UiDeviceState::SetAppearance` through `ModelObjectProxy`.
GTK changes a native radio before emitting its toggle signal; the callback
restores the published radio state until the model Event returns through the UI
mirror. A guarded synchronization prevents recursive commands. There is no
speculative canonical or mirror mutation. All open windows use the shared LOCAL
appearance preference, persisted at the existing native shutdown boundary.

The generic GTK window composes an optional native menubar outside its scrolled
content mount. The common constraint engine continues to own content geometry.
Initial sizing and explicit RequestFrame add measured native menu height to the
GTK whole-client request; LOCAL still observes the complete outer frame and is
not converted twice. Native measurement uses the current appearance/font.

## Reported Add freeze: two separate findings

The original WSLg process used a `GdkWaylandDisplay` with no default GDK seat.
Actual native pointer clicks did not reach `LinuxPushButtonPresenter::OnClicked`.
The model thread waited for work and the UI thread continued GLib polling or
painting; the captured evidence does not establish a model deadlock. The same
existing executable under explicit `GDK_BACKEND=x11` accepted an actual native
Add click and created Window 1. This host's seatless Wayland input remains
**NOT PASS**. The accepted interactive session uses explicit X11; no application
backend override or automatic product fallback was added.

A separate shared GTK projection defect was confirmed in source. Upstream GTK
queues a resize in `gtk_fixed_move` even for equal coordinates. Our
size-allocate callback refreshed layout, which moved every child again; button,
list and text presenters also did so on unrelated publications. The window also
recreated recursive CSS providers on every publication and child registration.
These operations could sustain allocation/style work without a semantic change.

Shared GTK helpers now compare requested native size and GtkFixed child
coordinates before writing them, including pending allocation. The window uses
one common-solver result for its children and avoids duplicate layout work in the
allocation callback. Unchanged titles, button captions and text labels are
guarded. CSS providers are retained until appearance or observed native settings
change; newly registered controls and newly added list rows explicitly receive
the retained providers. Provider application and removal both traverse attached
GtkMenuItem submenus, which GTK does not enumerate as ordinary child widgets.
Native popups therefore follow Light/Dark and the explicit custom slate palette,
with native radio, hover, disabled and keyboard behavior retained. Real GTK
settings notifications invalidate the cache.
Equal native geometry observations are deduplicated locally, while actual
geometry and monitor decisions still travel through LOCAL model Events.

## Validation scope

The new `apptraverse_window_catalog_linux_menu_add_test` runs two native
processes. Its first process activates all four native radio items, verifies
published checks and no speculative mirror change, compares stock/custom dark,
uses three actual GTK button actions to Add windows, checks new-child palette
and surviving native widget identity, changes to Russian, and verifies an exact
600×320 content viewport after RequestFrame with the menu present. After initial
allocation settles, repeated unchanged presentation must not restart allocation.
The second process verifies saved USER topology/locale and native LOCAL custom
appearance/menu/palette restoration.

The historical geometry fixture retains its exact comparisons. Its native
conversion now adds menu height to content-size requests and subtracts it when
decoding outer LOCAL frames to content dimensions. The independent raw-frame
reader translates the content widget's origin through GTK instead of assuming
the content begins at the top-level client origin. This corrects the test's
coordinate boundary; it does not relax settling or cold geometry requirements.

Final incremental Debug execution used the existing
`/home/nickc/apptraverse-layout-linux` cache and local dependencies. The private
X11 display had a live Openbox window manager throughout the final aggregate.

| Final native CTest | Result | Seconds |
| --- | --- | ---: |
| `apptraverse_linux_ui_client_mount_test` | PASS | 0.04 |
| `apptraverse_window_catalog_linux_local_environment_test` | PASS | 0.35 |
| `apptraverse_window_catalog_linux_menu_add_test` | PASS | 2.73 |
| `apptraverse_window_catalog_linux_acceptance_test` | PASS | 51.73 |

Final aggregate: **4/4 PASS in 54.86s**. Evidence:
`.artifacts/windowcatalog-linux-menu-final2-ctest.log` and
`.artifacts/windowcatalog-linux-menu-final-build.log`. The historical full
Catalog acceptance retains strict native geometry, lifecycle, list identity,
reopen, two cold cycles and z-order requirements. Initial diagnostic runs exposed
the old fixture's content/menu conversion assumption, then an unavailable private
window manager and a window manager introduced between save/cold phases. Those
runs did not pass; they are not reclassified as product defects or acceptance
success. The final run uses the corrected native boundary and a stable WM.

The user's original matching profile was preserved. The updated load-only
application was cold-started under explicit X11 as PID 2363 and restored Russian
locale, both Window 1/Window 2 rows, selected row and Custom dark. The preceding
interactive X11 session accepted actual native pointer input for Add;
F10 opened the localized four-choice popup, and Escape returned to the native
Add button so Return created Window 2. In the final cold process the popup
itself is dark with light text and the correct custom radio check.
The final cold process remained idle with CPU time `00:00:00` and empty stderr.
Evidence: `.artifacts/windowcatalog-wsl-final2.log` and the retained native
observations in this task. The process ID is an observation of this launch, not
a persistent application identity.

Existing consumers were relinked against the final shared GTK library. Both
native script scopes PASS: MinimalApp's two-cycle embedded USER acceptance and
Surface's Add/Close/Russian, stable surviving XIDs, resize/orientation, native X,
USER+LOCAL cold topology/placement and next-number smoke. Evidence:
`.artifacts/windowcatalog-linux-minimal-final-smoke.log`,
`.artifacts/windowcatalog-linux-surface-final-smoke.log` and
`.artifacts/windowcatalog-linux-consumers-final-build.log`.

The existing shared-chat native text consumer was rebuilt against the final GTK
sources and PASSes keyboard/input/caret/remount/Enter/button, RAM restart and
LOCAL cold restoration. Evidence: `.artifacts/windowcatalog-linux-text-final-test.log`
and `.artifacts/windowcatalog-linux-text-final-build.log`. The pinned SDK passed
its clean-source validation using process-local Git `core.autocrlf=true` to
match the Windows checkout; no pin check was bypassed or SDK source changed.
This is an existing-consumer regression, not new chat implementation. The AT-SPI
bus was active; that does not establish operation by a screen reader.

These are **seven distinct accepted native scopes**: four CTests and the three
existing MinimalApp, Surface and native text consumer runs. The Wayland input
failure remains NOT PASS.
Actual Windows 7, Apple runtime, physical mixed-DPI/
accessibility hardware matrices, full screen-reader operation and Release
packaging are outside this executed acceptance scope.
