# Android WindowCatalog

This port uses the shared `AndroidApplicationHost`, application model session,
publication buffer, UI mirror and native presenters. Java owns the Activity and
forwards gestures and actual viewport measurements to model Events. The model
selects the active page, formats localized text and calculates pixel layouts.
List commands carry holder IDs captured from the displayed mirror.

## State and lifecycle

USER is one ordinary `RamDomainStorage` over the embedded BACKING. Add, swipe,
locale changes, resize and child Close stay in memory. Close unloads the child
subgraph using the same model helper as the desktop ports.

`Activity.onStop` is the Android persistence boundary. It synchronously queues a
save after already accepted model work, then writes one `user.bin` under the
selected profile directory. This is necessary because Android may kill a
background process without delivering `onDestroy`. `onDestroy` does not request
another save. An abrupt kill before `onStop` is not a supported persistence
boundary. There are no gesture saves, object-layer files or checkpoint sidecars.

Normal **Debug** starts each process with fresh USER and never loads the previous
USER. Its profile is `files/<stateSubdir>/debug-run-<id>/user/user.bin` when the
Activity first goes into the background. Recreating the Activity in the same
process keeps the live model and remounts its presenters.

**debugLoadOnly** keeps Debug assertions and symbols but reads/writes
`files/<stateSubdir>/user.bin`. It exists for cold-restart acceptance of the
consumer path; it does not change normal Debug policy. The build-host exporter
creates the embedded initial graph for both variants. Its builder and distiller
are absent from the Android native runtime library. The generated header belongs
to each native build directory, not the source tree. ARM64/device builds and
Release packaging are not covered by the x86_64 emulator acceptance below. Other
example applications are outside this acceptance scope and were not run.

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
model-derived Close geometry and Activity replacement with the same model
process. Working actions must create no `user.bin`. Backgrounding must create
one file. A subsequent process restart restores the active child in
`debugLoadOnly`; normal Debug starts empty and leaves the earlier file unchanged.
The host `apptraverse_window_catalog_android_embedded_session_test` separately
checks cold USER loading into fresh RAM/backing, stable IDs, closed/open lazy
state and exclusion of immutable localization bodies from USER.

The Gradle `connectedDebugAndroidTest` or
`connectedDebugLoadOnlyAndroidTest` tasks also select the corresponding
`debugFresh` runner argument when `apptraverse.testBuildType` matches the target.
Do not run the old Surfaces smoke script against this package; it targets a
different application.
