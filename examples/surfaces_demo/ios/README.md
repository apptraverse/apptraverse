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

## Simulator launch

List available devices with `xcrun simctl list devices available`. After booting
a device, install and launch the embedded consumer for persistent use:

```sh
xcrun simctl install DEVICE_ID build/ios-simulator-debug/examples/surfaces_demo/ios/ios_window_catalog_demo_load_only.app
xcrun simctl launch DEVICE_ID org.apptraverse.WindowCatalog.ios.loadonly
```

The separate Debug bundle identifier is `org.apptraverse.WindowCatalog.ios`.
First simulator-service startup/installation may be slow on older hosts; allow
it to finish rather than erase device data. The acceptance runner allows up to
300 seconds for each cold install/launch.

## Behavior and storage

Add creates and activates a page. The catalog list selects a holder; Open or
row double-tap opens/activates it. Delete removes the selected entry. The bottom
page control and horizontal swipes address the model's mobile ring. Close is a
localized model button; closing an active child follows the shared model's
remaining-window activation order.
Selection and active page are distinct model state. RU/EN updates all pages.
Layout follows the real safe-area canvas and responds to rotation. The common
model's viewport Event places the child Close control with 12dp margins and a
48dp height; iOS uses logical points at density 160.

Native UIApplication deactivation queues a save on the live model thread,
behind accepted commands. The ordinary snapshot updates RAM USER and writes
one `user.bin` under a UIKit background task. The same model graph, runtime,
mirror, presenters and view adapters stay alive. Native activation continues
that session, without exposing the initial loading placeholder. Repeated
inactive/background notifications are coalesced; a later active-to-inactive
transition saves newer accepted work. Only explicit application shutdown stops
and joins the model thread and releases its presenters. No graph keepalive,
per-click/resize flush or new checkpoint format is added.

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

The runner installs test-only apps and verifies result files from three actual
processes: Debug fresh, embedded seed, embedded cold restart. It checks native
UITableView selection and UIPageControl callbacks, model page intents, rotation,
Close/Open/Delete, localization, lazy identities, live saves and one USER file.
Across deactivation/activation the test compares actual bridge, scene, adapter
and native-window object identities, not only restored values. A separate test verifies displayed-row identity
when native callbacks outlive a structural snapshot. PNGs capture actual app
window content; test expectations live outside the application USER profile.

An additional embedded scenario leaves the seeded app running, switches to the
system Settings app through simctl and observes one USER file. Reactivation must
retain the same process, bridge and presenter adapters, and accept more model
work. A second OS deactivation saves that later work; the process is then
terminated and a separate cold launch verifies the same profile. This exercises
real OS lifecycle delivery as well as the in-process native notification checks.

The additional XCUITest project in `tests/ios_ui_clicks` exercises actual
Simulator taps/double-taps/swipes against the installed product Debug app.
The validation report records the executed scope and results.
