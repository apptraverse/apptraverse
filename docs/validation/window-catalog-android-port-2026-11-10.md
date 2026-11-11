# WindowCatalog Android acceptance — 2026-11-10

Base: `473570d`, branch `feature/domain-hierarchy-cloning`. Scope: the
WindowCatalog Android application, shared model/runtime and existing
Windows/Linux/headless regression boundaries. Apple work is in another branch
and was neither changed nor merged. Maximization remains deferred.

## Implemented behavior

The port uses AndroidApplicationHost, ApplicationModelRuntime and the existing
publication/UI mirror pipeline. Native controls send model commands; list
commands capture holder IDs in the mirror before crossing the queue. Java
forwards swipe intent and actual client size/DPI, rather than choosing a page
or scaling model layouts itself. The published model selects the visible page.

USER is ordinary RamDomainStorage over embedded BACKING. Add, localization,
swipe, resize and child Close do not write files. Activity.onStop queues a save
after accepted model work and writes one user.bin. Android can terminate a
background process without onDestroy, so this platform boundary is explicit;
an abrupt kill before onStop is not a supported save boundary. onDestroy does
not request another save simply because it follows onStop.

Normal Debug starts each process with a fresh profile below the requested
parent and never loads prior USER. The separate debugLoadOnly build keeps
assertions but restores USER over the same supply. A build-host exporter is
used for cross-compilation. Its stable incremental build directory owns the
generated supply, which is copied into the current NDK build directory.
Runtime registration and graph construction are separate translation units.

## Defects found and corrected

- SyncPost previously checked stop separately from queue insertion and used a
  timeout while queued work captured stack references. Acceptance now shares
  the stop lock and accepted work waits for actual completion. Tests cover
  accepted work draining during stop and rejection after stop.
- Removing presenter-side scaling exposed the prefab-sized child Close button.
  A model viewport Event now calculates its rectangle from dimensions/DPI;
  the prefab uses the same pure layout function. CatalogWindow captures/replays
  its concrete state. Desktop position and restore geometry are untouched.
  The headless regression failed before the fix, then passed portrait,
  landscape, DPI-only, duplicate-event, replay, Close/reopen and cold-load cases.
- Recreating an Activity remounted native views but did not reapply the active
  page/title or dismiss loading. The application now reflects its current
  mirror immediately after remount; a same-process Activity replacement test
  verifies this without an additional model command.
- The mobile viewport implementation is isolated with the existing mobile
  Close code. An exact Debug Windows load-only link map confirms that the
  CatalogCloseButton, mobile-active and child-viewport registrars/TUs are absent.
  A class-name string alone was insufficient evidence because typed pointers
  also mention the class.
- Native UI callbacks are registered by the Android backend. The new viewport
  callback also has the conventional JNI export for existing hosts that only
  register the old button/list table. Other example applications were not run.

## Executed verification

Environment: Android 14/API 34 x86_64 emulator `Aether_NDK_Smoke_x86_64`, serial
`emulator-5554`; SDK platform 34, build-tools 36.0.0, NDK 29.0.14206865, SDK
CMake 4.1.2, JDK 20.0.1, Gradle 8.9/AGP 8.7.3. Existing verified dependency
sources reused; AE_NO_DEBUG_LOG=ON, Debug assertions retained.

| Check | Result |
|---|---|
| Final normal Debug instrumentation | 5/5 PASS, 40.007 s |
| Final debugLoadOnly instrumentation | 5/5 PASS, 41.807 s |
| Host Android embedded-session / model regression | 2/2 PASS, 10.94 s |
| Final Linux selected scope, private Xvfb/Openbox | 15/15 PASS, 46.67 s |
| Windows selected scope including Android host test | 24/25 PASS in batch; remaining native test PASS on focused rerun |
| Focused Windows native acceptance | 1/1 PASS, 11.10 s |
| Android native runtime symbols | No graph builder or distiller symbols |
| Android C++ runtime | Static; ELF NEEDED only liblog/libm/libdl/libc |

The Windows native focus check initially failed while the emulator retained
foreground ownership. The desktop was confirmed unlocked. On the passing
rerun, Computer Use activated only the test's main Window Catalog once; the
test then performed all child focus transitions itself. No child was activated
to satisfy an assertion, and no harness or production focus workaround was
added. The combined 25-test scope passes, but the initial batch failure remains
recorded rather than being reported as a clean 25/25 batch.

Instrumentation covers native Add, swipe, Close, reopen, Delete, RU/EN on both
catalog and an existing child, rotation, bottom/full-width model Close layout,
same-process Activity replacement, background save and process restart.
It rejects files other than user.bin. Fresh Debug leaves the previous file's
bytes and timestamp unchanged; load-only restores the active child and list.

The Linux result retains its documented WSLg exact-position limitation;
standard X11 acceptance is under Openbox. ARM64/physical Android devices,
Release packaging, native Wayland and other examples are NOT RUN here.

## Evidence and reproduction

Portable build/adb commands: [Android README](../../examples/surfaces_demo/android/README.md).
Local evidence under `.artifacts/`:

- `android-debug-final-build.log`, `android-loadonly-final-build.log`
- `android-debug-final-instrumentation.txt`, `android-loadonly-final-instrumentation.txt`
- `android-final-child.png`, `android-final-child-ui.xml`
- `android-port-child-layout-before-test.log`, `android-port-child-layout-after-test.log`
- `android-port-layout-final-windows-tests.log`, `android-port-cu-fast-test.log`
- `android-port-loadonly-map-probe.map`, `android-port-loadonly-factory-check.log`
- `android-port-linux-tests.log`, `android-port-linux-tests.xml`

Normal Debug was left installed with one child open in an isolated review
profile. The emulator remains running.
