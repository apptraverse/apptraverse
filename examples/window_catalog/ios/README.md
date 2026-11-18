# iOS WindowCatalog

WindowCatalog uses the shared C++20 model, Events, graph factory, clone/history,
model runtime, UI mirror and ordinary RAM BACKING/USER. SwiftUI renders model
rectangles; UIKit supplies the list and page control. Two thin catalog-specific
presenters adapt the generic iOS backend. There is no copy of the runtime or
mirror in the application.

## Build

On a Mac with Xcode (including an installed iOS Simulator SDK/runtime), CMake,
Ninja and MacPorts clang 20:

```sh
scripts/build_ios_window_catalog.sh
```

The optional arguments select the host and simulator build directories:

```sh
scripts/build_ios_window_catalog.sh /absolute/host-debug /absolute/ios-debug
```

The host Debug preparation executable uses the same iOS graph to produce the
embedded supply; it is not executed as a simulator binary. The runtime consumer
embeds that supply. The toolchain defaults to the host architecture and iOS 17.2;
configure `CMAKE_OSX_ARCHITECTURES` / `CMAKE_OSX_DEPLOYMENT_TARGET` explicitly for
a different installed simulator. C/C++/ObjC++ use C++20 without RTTI/exceptions;
Swift and native Apple frameworks keep their platform ABI.

Targets:

- `ios_window_catalog_demo.app`: **WindowCatalog Debug**, ordinary Debug,
  fresh graph once per process. A cold launch deliberately starts empty.
- `ios_window_catalog_demo_load_only.app`: **WindowCatalog**, explicit embedded
  consumer for normal use, restoring its saved USER on a cold launch.
- `apptraverse_ios_window_catalog_acceptance_debug.app` and `_load_only.app`:
  separate native acceptance fixtures, not product test modes.

These are simulator bundles, not signed physical-device or App Store packages.
Other example applications and other platforms are outside this port's scope.

### MinSizeRel

Configure a separate Ninja iOS build with `CMAKE_BUILD_TYPE=MinSizeRel` and a
matching Debug-prepared `APPTRAVERSE_WINDOW_CATALOG_IOS_DEBUG_SUPPLY_HEADER`
(leave `APPTRAVERSE_WINDOW_CATALOG_IOS_HOST_EXPORTER` empty when using a frozen
header). Build `ios_window_catalog_demo_load_only`.

`APPTRAVERSE_IOS_MIN_SIZE_OPTIMIZATIONS` defaults to ON for iOS MinSizeRel:
C/C++/ObjC++ use `-Oz`, ThinLTO, hidden symbols and function/data sections;
the linker removes unreachable code; a post-build `strip -x` removes local
symbols. C++ exceptions/RTTI and Objective-C exceptions are disabled. Swift
keeps `-Osize -wmo` and its platform ABI. The compiler's matching LLVM archive
tools are used for bitcode; MacPorts Clang selects its matching libLTO when
linking. Debug and other platforms retain their existing flags.

`APPTRAVERSE_BUILD_IOS_ACCEPTANCE=ON` also enables the existing native fixtures
under MinSizeRel. Both fixture variants then use embedded startup in fresh test
profiles. This explicitly enables Swift test access; turn acceptance OFF and
rebuild the product before measuring its final production size. The size report
distinguishes that final binary's smoke check from the full fixture runs.

## Simulator launch

List available devices with `xcrun simctl list devices available`. After booting
a device, install and launch the embedded consumer for persistent use:

```sh
xcrun simctl install DEVICE_ID build/ios-simulator-debug/examples/window_catalog/ios/ios_window_catalog_demo_load_only.app
xcrun simctl launch DEVICE_ID org.apptraverse.WindowCatalog.ios.loadonly
```

The separate Debug bundle identifier is `org.apptraverse.WindowCatalog.ios`.
First simulator-service startup/installation may be slow on older hosts; allow
it to finish rather than erase device data. The acceptance runner allows up to
300 seconds for each cold install/launch.

## Behavior and storage

Add creates and activates a page. A tap selects a holder. Open or a
double-tap opens or activates it. Delete removes the selected holder, and
Close leaves its row available for reopening. A SwiftUI page picker and
horizontal swipes navigate the model's mobile ring. Language and Theme are
the same distilled commands used on macOS. MinimalApp shares the host and
supplies a single active page with its own Open Detail and Close Detail actions.

Both apps use the Apple SwiftUI constraint layout. Controls are measured in
points and placed by the shared C++ solver against the current safe-area
viewport. The catalog fits its list and actions horizontally in landscape and
vertically in portrait. iOS records the canvas in LOCAL without converting a
rotation into a portable window frame request.

SwiftUI scene deactivation queues a save on the continuing model thread after
accepted commands. The OS background assertion allows the writes of USER
and `local/user.bin` to finish. Reactivation keeps the same model graph and
presenter adapters. Explicit shutdown drains the worker and releases the
presenters.

Ordinary Debug starts fresh on a new process launch. Explicit load-only startup
reads the saved USER against its embedded supply. Portable platform supplies of
the same current revision use deterministic identities and common presenter
schemas. Older experimental profiles are not migrated and must be preserved
separately rather than mixed with a different supply revision.

There is no reliable final callback for a suspended app being killed. Saving
starts at deactivation rather than waiting for `applicationWillTerminate` or
doing I/O from a POSIX signal handler.

## Native checks

With the acceptance bundles built and a simulator already booted:

```sh
python3 scripts/test_ios_window_catalog.py \
  --build /absolute/ios-debug --device DEVICE_ID \
  --artifacts /absolute/evidence
```

The runner checks Debug fresh, embedded seed and cold processes. It covers
layout on rotation, page selection, Close/Open/Delete, language and theme,
lazy identities, and real app switching to verify USER/LOCAL saves while the
live model continues.

After installing the product Debug bundles, run the touch tests for both apps
on the same device:

```sh
xcodebuild test -project tests/ios_ui/AppTraverseIosUi.xcodeproj \
  -scheme AppTraverseIosUi -configuration Debug \
  -destination "platform=iOS Simulator,id=DEVICE_ID" \
  -parallel-testing-enabled NO
```

The touch tests exercise rotation, list taps and double-taps, page buttons,
swipes, and MinimalApp's Detail flow.
