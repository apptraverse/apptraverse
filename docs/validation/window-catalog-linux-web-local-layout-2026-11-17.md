# Linux and Web: shared layout and LOCAL acceptance — 2026-11-17

This report records the executed Linux/Web part of the authorized non-Apple
Canon correction. Implementation: GPT-6.1 Sol high. The shared contracts and
Windows results are described in [the coordinated report](nonapple-layout-local-canon-fixes-2026-11-16.md).
Application profiles use the current S0 schema and newly generated matching
platform supplies. Experimental older profiles were not migrated or overwritten.

## Implemented boundary

GTK and DOM presenters measure native controls and call the same portable
constraint solver against the current UI mirror. The solver owns control geometry;
GtkScrolledWindow and DOM scrolling expose overflow without reducing native text.
Controls register with their backend window presenter. Required controls missing
at the end of construction fail explicitly. GTK's native caption Close policy
uses the shared explicit identity/slot filter, including the effective layout tree.
The child content Close remains at the bottom through a flexible constraint node.

Native viewport/frame facts no longer enter Catalog's portable pixel/layout
Events. GTK reports window observations to UiDeviceState LOCAL; browser resize
and media notifications update LOCAL and local measurement. Portable explicit
semantic frame requests and layout overrides remain separate. GTK applies the
published LOCAL monitor/placement choice and preserves the preferred endpoint
through automatic fallback. Browser pages cannot move physical browser windows;
the ordinary Screen API describes the current browser screen only.

GTK reads settings/displays on its UI thread before the model starts, and the
prepare hook compares loaded LOCAL before the first publication. GtkSettings and
screen notifications and Web ResizeObserver/media/resize callbacks handle live
changes without periodic presentation polling. Unsupported capabilities are not
reported as false supported values. GTK and Web expose Auto/Light/Dark and soft
Dark preferences through model proxies and local Events; High Contrast/forced
colors takes priority. GTK offers the preferences in its secondary-click menu;
Web provides labeled native select/checkbox controls.

Both Catalog and independent MinimalApp use the generic two-partition model
session. USER and LOCAL use the ordinary native storage format; persistence
writes user.bin and local/user.bin at the existing lifecycle boundary. Web's
hidden/pagehide capture takes both RAM partitions at one model barrier and queues
them in one IndexedDB transaction. Runtime notifications stop at the host's save
and shutdown barriers, preventing late ResizeObserver work after stop begins.
The old single-file callback remains available for existing host clients.

## Executed runtime evidence

Linux used Ubuntu WSL, clang++ Debug, GTK 3.24.41, a private Xvfb :97 display and
Openbox. Build directory: /home/nickc/apptraverse-layout-linux. Windows foreground
was not touched. Web used the installed emsdk Debug toolchain and headless Chrome
157.0.8103.79 through Playwright, served with COOP/COEP. Its build directory is
C:/Users/nickc/Documents/Codex/build-window-layout-web. Existing patched dependency
sources were reused. No Windows build directory was modified by this worker.

| Executed check | Result and retained local evidence |
| --- | --- |
| Full historical GTK Catalog smoke, including fresh Debug, native placement, identity/activation, localization, Close/reopen/delete, ordinary geometry and native z-order across cold cycles | PASS; .artifacts/linux-layout-smoke.log |
| Gtk client-mount regression and new live environment test under CTest | 2/2 PASS, 0.56s |
| Native GTK font/motion/theme notification, narrow resize, portable semantic frame unchanged while LOCAL observations change, soft-Dark preference and constraint override Event | PASS; .artifacts/linux-layout-local.log |
| Independent MinimalApp GTK two-cycle embedded lifecycle smoke | PASS; .artifacts/linux-minimal-layout-smoke.log |
| Full Catalog browser smoke: real DOM input/pager, resized child layout, locale, Close/reopen, fresh Debug and two cold restores; Dark/soft preference and forced-colors priority | PASS; .artifacts/web-layout-browser.log |
| Browser lifecycle: ordinary tab close, queued-command reload, navigation, Close+reload in the same JS turn, full Chrome process restart | PASS, 15.407s; .artifacts/web-layout-lifecycle.log |
| Independent MinimalApp browser: native DOM actions, two-window Close/reopen, Save, three cold restores, stale removed-control click, complete teardown | PASS; .artifacts/web-minimal-layout-smoke.log; emval handles 11 before stop, 6 at initial startup, 0 after stop |

The new GTK test changes actual GtkSettings properties on the GTK owner thread;
it verifies native notification/measurement behavior, not an external desktop
control-panel interaction. Browser forced colors is Playwright media emulation,
not a claim that the host OS High Contrast setting was changed. The lifecycle
run used --headless: Chrome's real tab-hide case is explicitly skipped in that
mode; tab close/reload/navigation/browser process restart were actually executed.
Emscripten's known main-thread blocking advisory for the explicit lifecycle
barrier is accepted identically by the Catalog and MinimalApp browser harnesses.

## Actual Windows source transfers

The source was the immutable, current-schema USER+LOCAL written by native Windows,
provided by the coordinating worker at ../outputs/android-acceptance/windows-source.
Its two holders, English locale and closed/open child states were inspected without
saving it. An unchanged copy plus a portable semantic manifest is kept under
.artifacts/transfer/windows-source; no source profile was rewritten.

| Transfer execution | Result |
| --- | --- |
| Windows USER -> absent Linux LOCAL, with optional whole native LOCAL seed | PASS in native-storage fixture and actual GTK lifecycle |
| Windows USER -> existing Linux LOCAL, with an intentionally absent optional seed path | PASS; existing native LOCAL map unchanged in full before destination OS comparison; actual GTK lifecycle PASS |
| Windows USER -> new Web LOCAL seeded from Windows | PASS in actual browser; Dark+soft preference, original closed-child identity reopened, save and cold restart |
| Windows USER -> existing Web LOCAL with Light preference and deliberately invalid optional seed bytes | PASS; Light retained, seed bypassed, original child reopened, save and cold restart |
| Both browser-produced USER partitions read back by Linux native fixture | PASS verify-reopened; complete portable semantic manifest matches the deliberate reopen |

Evidence: .artifacts/transfer/linux-{seeded,existing}.log,
linux-{seeded,existing}-native.log, web-{seeded,existing}.log and
web-return-{seeded,existing}.log. Destination OS observations legitimately update
LOCAL after the transfer; the exact existing-LOCAL equality assertion is made
before those OS observations, at the native storage import boundary.

The portable fixture now supports two native partitions, transfer with optional
seed/existing LOCAL, local inspection and semantic layout override/Clear. Browser
transfer uses the application's real IDBFS mount and native files. This is the
executed transfer scope, not a rerun of the historical 52-process matrix.

## Real compatible supply update

A second Debug Web target, web_window_catalog_demo_update, embeds an independently
distilled supply with the same identities/schema and default padding 24, compared
with padding 12 in the ordinary supply. The actual browser was restarted with
that binary and the same native USER+LOCAL. Native DOM geometry selected 24.
A model Event created USER override 44; that override survived supply B. After
ClearOverride, supply B's default 24 returned and survived another cold restart.
Both supplies were generated before runtime; no BACKING constant was mutated in
the running application.

PASS: .artifacts/web-supply-update.log. Reproduction uses
`tools/window_catalog_web_supply_update_test.py` with the two served URLs, the
Node Web portable fixture, and an isolated artifacts directory.

## Limits

Wayland, WSLg placement, physical mixed-DPI/multiple-monitor hardware, monitor
unplug/replug, Orca/AT-SPI reader operation, real OS forced-colors/high-contrast
interaction, other browsers, ARM devices and Release packaging were NOT RUN.
GTK endpoint identity falls back to an explicitly named GDK enumeration endpoint
when no connector name is exposed. No assertion here promotes that fallback to a
persistent hardware identity. The Web Screen API does not enumerate all monitors
or require a permission prompt. Apple source/runtime work is excluded.

Build alone is not counted as runtime acceptance. Runtime profiles, browser
profiles, object files and raw logs remain outside version control; this report
contains only sanitized scope and results.

## Post-review correction: desktop outer-frame contract

The final Astra review found that the first GTK implementation recorded the
client/scroller size in frame_dip while Windows records the outer frame. That
was a real cross-platform LOCAL seed defect. The corrected GTK adapter reads
`gdk_window_get_frame_extents` and records outer position/size in 96-DPI DIPs.
It waits until native mapping provides window-manager decorations, subtracts
those decorations only at the `gtk_window_resize` client-size boundary and
clamps using the full outer frame. Pending programmatic configure reports do
not rewrite the preferred placement. A no-op placement clears its pending flag
immediately because GTK may emit no configure notification for it.

A new placement revision, rather than a transient difference between asynchronous
native geometry reads, triggers placement application. This prevents an ordinary
user resize in flight from being mistaken for a changed decoration and restored
to the old placement. Backend runtime placement caches reset when a native window
is created again. No S0 fields or storage format changed; earlier experimental
GTK profiles with the incorrect client-size interpretation are not migrated.

The native cross-seed test uses the immutable actual Windows source. Its saved
main OUTER frame is **333 × 276 DIPs**. On decorated Openbox, corrected GTK creates
an OUTER frame of **333 × 276** with a CLIENT of **331 × 251**; its LOCAL observation
is **333 × 276**, not 331 × 251. After saving and creating a new GTK process/domain,
the same outer/client dimensions are observed. The assertions explicitly require
nonzero native decorations, so a same-platform client/outer conflation cannot
satisfy them. Evidence: .artifacts/windows-outer-source.log and
.artifacts/outer-transfer/seed-{native,cold}.log.

Legacy GTK smoke geometry targets remain native CLIENT sizes. Their helper now
explicitly subtracts the actual native decorations when comparing those targets
with LOCAL OUTER observations; the cross-seed test above compares the outer
contract directly.

The final focused recheck also exercises an immediate native resize while startup
observations and font notifications are still being published. GTK now recognizes
only a revision-1 placement originating from this native window's own first
observation as already applied; it cannot restore that stale startup size over
the newer resize. Seeded/existing placements and coalesced later monitor-resolution
revisions still apply normally.

Final focused evidence is .artifacts/linux-outer-followup.log and the individual
.artifacts/outer-transfer/*-final.log files. With an existing fresh GTK LOCAL,
Windows USER import preserves its **222 × 215** outer frame and native **220 × 190**
client. With absent LOCAL, the actual Windows seed produces **333 × 276** outer /
**331 × 251** client and preserves that exact outer size on native cold restart.
The same run passes the live font/motion/layout test and the independent GTK
MinimalApp two-cycle lifecycle smoke. These executions use the latest shared B3
explicit-frame-intent correction as well as the GTK outer-frame correction.

The final full GTK Catalog historical smoke was rerun after both corrections and
PASSed: fresh Debug, native placement, child Close/reopen/identity, locale,
ordinary client move/resize, native z-order and all cold geometry cycles.
Evidence: .artifacts/linux-outer-smoke.log. No build-only result is counted as this
runtime PASS. git diff --check also passes (apart from Git's informational
line-ending notice for the browser transfer script).
