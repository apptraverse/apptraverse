# Apple SwiftUI Window Catalog experiment

Two Apple backends of Window Catalog, from the same checkpoint as the parallel Windows experiment. This branch does not merge that work and does not claim profile portability across backends.

## Checkpoint and result

| | |
| --- | --- |
| Checkpoint | `982b47366bca343f78dc6275fdac7b6eaceb84fb` |
| Tag | `checkpoint/web4-before-five-backends-982b473` |
| Branch | `experiment/window-catalog-apple-swiftui-982b473` |
| Implementation | `f68d5a9797943c511f26596fc8e6b91250bb08f0` |
| This report | the following commit on the same branch, which only records that hash |

Worktree: `/Users/nick/Projects/apptraverse-window-catalog-apple-swiftui`. The original clone and `experiment/window-catalog-five-backends-982b473` were not switched or rewritten.

## Machine and compilers

| Tool | Path / version |
| --- | --- |
| macOS | 13.7.8 (22H730), x86_64 Intel |
| Xcode | 15.2 (15C500b), `/Applications/Xcode.app` |
| `DEVELOPER_DIR` | `/Applications/Xcode.app/Contents/Developer` (process-local; `xcode-select` was not changed) |
| C / ObjC | `/opt/local/bin/clang-mp-20` (Clang 20.1.8) |
| C++ / ObjC++ | `/opt/local/bin/clang++-mp-20` (Clang 20.1.8) |
| Swift | `/Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/swiftc` (Apple Swift 5.9.2, swift-driver 1.87.3) |
| CMake | `/usr/local/bin/cmake` 4.3.4 |
| Ninja | `/usr/local/bin/ninja` 1.13.2 |
| macOS SDK | MacOSX.sdk, binary minos 13.0, sdk 14.2 (`vtool`) |
| iPhoneSimulator SDK | `iPhoneSimulator17.2.sdk`, binary platform `IOSSIMULATOR`, minos 17.2, sdk 17.2 |
| Æther pin | `79283b667cabf689da1059d618fe2a22abf51db4` via the repo CPM checkout. No hand-edited CPM cache. |

`/opt/local/bin/clang++` does not exist. `clang++` on `PATH` was not used. C++ flags (`-fno-rtti`, `-fno-exceptions`, `-fno-objc-exceptions`) are on the ObjC++ targets only. The Swift target is a separate static library. `AppleCatalogBridge.h` is the bridging header and does not include Æther or C++ headers. ARC is `-fobjc-arc` on the `.mm` sources.

## Simulator choice

`simctl list runtimes -j` has one installed runtime: iOS 17.2, identifier `com.apple.CoreSimulator.SimRuntime.iOS-17-2`, `isAvailable` true, build 21C62. There is no newer installed runtime, so there was no Intel-incompatible newer runtime to reject. The installed SDK and the installed runtime are the same 17.2; the runtime, not the SDK, was the selection.

Device: **iPhone 15 Pro Max**, UDID `EB9ED0F9-3C43-48A2-A18B-66C654FAAD4D`, state Shutdown, available, on that runtime. It is the highest iPhone model on the runtime (15 Pro Max above 15 Pro, 15 Plus, 15, and SE), not the first `grep` hit. No user simulator was deleted. No new Xcode or runtime was downloaded.

## Configure / build

macOS Debug, `build/macos-x86_64-debug`:

```bash
cmake -S . -B build/macos-x86_64-debug -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_COMPILER=/opt/local/bin/clang-mp-20 \
  -DCMAKE_CXX_COMPILER=/opt/local/bin/clang++-mp-20 \
  -DCMAKE_OBJC_COMPILER=/opt/local/bin/clang-mp-20 \
  -DCMAKE_OBJCXX_COMPILER=/opt/local/bin/clang++-mp-20 \
  -DCMAKE_Swift_COMPILER=$DEVELOPER_DIR/Toolchains/XcodeDefault.xctoolchain/usr/bin/swiftc \
  -DCMAKE_C_COMPILER_TARGET=x86_64-apple-macos13.0 \
  -DCMAKE_CXX_COMPILER_TARGET=x86_64-apple-macos13.0 \
  -DCMAKE_OBJC_COMPILER_TARGET=x86_64-apple-macos13.0 \
  -DCMAKE_OBJCXX_COMPILER_TARGET=x86_64-apple-macos13.0 \
  -DCMAKE_Swift_COMPILER_TARGET=x86_64-apple-macos13.0 \
  -DCMAKE_OSX_ARCHITECTURES=x86_64 \
  -DCMAKE_OSX_SYSROOT=$DEVELOPER_DIR/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0 \
  -DAPPTRAVERSE_BUILD_LINUX_GUI=OFF
cmake --build build/macos-x86_64-debug --target macos_window_catalog_demo
```

iOS Simulator Debug, `build/ios-sim-x86_64-debug`:

```bash
cmake -S . -B build/ios-sim-x86_64-debug -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_SYSTEM_NAME=iOS \
  -DCMAKE_OSX_SYSROOT=$DEVELOPER_DIR/Platforms/iPhoneSimulator.platform/Developer/SDKs/iPhoneSimulator17.2.sdk \
  -DCMAKE_OSX_ARCHITECTURES=x86_64 \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=17.2 \
  -DCMAKE_C_COMPILER=/opt/local/bin/clang-mp-20 \
  -DCMAKE_CXX_COMPILER=/opt/local/bin/clang++-mp-20 \
  -DCMAKE_OBJC_COMPILER=/opt/local/bin/clang-mp-20 \
  -DCMAKE_OBJCXX_COMPILER=/opt/local/bin/clang++-mp-20 \
  -DCMAKE_Swift_COMPILER=$DEVELOPER_DIR/Toolchains/XcodeDefault.xctoolchain/usr/bin/swiftc \
  -DCMAKE_C_COMPILER_TARGET=x86_64-apple-ios17.2-simulator \
  -DCMAKE_CXX_COMPILER_TARGET=x86_64-apple-ios17.2-simulator \
  -DCMAKE_OBJC_COMPILER_TARGET=x86_64-apple-ios17.2-simulator \
  -DCMAKE_OBJCXX_COMPILER_TARGET=x86_64-apple-ios17.2-simulator \
  -DCMAKE_Swift_COMPILER_TARGET=x86_64-apple-ios17.2-simulator \
  -DAPPTRAVERSE_BUILD_LINUX_GUI=OFF \
  -DBUILD_TESTING=OFF
cmake --build build/ios-sim-x86_64-debug --target ios_window_catalog_demo
```

Repeat configure is not required. Later builds were incremental `cmake --build` of those targets only.

## App locations

| Platform | Bundle | Binary |
| --- | --- | --- |
| macOS | `com.apptraverse.experiment.windowcatalog.macos` | `build/macos-x86_64-debug/examples/surfaces_demo/macos/macos_window_catalog_demo.app` |
| iOS Simulator | `com.apptraverse.experiment.windowcatalog.ios` | `build/ios-sim-x86_64-debug/examples/surfaces_demo/ios/ios_window_catalog_demo.app` |

The iOS binary is Mach-O x86_64, `LC_BUILD_VERSION` platform `IOSSIMULATOR`, minos 17.2. Linked frameworks are UIKit, Foundation, CoreGraphics, SwiftUI, libc++, libSystem, and `/usr/lib/swift`. No `/opt/local` library is on the link line. The macOS demo links AppKit, Foundation, SwiftUI, and the system libc++; it does not link MacPorts dylibs.

## Results

| Check | macOS | iOS Simulator |
| --- | --- | --- |
| Debug build of the Window Catalog demo | PASS | PASS |
| UI E2E steps 1–11 through the real UI | PASS | NOT RUN |
| Save, process exit, cold restart without wiping the profile | PASS | NOT RUN |
| Native geometry restored after cold restart | PASS (`96,92,700,492` main; child `140,188,520,332`) | n/a |
| Rotation does not rewrite desktop geometry | n/a | NOT RUN |
| Background / foreground does not start a second runtime | n/a | NOT RUN |
| `apptraverse_window_catalog_headless_test` | PASS | n/a (headless is host) |
| `apptraverse_application_model_runtime_test` | PASS | NOT RUN |
| `apptraverse_application_ui_session_test` | PASS | NOT RUN |
| `apptraverse_model_persistence_runtime_ref_test` | PASS | NOT RUN |
| `apptraverse_checkpoint_load_cycle_persistence_test` | PASS | NOT RUN |
| `apptraverse_application_contract_headless_test` | PASS | NOT RUN |
| `apptraverse_minimal_app_headless_test` | FAIL | NOT RUN |
| `apptraverse_object_clone_test` | FAIL | NOT RUN |
| `apptraverse_object_origin_facet_test` | FAIL (abort) | NOT RUN |
| Windows / Linux / Android / Web targets | NOT RUN | NOT RUN |

Headless `apptraverse_window_catalog_headless_test` does not link AppKit, UIKit, SwiftUI, or CoreGraphics.

### macOS UI E2E

Driver: `tools/experiments/apple_window_catalog_macos_e2e.py`. It clicks System Events elements by accessibility identifier and the native `AXCloseButton`. It waits on `catalog.revision` and `catalog.save.status`. It does not call `AddWindow`, `Commit`, or model setters.

Profile: `/tmp/wc-e2e-macos-state`. Deleted only before the first launch. The cold restart reused it.

Observed sequence:

1. Revision 1, empty list, frame `80,80,640,480`, session `1`, selection `none`. Add / Open / Delete / RU / Save. Open and Delete disabled.
2. Two adds. Rows `108933539` “Window 1” and `1057458202` “Window 2”. Windows `catalog.window.145758559` and `catalog.window.1661113322`.
3. Language. Title “Каталог окон”, rows “Окно 1” / “Окно 2”, buttons “Добавить” / “Открыть” / “Удалить” / “EN”.
4. Row 0 selected (`catalog.selection` = `0`).
5. Native close of `catalog.window.145758559`. Both rows remain. The other window still accepts a geometry change (`140,188,520,332`).
6. Open restores the same window id `145758559` and title “Окно 1”.
7. Delete removes row `108933539` and that window. Row `1057458202` “Окно 2” remains.
8. Main window moved. Frame becomes `96,92,700,492`. Save status `confirmed`.
9. Native close of the main window. Process exit status 0.
10. Cold launch of the same directory. Language stays Russian. The remaining row id is still `1057458202`. Both frames match the saved values. Session label stays `1`.
11. One more Add. Row “Окно 3”, new row id `1628201419`, new window `catalog.window.3128267316`.

Screenshots: `docs/experiments/apple-swiftui/screenshots/macos/01-initial.png` through `08-add-after-reload.png`.

### iOS Simulator blocker

The `.app` is built, signed (`codesign --force --sign -`), and is a real iOS Simulator x86_64 binary. It was not installed and the UI E2E was not run.

`simctl boot EB9ED0F9-3C43-48A2-A18B-66C654FAAD4D` fails before the device leaves Shutdown:

```
domain=NSPOSIXErrorDomain, code=60
Unable to boot the Simulator. launchd failed to respond.
domain=com.apple.SimLaunchHostService.RequestError, code=4
Failed to start launchd_sim: could not bind to session, launchd_sim may have crashed or quit responding
```

The same error is in `~/Library/Logs/CoreSimulator/CoreSimulator.log` for session `com.apple.CoreSimulator.SimDevice.EB9ED0F9-3C43-48A2-A18B-66C654FAAD4D`. Retries: `simctl boot` directly, `launchctl asuser 501`, `osascript do shell script`, and a File > Open Simulator menu click. `Simulator.app` itself starts. `launchd_sim` does not bind to the window-server session from this environment. No device data was deleted and CoreSimulatorService was not left as the fix. This is an external session blocker, not a missing runtime and not an arm64-only runtime.

`examples/surfaces_demo/ios/run_window_catalog_simulator.sh --print-device` selects the runtime and device without booting. A full launch from that script hits the same boot error.

## Manual launch

macOS:

```bash
build/macos-x86_64-debug/examples/surfaces_demo/macos/macos_window_catalog_demo.app/Contents/MacOS/macos_window_catalog_demo \
  --state-dir /tmp/wc-e2e-macos-state
python3 tools/experiments/apple_window_catalog_macos_e2e.py
```

The E2E script removes `/tmp/wc-e2e-macos-state` unless `WC_FRESH=0`. A cold-restart check must not delete that directory between the two launches; the script only deletes it at the start.

iOS, once `launchd_sim` can bind:

```bash
examples/surfaces_demo/ios/run_window_catalog_simulator.sh --print-device
examples/surfaces_demo/ios/run_window_catalog_simulator.sh
```

Do not `simctl uninstall` between save and the cold launch. Default storage is the app container Application Support directory `AppTraverseWindowCatalogExperiment`.

## Common files, separate from the Apple backends

These are the shared edits. The parallel Windows branch may touch the same boundary.

- `examples/surfaces_demo/common/window_catalog_platform_graph.h` declares `BuildWindowCatalogGraph` and `EnsureWindowCatalogGraphRegistration`. Lifecycle includes this header, not the Windows graph header and not generated Windows supply.
- `examples/surfaces_demo/windows/window_catalog_graph.h` now only includes that declaration header.
- `window_catalog_lifecycle.h` / `.cpp`: `request_persist` posts `SaveWindowCatalogPersistentSnapshot` on the model thread and completes there. Shutdown save is unchanged. Apple Debug distillation does not include the embedded Windows supply header.
- `CatalogListContainer::HandleListRowSelectionChanged` calls `WindowCatalog::SubmitSelectionFromUi`. Platform presenters call the existing container method. Activation stays `HandleListRowActivated` → `OpenRow`.
- Root `CMakeLists.txt`: `enable_language(OBJCXX)` on Apple, and the two UI libraries.

Apple-only: `include/apptraverse/platform/{apple,macos,ios}`, `src/platform/{apple,macos,ios}`, `examples/surfaces_demo/macos/window_catalog_*`, `examples/surfaces_demo/ios/window_catalog_*`, the two demo `CMakeLists.txt` targets, and `tools/experiments/apple_window_catalog_macos_e2e.py`.

## Ownership

| Boundary | Where it lives |
| --- | --- |
| Model Domain, events, clone, save | Model thread, inside `WindowCatalogModelSession::Run` |
| Publication | `on_published` after the model mutex is released. The host `dispatch_async`s to the main queue, applies presenters, then `TakePublishedCopy` / `NotifyPublicationConsumed` |
| UI mirror | Main thread, applied by `ApplicationUiSession` before presenter `OnModelChanged` |
| SwiftUI | `AppleCatalogModel` is a projection of `AppleCatalogProjection`. It is not a second catalog |
| Actions | SwiftUI / `NSButton` → ObjC block → presenter `NotifyClickedFromUi` / `SubmitListRowSelectionChangedFromUi` / `NotifyUserClose` → `ModelObjectProxy` → model event |
| Native windows | Created in `MacDesktopWindowPresenter::OnLoad`, released in `OnUnload`. Object Load/Save does not create an `NSWindow` |
| iOS pages | One `UIWindow` and one `UIHostingController` created in `IosApplicationHost::Start`. `started_` refuses a second runtime. Rotation calls `onViewport` only; `report_desktop_geometry` is false |
| Storage | `DirectoryDomainStorage` under `--state-dir` or Application Support `AppTraverseWindowCatalogExperiment`. Explicit Save and main-window close both use the model-thread snapshot. iOS does not save on background |

## Architectural notes for the seven-backend comparison

Problems hit, and what was done:

- ObjC++ `-fno-exceptions` still defines `__EXCEPTIONS` while ObjC exceptions are on, so `no_exceptions.h` aborts the compile. The Apple UI libraries and demos compile ObjC++ with `-fno-objc-exceptions`.
- `NSHostingView` publishes `accessibilityIdentifier` and not a SwiftUI `Button` title. macOS chrome and rows are `NSButton`s inside the SwiftUI tree so System Events can read the title. The action is still the projection callback. iOS keeps a SwiftUI `Button` with the same identifier and label.
- SwiftUI `.disabled` is what System Events reports as `AXEnabled`. Setting `NSButton.isEnabled` alone was overwritten. The macOS button uses both.
- A parent `.accessibilityIdentifier("catalog.list")` replaced the row button’s identifier. The list container is `.accessibilityElement(children: .contain)`.
- `request_persist` was added on the shared session so Save does not depend on `applicationWillTerminate` or on stopping the model thread. Completion hops back to the main queue and sets `catalog.save.status` to `confirmed`.
- One `NSHostingView` is installed per `NSWindow`. Later publications mutate the projection and bump `revision`. Callbacks are cleared in `AppleUnbindPresentationHost` on unload.

Duplication to discuss, not redesigned here:

- macOS and iOS presenter classes both only sync the shared projection. macOS additionally owns `NSWindow`, frame conversion (model top-left versus screen bottom-left), and native close. iOS has no `NSWindow` per logical window; logical windows are pages in one `UIHostingController`.
- The Swift file is shared, with `#if os(macOS)` / `#if os(iOS)` for the button representation, hosting install, and the phone root. There is no universal Apple framework.
- Class ids for the Apple presenter registrations are Apple-specific. A Windows profile is not a macOS profile.
- Explicit Save is a host button (`catalog.button.save`), not a model `Catalog*Button`. The model snapshot function is the shared one.
- macOS child close is the window’s `AXCloseButton` → `windowShouldClose` → `UserCloseWindow`. The SwiftUI “Close” control on a child page calls the same `onCloseWindow` path. iOS has only that control.
- Viewport reporting exists only on iOS. macOS reports desktop geometry and will persist it. iOS must not.

## Tests that failed on this Mac, outside the Apple presenters

`apptraverse_minimal_app_headless_test` stops at `tests/minimal_app_headless_test.cpp:558`: `UiAuxiliaryRowList(ui_app).selection_index == -1`. The minimal-app sources were not edited.

`apptraverse_object_clone_test` prints `object_clone_test FAIL: USER/A unexpected Obj S layers`.

`apptraverse_object_origin_facet_test` prints `object_origin_facet_test FAIL: unexpected Obj S layers on A in USER (expected S0+S1 with origin facet)` and aborts (exit 134).

Those assertions are in the unchanged clone/origin fixtures. This experiment did not retune S-layer versioning.

Windows, Linux, Android, and Web checks were not executed here. They are NOT RUN.
