# WindowCatalog display controls on non-Apple platforms

The desktop and paged supplies use one CatalogUiState for fullscreen and one
layout model for the main window. Native input carries the target identity to
the model through the proxy. The presenter applies state from the UI mirror.
The existing S0 formats are unchanged. Newly authored resources on GTK,
Android and Web have separate ID ranges so the original resources retain
their IDs. The Windows display definition keeps its existing IDs and labels.

| Scope | Executed evidence |
|---|---|
| Windows and headless | Six Windows Debug tests passed with the extended headless and menu model cases. They cover model publication, stale targets, explicit Delete, dormant LOCAL, cold restore and optional seed with desktop and paged supplies. Win32 native acceptance and monitor placement regression also passed. |
| Linux/GTK | Native X11/Xvfb with Openbox passed the menu and fullscreen tests for main and child, F11/Escape, normal-frame restore, deferred equal-data frame requests, child Close/reopen and LOCAL cold restore. The earlier GTK lifecycle smoke, local environment, client mount and headless tests also passed. A focused WSLg Wayland run passed the display commands and cold restore, including restoration of the original content size and retention of the saved normal frame. |
| Android | The API 34 x86-64 emulator passed 11 native load-only tests, including two new display cases, and a separate fresh Debug persistence test. An APK update installed over the prior app with existing LOCAL. Separate USER-only imports of two and four windows started from a three-window Android profile. Both kept the local theme, language, monitor observations and fullscreen IDs through cold restart. The import with two windows left the missing third window's fullscreen record dormant. See [Android evidence](window-catalog-android-display-2026-11-19.md). |
| Web | Real Chrome passed localized layout and viewport fullscreen, paging, Escape, child Close/reopen, cold LOCAL restore, and unchanged USER during fullscreen. Browser smoke, browser lifecycle and the embedded session passed. An independently distilled supply update changed the default while preserving a USER override until Clear. The separate MinimalApp load-only browser smoke passed with its original aggregate registration. |
| USER interchange | The Windows, Linux, Android x86-64 and Web target-ABI consumers passed 52 processes. This includes mutations and return transfers. All 32 verification steps preserved the input USER and test LOCAL bytes. The harness copies its private LOCAL fixture when present so it can compare localized rows; actual USER-only import has separate native coverage. |

The first matrix run failed seven routes involving Android because that runner
copied USER and the manifest but omitted the test LOCAL. The corrected runner
copies and hashes both input partitions. It does not write installed
application profiles.

A separate native Windows run opened the Android target-ABI USER with a fresh
Windows LOCAL, reopened a closed child via Win32 controls and restored it on a
cold launch. Row labels used the destination language.

The branch was subsequently integrated over main at `336f25df`, which
contains the independent Apple work. The generated Windows, GTK, Android and
Web WindowCatalog supplies match their verified pre-integration bytes.
On the integrated source, Windows and headless passed the six selected tests.
Native Win32 acceptance required a separate invocation to satisfy its
foreground precondition. GTK/X11 and the isolated focused Wayland display
test passed again. Chrome repeated the display and embedded-session tests.
Android repeated both new display cases. The instrumentation now waits for
observed native rotation and viewport dimensions before injecting swipe input.

The Wayland adapter compares client size and supported global position when
classifying user geometry changes. Changes in compositor decoration extents
remain native observations. Physical display unplug/replug and mixed-DPI
monitor hardware were outside the GTK runs.

The full Gtk menu run on WSLg Wayland timed out in the quiet-allocation probe
before its fullscreen stages. Native request traces had settled at that point,
while GTK continued emitting allocation signals. The focused Wayland display
run and full X11 menu run have separate results.

The Web application fills the viewport provided by the tab. The browser owns
browser-level fullscreen. Separate optional Web harness checks still have
their known scope limitations: the standalone presenter fixture expects an
empty work queue with native observations enabled, and the MinimalApp cold
restore harness requires the load-only consumer. The app-level Web checks above
ran with their corresponding supported configuration.

## Canon review

The read-only Astra ultra review covered all 30 rules in §25 against the port
diff. Sol high implemented the platform changes. The review found that GTK
could consume a model frame intent during fullscreen without applying it.
GTK now retains the latest intent at runtime and applies it after exiting,
including repeated requests with equal model data. The native X11 test
verifies the resulting client viewport. X11 and the focused Wayland suite
also verify an undersized GTK request followed by a real native resize.
The first request can be constrained by native chrome without blocking the
next observed user change.

| Rules | Source disposition |
|---|---|
| 1–4 | Supply composes required links and defaults. Native readiness, closed targets and unavailable outputs remain explicit runtime conditions. |
| 5–8 | Proxy and Events author state, UI mirrors supply native projection, and the independently used registration aggregates remain available. |
| 9–13 | Fresh Debug and load-only remain separate. Platform lifecycle uses existing separate RAM USER and LOCAL partitions, existing S0 and native save boundaries. Release binaries were outside this run. |
| 14–18 | Native handles, subscriptions and pending requests remain runtime. Domain/Clone, immutable resources, lazy identity and immediate Event folding retain their contracts. |
| 19–23 | Close retains LOCAL, Delete clears it and USER omission preserves dormant state. Localization follows the active slot. GTK applies monitor/geometry publication. Headless covers both display supply shapes. |
| 24–28 | The port uses existing native failure boundaries and build policies. No Apple backend files or network features are involved. |
| 29–30 | Executed evidence and source review remain separate. The changes deploy from the feature branch under the existing main publication instruction. |
