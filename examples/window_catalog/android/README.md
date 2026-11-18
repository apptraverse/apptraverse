# Android WindowCatalog

This port uses the shared `AndroidApplicationHost`, application model session,
publication buffer, UI mirror and native presenters. Java owns the Activity and
forwards gestures through model Events. The model selects the active page and
formats localized text. The shared AppTraverse constraint engine runs beside the
UI mirror; Android measures native buttons in SP and applies its rectangles in
pixels once. Native scroll views expose the solver's overflow content extent;
they do not solve control geometry. Viewport/DPI observations go to LOCAL Events.
List commands carry holder IDs captured from the displayed mirror.

The native options menu projects the distilled `CatalogMenuDefinition`: Theme
offers From system, Light, Dark and Soft dark; Language lists English and
Русский by their endonyms. Labels and checks come from the published UI mirror.
The Activity passes only a native command token, which the native adapter maps
back to authored group/item indices and submits through `ModelObjectProxy`.
The old in-content language control and the generic appearance popup are not
presented by WindowCatalog. Other applications retain the generic backend menu.

## State and lifecycle

Portable USER and device LOCAL use separate ordinary `RamDomainStorage` instances
over embedded BACKING, routed by the supply's `ClientLocalState` roots. Add, swipe,
locale changes, native observations, appearance and child Close stay in memory. Close unloads the child
subgraph using the same model helper as the desktop ports.

`Activity.onStop` is the Android persistence boundary. It synchronously queues a
save after already accepted model work, then writes `<profile>/user.bin` and
`<profile>/local/user.bin` using the same native object format. This is necessary because Android may kill a
background process without delivering `onDestroy`. `onDestroy` does not request
another save. An abrupt kill before `onStop` is not a supported persistence
boundary. There are no gesture saves, object-layer files or checkpoint sidecars. LOCAL
contains the OS comparison baseline, window observations, appearance preferences
and selected language;
it is excluded from portable USER.

Normal **Debug** starts each process with fresh USER and never loads the previous
USER. Its profile is `files/<stateSubdir>/debug-run-<id>/user/user.bin` when the
Activity first goes into the background. Recreating the Activity in the same
process keeps the live model and remounts its presenters.
`onDestroy` releases the owning Activity's native mounts, JNI global references
and native settings/display/accessibility listeners. The Application keeps the
model worker and mirror; publications continue to apply while detached. A later
Activity remounts that mirror. An obsolete Activity cannot detach its replacement.

The published appearance also selects the Activity's native Material theme.
Switching between effective light and dark recreates the Activity so the standard
ActionBar and options menu receive the same native theme as the content. That
recreation reaches the ordinary `onStop` checkpoint boundary. System mode follows
the OS configuration; LOCAL continues to record the actual OS baseline separately.

**debugLoadOnly** keeps Debug assertions and symbols but reads/writes
`files/<stateSubdir>/user.bin`. Use it for interactive close/reopen demonstrations
and cold-restart acceptance of the consumer path; it does not change normal
Debug policy. The build-host exporter
creates the embedded initial graph for both variants. Its builder and distiller
are absent from the Android native runtime library. The generated header belongs
to each native build directory, not the source tree. ARM64/device builds and
Release packaging are not covered by the x86_64 emulator acceptance below. Other
example applications are outside this acceptance scope and were not run.

For manual persistence checks, install `app-debugLoadOnly.apk` with `adb install
-r`, then open the ordinary launcher icon. This uses the stable default profile
`files/window_catalog_state/user.bin`. Back or Home invokes `onStop`; removing
the task from Recents may then end the process. Opening the launcher icon again
loads that file. Normal `app-debug.apk` intentionally starts empty on each
process launch even though its previous run was saved. Installing with `-r`
keeps existing profiles; neither variant imports or deletes older Debug runs.
Supplies use deterministic common IDs. A schema-compatible supply update keeps
USER and LOCAL while replacing untouched distilled constraints; a model override
still wins. Schema changes within this S0 prototype require a fresh profile and
matching freshly distilled supplies; older experimental profiles are preserved.

## Incremental build

Open a Windows developer shell with MSVC available for the build-host exporter.
Use the existing dependency source cache; no clean or cache population is needed
for incremental work. The validated environment uses JDK 20.0.1, Gradle 8.9,
Android Gradle Plugin 8.7.3, SDK platform 34, build-tools 36.0.0, NDK
29.0.14206865 and SDK CMake 4.1.2. Set `JAVA_HOME` to that JDK and `ANDROID_HOME`
(or `ANDROID_SDK_ROOT`) to the installed SDK before invoking the wrapper from
this directory.

```powershell
.\gradlew.bat --console=plain :app:assembleDebug :app:assembleDebugAndroidTest `
  -Papptraverse.abiFilters=x86_64 -Papptraverse.testBuildType=debug

.\gradlew.bat --console=plain :app:assembleDebugLoadOnly :app:assembleDebugLoadOnlyAndroidTest `
  -Papptraverse.abiFilters=x86_64 -Papptraverse.testBuildType=debugLoadOnly
```

An existing CMake initial-cache file can be forwarded to both the NDK build and
the native host exporter with
`-Papptraverse.cmakeInitialCache=C:/absolute/path/dependency-sources.cmake`.
This is how the verification run reused the previously verified Æther patch
chain and dependency sources. That file may set `AE_NO_DEBUG_LOG=ON` to disable
object logging while retaining Debug assertions. `ANDROID_STL=c++_static` is
set by the application build.

For MinSizeRel, pass the matching supply prepared by Debug with
`-Papptraverse.debugSupplyHeader=C:/absolute/path/embedded_distilled_window_catalog_android_supply.h`.
The NDK consumer embeds that header; it does not run a graph builder or distiller.
R8 keeps the exact Java classes and members resolved by JNI.

## Emulator acceptance

The verified device is the existing `Aether_NDK_Smoke_x86_64` AVD, Android 14
(API 34), serial `emulator-5554`. Start an available emulator and wait for
`adb shell getprop sys.boot_completed` to return `1` before installing.

```powershell
adb -s emulator-5554 install -r app/build/outputs/apk/debug/app-debug.apk
adb -s emulator-5554 install -r app/build/outputs/apk/androidTest/debug/app-debug-androidTest.apk
adb -s emulator-5554 shell am instrument -w -r -e debugFresh true `
  com.apptraverse.windowcatalog.test/androidx.test.runner.AndroidJUnitRunner

adb -s emulator-5554 install -r app/build/outputs/apk/debugLoadOnly/app-debugLoadOnly.apk
adb -s emulator-5554 install -r app/build/outputs/apk/androidTest/debugLoadOnly/app-debugLoadOnly-androidTest.apk
adb -s emulator-5554 shell am instrument -w -r -e debugFresh false `
  com.apptraverse.windowcatalog.test/androidx.test.runner.AndroidJUnitRunner
```

Instrumentation uses a unique profile per test and keeps its process separate
from `:demo`. It checks the native controls and model behavior: empty-ring swipe,
Add, page navigation, child Close, reopen, Delete, RU/EN localization, rotation,
shared-solver Close geometry and Activity replacement with the same model
process. Working actions must create no `user.bin`. Backgrounding must create
one native file per partition. A subsequent process restart restores the active child in
`debugLoadOnly`; normal Debug starts empty and leaves the earlier file unchanged.
Two additional API34/Pixel-launcher cases use real Back/Home and swipe the
catalog card out of Recents and verify that its task ID disappears. Android may
retain a cached process after task removal; the tests terminate only that
remaining demo process, when necessary, and verify a different PID after
relaunch. They preserve the active child and RU locale in `debugLoadOnly`, and
verify fresh-state policy in normal Debug.
The host `apptraverse_window_catalog_android_embedded_session_test` separately
checks cold USER loading into fresh RAM/backing, stable IDs, closed/open lazy
state and exclusion of immutable localization bodies from USER.

The Gradle `connectedDebugAndroidTest` or
`connectedDebugLoadOnlyAndroidTest` tasks also select the corresponding
`debugFresh` runner argument when `apptraverse.testBuildType` matches the target.
Do not run the old Surfaces smoke script against this package; it targets a
different application.

## Device settings and transfer

The ordinary Activity options menu exposes Auto/Light/Dark and Soften dark theme.
Commands enter the presenter proxy and `UiDeviceState::SetAppearance` Events;
publications apply them to every mounted page. Android theme attributes supply
native colors, state feedback and control styles. Native SP measurement respects
the OS font scale; dark softening changes the local app palette only. The system
OS snapshot is sampled independently of the chosen app theme.

Before the first publication, the UI thread captures the real Android configuration
and model prepare compares it to loaded LOCAL through `ObserveEnvironment` with
Startup reason. While running, configuration dispatch, a settings ContentObserver,
display listeners and accessibility listeners use the same comparison path with
SystemNotification reason. Identical snapshots create no Event. Observers are
registered per mounted window and released with its native mount; there is no poll.
Supported baseline fields are density, font scale, system night mode, animator
scale/reduced motion, touch-exploration hint, semantic colors and display endpoints,
bounds, rotation and refresh rate. High-contrast/keyboard-cue APIs are not claimed.
The touch-exploration flag is an OS accessibility hint, not screen-reader detection.

For an initial optional LOCAL seed, launch the load-only Activity with
`--es localSeedSubdir <source-local-subdirectory-under-files>`. Copy portable USER
into the chosen `stateSubdir`; put the source's native LOCAL `user.bin` in that
seed directory. The seed is read only when destination LOCAL is absent. Android
then compares and adapts its OS baseline before presentation. A destination that
already has LOCAL keeps its full local graph and does not read the supplied seed
path. Ordinary Debug ignores prior USER, LOCAL and seed as part of fresh policy.

The target-ABI `apptraverse_window_catalog_android_environment_fixture PROFILE`
reads actual lifecycle saves with Android's freshly distilled BACKING and reports
portable model identity plus LOCAL baseline/revision/preferences. It rejects LOCAL
root leakage into USER. Acceptance evidence is recorded separately from this
source description in the [current Android system-settings validation](../../../docs/validation/window-catalog-android-settings-2026-11-17.md).

The acceptance-only `-Papptraverse.testSupplyPadding=24` build option produces a
compatible distinct supply at stable definition identities. It is used to verify
that untouched defaults update while a model Event override and own LOCAL remain.
It is omitted from normal builds. The target fixture also accepts `PROFILE
missing-control` to prepare the intentional required-native-binding abort case;
its ordinary `PROFILE` mode only inspects saved native objects.
