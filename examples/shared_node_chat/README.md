# SharedNode chat

SharedNode chat has targets for Windows, GTK Linux, Android, the browser, macOS and
iPhone. A host creates a room and participants join using the host Aether ID.
All targets use the same chat model and Æther stream format.
On Windows the chat follows the system UI language and theme at startup.
Russian Windows selects Russian; other Windows languages use English.
Override either setting for one process with `--lang system|en|ru` and
`--theme system|light|dark|soft-dark`. Windows High Contrast takes precedence.
The window uses the same native controls, font/DPI measurements, accessibility
notifications and constraint solver as WindowCatalog. Each profile restores
the normal window rectangle, preferred monitor and maximized state.

The native `shared_node_chat_headless` target uses the same `ChatSession`,
SharedNode journal and embedded transport supply without a UI graph or window.
Run `shared_node_chat_headless PROFILE NAME` on Windows, Linux or macOS, then enter
`host`, `join HOST-AETHER-UID`, `send MESSAGE`, or `stop` on stdin. It prints
the local UID and newly materialized messages. Use a separate private profile
per process. Push configuration is optional and not needed for ordinary chat.

Enable `APPTRAVERSE_BUILD_AETHER_LINK=ON` and
`APPTRAVERSE_BUILD_SHARED_NODE_CHAT=ON` in an existing Debug build. The SDK must
match the exact public revision in `cmake/shared_aether.cmake`.
`APPTRAVERSE_AETHER_CLIENT_SOURCE` accepts a clean checkout at that revision.
The default native configuration uses direct `P2pStream` over TCP, with no
SafeStream or adaptive stream. Native archive fragments carry whole SharedSync
messages; SharedSync owns in-process RAM ACK/retry and duplicate suppression.
This SDK uses Æther's delivery envelope on the default stream, including its
cloud route. A participant built with the earlier raw-message SDK requires a
compatible SDK build to join. Ordinary builds leave both options OFF. Build
target `shared_node_chat`.

The current chat graph, messages, drafts, names and pending receipts live only
in RAM. Send/receive, checkpoint and shutdown never write chat business USER.
Closing a process discards that graph; a returning participant explicitly joins
the live host again and may receive its retained snapshot. The SDK identity
remains private under the independent `transport/sdk` profile. Native device
preferences and window placement alone persist in `local/user.bin`, using the
ordinary native object format. Old experimental chat business files are neither
read nor deleted. Earlier platform restore reports retain their historical scope
and are superseded by this policy.

The Windows GUI reopens its profile's UI LOCAL in both Debug and Release.
Other Debug hosts use fresh UI LOCAL under `debug-run-*` unless their load-only
mode is selected.
Shipping consumers use the matching embedded supply; the Debug exporter prepares
that supply without adding application migrations.

```powershell
./examples/shared_node_chat/run_demo.ps1
```

This opens Host, Alice and Bob with separate private SDK identity profiles under
`.artifacts/shared-chat-demo`. The Host creates its room automatically. Click
**Copy** beside its **My Aether ID**, paste that ID into a client's **Host Aether
ID** field, and click **Join** (or press Enter). No room number is exposed.
Each role has one visible ID field and one action button.

For individual processes:

```powershell
./build/shared-node-debug/examples/shared_node_chat/shared_node_chat.exe --host
./build/shared-node-debug/examples/shared_node_chat/shared_node_chat.exe --client ff1f6401-bbee-4d06-b4e2-551ee3a9198e
./build/shared-node-debug/examples/shared_node_chat/shared_node_chat.exe --client --profile .artifacts/chat-alice --name Alice --lang ru --theme soft-dark
```

`--host` automatically creates a room after Aether initialization. `--client
HOST-AETHER-ID` automatically resolves and joins the host's room. `--client`
without an ID (also the default when no role is supplied) allows manual entry.
The roles are mutually exclusive. `--profile PATH` and `--name NAME` remain
optional. Windows places its default role profiles (`shared-chat-host-state`
and `shared-chat-client-state`) under `%LOCALAPPDATA%\AppTraverse\SharedNodeChat`.
Launching from a directory with an existing role profile continues to use that
profile. SDK identities are restored. Chat business state starts empty after process restart.
Messages committed during a live-process transport outage synchronize on reconnection.

Room resolution uses retried request/reply frames on the existing Aether link;
only replies from the selected host are accepted. The room object ID remains an
internal SharedNode identifier. Both peers need this protocol revision for
UID-only connection; older `UID/room-number` invitations are no longer accepted.

Each concurrently running process requires its own profile. Keep profiles
private: they contain SDK identity credentials.

The model publishes changed values after materialized Node changes or status
transitions, with runtime transport/journal diagnostics for acceptance tests.
The Windows consumer receives `PostMessage` notifications and reads `ChatView`
then; the Windows UI has no refresh timer. Native presence is sampled on the
model thread. Accepted model changes and their delivery obligations are committed
together to RAM before SharedSync acknowledges them. Runtime handles and fragment
assemblies never enter object storage.

The chat displays local connectivity. `ChatView` also exposes native presence
observations for remote participants.
The link sends as soon as transport capacity permits. SharedNode keeps a
message until its application ACK and retries it every second if the ACK is
missing, including while the recipient is stopped. SDK write completion alone
does not confirm delivery. Duplicate Events are applied only once.

The library adapter keeps presence disabled by default. The chat explicitly
enables observation with `gate_delivery_on_presence=false`, so Unknown/Offline
status does not prevent durable delivery attempts. Historical acceptance is in
[historical validation](../../docs/validation/shared-node-ack-retry-2026-11-12.md).

Explicit cloud acceptance (not part of ordinary CTest):

```powershell
cmake --build build/shared-node-debug --target apptraverse_shared_chat_win32_test
./build/shared-node-debug/examples/shared_node_chat/apptraverse_shared_chat_win32_test.exe --exe "$PWD/build/shared-node-debug/examples/shared_node_chat/shared_node_chat.exe"
```

The acceptance controller owns three processes and fresh private profiles. It
requires actual admission, Unicode/all-peer delivery, continued use while a
peer is stopped, the same SDK identity and room history recovered from live peers after restart,
delivery of offline messages, localized native accessibility names, theme
selection, and cold restoration of the maximized and normal window frames.
A bootstrap failure does not count as a chat PASS.

Headless delivery latency, using the same production `ChatSession` in two
separate processes with real Aether and private fresh profiles:

```powershell
cmake --build build/shared-node-debug --target apptraverse_shared_chat_latency_worker
python tests/run_shared_chat_latency.py --exe build/shared-node-debug/examples/shared_node_chat/apptraverse_shared_chat_latency_worker.exe --out .artifacts/shared-chat-latency --samples 100 --warmup 10 --payload-bytes 128
```

This explicit network benchmark records a QPC timestamp immediately before
`ChatSession::Send` and timestamps in the remote process. `observed_ms` ends
when a change notification wakes the consumer and it reads `ChatView`;
`presented_ms` ends when that event-driven headless display builds its text.
The consumer waits on a condition variable, with no presentation polling or
periodic display cadence. Delivery, RAM apply and ACK/retry are included. Physical button
input, sender edit conversion and actual Win32 painting are excluded.
Use a fresh output directory. Raw samples, worker logs and phase/direction
statistics are saved there. First messages and warmup are reported separately;
the default measured phase is 50 messages in each direction. Normal CTest does
not register cloud identities or run this benchmark.
The timing controller also checks idle notifications and rejects legacy
`native`/`root.id` sidecars. Its historical timing numbers do not establish
performance of the current P2pStream implementation.
The [previous measurements](../../docs/validation/shared-node-chat-latency-2026-11-12.md)
used the superseded polling/disk configuration and include an intermittent stall.
Current [event-driven RAM checks and timing](../../docs/validation/shared-node-chat-event-driven-2026-11-12.md)
are recorded separately.
The subsequent [backpressure latency fix and controlled measurements](../../docs/validation/shared-node-chat-latency-backpressure-2026-11-12.md)
remove the extra ACK-timeout wait after a locally refused send.

## macOS (SwiftUI)

The macOS host uses SwiftUI `App`, a single `Window` scene, `Commands` and
controls, with an Objective-C++ bridge to the shared chat model. SwiftUI measures
and arranges the controls with the current system font and appearance. A thin
AppKit attachment observes the SwiftUI-owned native window's placement and
forwards APNs and termination callbacks; it does not create windows or menus.
Model notifications dispatch to the main queue; published values update the
SwiftUI presenter. There is no UI polling timer or separate chat model.
Editing fields, room creation, Join, Copy and Send enqueue model commands.
Closing the last window drains/stops the session. Deactivation leaves it alive.
Standard Copy/Paste shortcuts are available through the Edit menu.
Native settings are captured before the initial publication and observed live.
Moves and resizes update device-local outer placement through model Events;
LOCAL restore and monitor fallback apply the published placement without
rewriting the portable semantic frame. Only LOCAL and the SDK identity persist.

Use Xcode's Swift compiler and a C++20 compiler (MacPorts Clang 20 is supported).
For example, from the repository root, with Xcode selected:

```sh
cmake -S . -B build/chat-macos -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_COMPILER=/opt/local/bin/clang-mp-20 \
  -DCMAKE_CXX_COMPILER=/opt/local/bin/clang++-mp-20 \
  -DCMAKE_OBJC_COMPILER=/opt/local/bin/clang-mp-20 \
  -DCMAKE_OBJCXX_COMPILER=/opt/local/bin/clang++-mp-20 \
  -DCMAKE_OSX_SYSROOT="$(xcrun --sdk macosx --show-sdk-path)" \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0 \
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DAE_NO_DEBUG_LOG=ON \
  -DAPPTRAVERSE_BUILD_AETHER_LINK=ON \
  -DAPPTRAVERSE_BUILD_SHARED_NODE_CHAT=ON
cmake --build build/chat-macos --target apptraverse_chat_supply_exporter
build/chat-macos/apptraverse_chat_supply_exporter "$PWD/build/chat-macos/chat_supply.h"
cmake -S . -B build/chat-macos \
  -DAPPTRAVERSE_CHAT_SUPPLY_HEADER="$PWD/build/chat-macos/chat_supply.h"
cmake --build build/chat-macos --target shared_node_chat
open build/chat-macos/examples/shared_node_chat/macos/shared_node_chat.app
```

The default private profile is under
`~/Library/Application Support/AppTraverse/SharedNodeChat/client`.
Launch with `--host` to create a room automatically; the default `--client`
shows **Host Aether ID** and **Join**, matching the Windows protocol.
`--client HOST-UID` joins automatically. The host default profile ends in
`/host` instead of `/client`.
For several clients, launch the bundle executable directly with distinct
`--profile PATH --name NAME` arguments. An advisory lock rejects concurrent
use of one profile. Keep existing profiles paired with their supply revision.

The committed Apple UI supply serves both SwiftUI targets. After changing the
shared UI schema, build the Debug target `apptraverse_chat_apple_ui_exporter`
on a desktop host and pass it `examples/shared_node_chat/generated/apple/chat_ui_supply.h`
as its output path.

Explicit three-process SwiftUI/cloud acceptance:

```sh
cmake --build build/chat-macos --target apptraverse_shared_chat_macos_acceptance
python3 tests/run_shared_chat_macos.py \
  --exe build/chat-macos/examples/shared_node_chat/macos/apptraverse_shared_chat_macos_acceptance \
  --out .artifacts/shared-chat-macos --idle-seconds 180
```

The separate acceptance host compiles the production SwiftUI view and bridge,
adds stdin commands and screenshot output, and presses the SwiftUI buttons
through accessibility. It checks rendered accessibility text, three-peer
Unicode delivery, TCP activation and durable delivery after forced restarts.
The normal application has no command channel. Use a fresh output directory;
its profiles contain test credentials and must remain private.

## iPhone (SwiftUI)

The iOS screen uses the same Apple `ChatPresenter`, Objective-C++ bridge and
C++ `ChatSession` as macOS. It places messages above the keyboard-aware composer
and displays the participant list above the conversation. Publications drive the interface.
The default role is client: enter the Windows/macOS host Aether ID and tap Join.
`--host` and `--client HOST-UID` are also available as launch arguments.

On scene deactivation, iOS enqueues a model-worker checkpoint with a bounded
background task. The session and presenters stay alive. A cold launch restores
local UI preferences and the SDK identity. Rejoin the live host to recover the
room's messages.
SwiftUI supplies the initial and live display, accessibility and viewport
observations to LOCAL model Events. An explicit application termination drains
and stops the session; ordinary deactivation only checkpoints LOCAL.

For an installed iOS 17.2 Simulator, first produce the chat supply using the
macOS exporter above. Then, from the repository root:

```sh
cmake -S . -B build/chat-ios -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/cmake/toolchains/ios-simulator.cmake" \
  -DCMAKE_BUILD_TYPE=Debug -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
  -DAE_NO_DEBUG_LOG=ON -DBUILD_TESTING=OFF \
  -DAPPTRAVERSE_BUILD_EXAMPLES=OFF \
  -DAPPTRAVERSE_BUILD_AETHER_LINK=ON \
  -DAPPTRAVERSE_BUILD_SHARED_NODE_CHAT=ON \
  -DAPPTRAVERSE_CHAT_SUPPLY_HEADER="$PWD/build/chat-macos/chat_supply.h"
cmake --build build/chat-ios --target shared_node_chat --parallel 2
codesign --force --sign - build/chat-ios/examples/shared_node_chat/ios/shared_node_chat.app
xcrun simctl install booted build/chat-ios/examples/shared_node_chat/ios/shared_node_chat.app
xcrun simctl launch booted org.apptraverse.SharedNodeChat.ios
```

Profiles live in the application's private Application Support directory.
The simulator toolchain and ad-hoc signing above do not package a physical-device
release; device signing is a separate step.

Explicit native UI/cloud acceptance uses the installed product plus the
standalone `tests/ios_chat_ui/SharedChatUi.xcodeproj` XCTest runner. It taps the
real SwiftUI controls, checks Mac/iPhone messages, Unicode replies, participants,
rotation, background/foreground without a process replacement, and cold identity
restore followed by a live-room rejoin. Chat history itself stays in RAM.
Build its `SharedChatUi` scheme with `xcodebuild build-for-testing`, then run:

```sh
python3 tests/run_shared_chat_ios.py \
  --host build/chat-macos/examples/shared_node_chat/macos/apptraverse_shared_chat_macos_acceptance \
  --xctestrun PATH/TO/SharedChatUi_iphonesimulator17.2-x86_64.xctestrun \
  --device BOOTED-SIMULATOR-UUID --out .artifacts/shared-chat-ios
```

This creates fresh private cloud identities and requires a new output directory.
It never joins or modifies a user's manually running chat.

## Linux GTK

For interactive Debug timing, configure the chat build with
`-DCMAKE_CXX_FLAGS_DEBUG="-g -Og"`. This keeps assertions and symbols while
optimizing the object graph traversal shared by the model and transport.
On WSLg, `GDK_BACKEND=x11 GTK_CSD=0 ./shared_node_chat --client HOST-AETHER-ID`
uses the window manager's title bar for dragging. Native Wayland also supports
the chat and observes changes to the window and client viewport.

## Android

From `examples/shared_node_chat/android`, build the x86_64 debug APK with
`./gradlew :app:assembleDebug` or the corresponding `gradlew.bat` command on
Windows. Add `-PchatAbis=armeabi-v7a,x86_64` to include 32-bit ARM phones in
the same APK. Select the device serial explicitly with
`adb -s SERIAL install -r app/build/outputs/apk/debug/app-debug.apk`.
The launcher opens a host room. Use the activity
extras `host=false` and `invite=HOST-AETHER-ID` to launch as a client, and use a
distinct `profile` for each local participant.

The `send_latency` instrumentation mode measures three native Send clicks
through the first pre-draw containing each committed message. Build and install
the Debug app and its `:app:assembleDebugAndroidTest` APK, then run

```sh
adb -s SERIAL shell am instrument -w -e mode send_latency -e profile timing com.apptraverse.sharedchat.test/com.apptraverse.sharedchat.GenericChatInstrumentation
```

The default creates a host. Supply `-e hostUid HOST-AETHER-ID`
to measure a client in an existing test room. The output reports local display
time in milliseconds. Measuring delivery also requires observing the receiving
participant.

## Optional Android push demo

See [the binary push service](../../services/push/README.md) for the minimal Windows to Aether service to Firebase to Android flow. It is opt-in.

## Web and Linux notifications

The WebAssembly chat can subscribe to standard browser Web Push. Set the push
service UID and VAPID public key in its URL, then click **Enable notifications**.
The service worker and server setup are described in [the push service
instructions](../../services/push/README.md#optional-browser-push).
With Emscripten and Ninja installed, build it from the repository root:

```sh
emcmake cmake -S . -B build/chat-web -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
  -DAPPTRAVERSE_BUILD_AETHER_LINK=ON -DAPPTRAVERSE_BUILD_SHARED_NODE_CHAT=ON
cmake --build build/chat-web --target shared_node_chat_web
```

Serve `build/chat-web/examples/shared_node_chat/web/` as one directory with
HTTPS and the COOP/COEP headers noted in the service instructions.

The GTK Linux chat uses the Freedesktop desktop notification service (the usual
interface on GNOME, KDE and other Linux desktops). It shows **New message** for
new remote messages while its window is inactive. It does not need Firebase,
VAPID or an extra daemon beyond the desktop's notification service. The Linux
chat process must be running; this does not wake a closed desktop app.
Build the `shared_node_chat` target with the same two CMake options on Linux;
GTK 3 development headers and a session D-Bus are required.
Set `APPTRAVERSE_PUSH_SERVICE_UID` before launch if this Linux chat should send
push signals to its peers through the Aether service.

## Explicit generic native acceptance

The Linux opt-in target `apptraverse_shared_chat_linux_native_test` exercises
actual GTK controls under a display server. Give it a new UI profile path and,
optionally, a separately owned SDK profile to reuse its identity. The Web helper
`tools/shared_chat_web_generic_test.py --url CHAT_URL --profile-dir NEW_BROWSER_PROFILE`
uses headless Chrome and two private cloud participants. These cloud checks are
explicit commands, not default CTest cases.

The Android app includes a dependency-free `GenericChatInstrumentation` runner.
Build `:app:assembleDebug` and `:app:assembleDebugAndroidTest`, install both APKs
on the selected emulator, then run its instrumentation with an owned `profile`
argument. The runner emits `HOST_UID` for a separate owned headless peer to join;
after the peer receives its Unicode marker it replies with exactly
`peer-reply Привет 🌍`. The orchestration sets font scale to 1.0 before launch,
handles `REQUEST_ROTATION` (landscape) and `REQUEST_FONT_SCALE` (1.15), and restores
the original system settings afterward. The test uses actual native widgets,
Activity replacement and a delayed obsolete JNI teardown callback; it requires
no product test hook. See the current non-Apple validation report for the exact
executed scopes and remaining environment limits.

The instrumentation also has focused `local_prepare` / `local_verify` modes for
same-profile native LOCAL save/cold-process restore, and `long_text` for native
IME commit and fresh-timestamp key injection. The current validation reports
required native/lifecycle and existing-consumer checks passing. Optional
unpaced adb40 truncation and physical Enter after the separate 40-key stress
case remain NOT PASS; IME40 model/send and exact native fresh-key40 text/focus
passed independently. These outcomes are separate from ordinary three- and
15-character native typing acceptance.
