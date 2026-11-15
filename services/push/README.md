# Minimal Aether push service

Open-source module under the AppTraverse repository's Apache-2.0 license.
The example is `examples/shared_node_chat`; AeroAdmin is not involved.
One Firebase project, one Android installation, one Windows sender. No user
accounts, marketplace catalog, object graph, discovery RPC or multi-device routing.

```
Android -- Register(token), authenticated as its Aether UID --> service
Windows -- ordinary SharedNode message --> Android
Windows -- Notify(Android Aether UID) --> service -- FCM --> Android
```

## 1. Firebase (manual)

Create a Firebase project and register Android package `com.apptraverse.sharedchat`.
Download its `google-services.json` into `examples/shared_node_chat/android/app/`.
In project settings / Service accounts, generate a private key for the service.
Keep that JSON outside source control and outside both client applications.
Enable the Firebase Cloud Messaging HTTP v1 API and grant the service account
permission to send messages in this project.

`google-services.json` is app configuration, not the server private key. FCM's
installation token is the delivery address, not either of these files.
The application wire protocol is binary. Firebase's own HTTPS wire format and
credential JSON files are handled by Google's SDK, not sent through Aether.

## 2. Build/start the service

The pinned Aether Java artifact requires **JDK 25**. Gradle wrapper 9.5 is included.
On Windows, CMake and Visual Studio 2022 C++ Build Tools are also needed:
the build automatically compiles the pinned Hydrogen DLL missing from the Java
SDK's Windows package and includes it in the service distribution. WebRTC's
Windows runtime is included because the SDK initializes it for peer streams.
From this directory, in PowerShell:

```powershell
$env:JAVA_HOME='C:/path/to/jdk-25'
./gradlew.bat test installDist
$env:GOOGLE_APPLICATION_CREDENTIALS='C:/private/firebase-service-account.json'
# First start may omit the allowlist to obtain the stable service UID.
./build/install/aether-push-service/bin/aether-push-service.bat
```

Record the printed `PUSH_SERVICE_UID`. Preserve the `state/` directory: it holds
SDK identity secrets and SQLite device registrations. Stop with Ctrl+C.
`PUSH_STATE_DIR` overrides this path. Optional `AETHER_PARENT_UID` and
`AETHER_REGISTRATION_URI` select your own Aether registration settings.
The default parent is the public example parent; use your own for deployment.

## 3. Windows chat

Build the existing Debug SharedNode chat as described in its README. Before launch:

```powershell
$env:APPTRAVERSE_PUSH_SERVICE_UID='<PUSH_SERVICE_UID>'
./build/push-debug/examples/shared_node_chat/shared_node_chat.exe --host --profile ./build/push-host --name Windows
```

Copy **My Aether ID** from the chat. Restart the service with this sender allowed:

```powershell
$env:PUSH_ALLOWED_SENDERS='<WINDOWS_AETHER_UID>'
./build/install/aether-push-service/bin/aether-push-service.bat
```

The allowlist contains authenticated Aether UIDs, comma-separated. An empty
allowlist allows registration but rejects every Notify. No common application
secret is embedded in clients. A client can register only under its own transport
identity. For this local demonstration an allowed sender can notify any device
registered with this one service; tenant membership is intentionally deferred.

## 4. Android chat

From `examples/shared_node_chat/android`, using the Android-compatible JDK 20:

```powershell
$env:JAVA_HOME='C:/path/to/jdk-20'
./gradlew.bat :app:assembleDebug -PchatPush=true -PpushServiceUid=<PUSH_SERVICE_UID>
```

Install `app/build/outputs/apk/debug/app-debug.apk`. Use a Google APIs/Google Play
emulator (the existing build targets x86_64) or add the appropriate device ABI.
Grant notification permission. Launch as a client with the Windows host UID:

```powershell
adb shell am start -n com.apptraverse.sharedchat/.MainActivity --ez host false --es invite <WINDOWS_AETHER_UID> --es profile default --es name Android
```

Wait until the Android chat has joined and the service logs `method=1 status=0`.
Then put Android in the background with Home (do not Force stop) and send a
message from Windows. The service should log `method=2 status=0` and Android
should display **SharedNode Chat / New message**. Tap to reopen the chat.
While the chat is in the foreground, FCM still delivers the signal but the app
does not post a notification. SharedNode updates the visible messages normally.
Opening this single-chat app clears all its notifications and the launcher dot,
whether opened through a notification, the launcher icon or the recent-apps list.
This uses the pinned FCM SDK's notification-message routing; data-only messages
and the newer AndroidConfigV2 API are not part of this example's contract.
With no `-PchatPush=true`, Firebase is not linked and no Firebase config is needed.

## Binary contract v1

All integers are unsigned, big-endian. UTF-8 strings have a u16 byte length.
No host struct layouts, native UUID encodings or Java serialization.

| Bytes | Meaning |
| --- | --- |
| 0..3 | ASCII `ATPN` |
| 4..5 | Total frame length, including header, at most 4096 |
| 6 | Version, currently 1 |
| 7 | Method: 1 Register, 2 Notify, 0x81/0x82 corresponding response |
| 8..15 | Request ID, u64; echoed in response |
| 16.. | Request: u16 length + string (max 2048 bytes); response: one status byte |

Register's string is the FCM token; its owner is the authenticated sender UID.
Notify's string is the target UID in canonical UUID text. Responses: 0 means
registration stored or FCM accepted the message, 1 rejected/target missing,
2 provider/storage failure or temporary service overload. Success does **not**
confirm display or reading. Both codecs reject malformed UTF-8 and unknown v1
methods/status values; truncated stream frames wait for their remaining bytes.

Existing methods/fields are frozen. New methods get new IDs. Incompatible
changes require a new wire version and explicit support; no negotiation is
claimed. This small contract is manually encoded in C++ and Java, not generated
by ADSL. Both codecs check the same golden binary fixture. The service endpoint
bypasses the SharedNode fragment envelope over the existing TCP P2pStream;
ordinary chat replication stays unchanged. Do not enable UDP for this raw stream.

## Validation and current limits

`./gradlew.bat test` checks shared binary bytes, every stream split point,
UTF-8/length/method/status validation, registration replacement, SQLite restart,
sender authorization, provider failures, queue overload and shutdown/exception
handling. `apptraverse_push_protocol_test` is also registered with ordinary CTest.
Standalone C++ fixture test (from repository root, with a C++20 compiler):

```powershell
g++ -std=c++20 -I include services/push/tests/protocol_cpp_test.cpp -o build/protocol_cpp_test.exe
./build/protocol_cpp_test.exe
```

Registration is retried every five seconds until its response. Fetching an
unchanged token on resume preserves the existing registration/ACK. Notify is
best effort after a successful local chat commit: a bounded memory queue (64
requests) bridges the single-frame transport, but there is no durable push queue,
automatic retry after an accepted write or deduplication. A full queue logs a
refusal. It does not gate chat delivery or track message read state. A delayed
FCM signal can still arrive after an earlier chat read if the app is back in the
background. The service has a bounded 256-request work queue; overload replies
with status 2. Provider HTTP connect/read/write timeouts are 10/20/10 seconds.
Provider errors do not fail the chat. Service should run while sending;
sleep/offline pauses it.
Real FCM delivery requires your actual project configuration and private key.

### Explicit cloud transport acceptance (no Firebase credentials)

Build `apptraverse_push_live_probe` in the existing native Debug build. In two
terminals, choose the same fresh absolute directory for both commands:

```powershell
# Terminal 1, services/push
./gradlew.bat liveTransportProbe -PprobeDir=C:/private/push-probe-01
# Terminal 2, repository root
./build/push-debug/apptraverse_push_live_probe.exe C:/private/push-probe-01
```

This creates three real Aether identities and validates C++ Register/Notify,
Java decoding, six matching Notify responses (including six-second idle gaps
and a three-request burst), token replacement and ordinary chat admission/send.
The Java test recipient replaces Firebase; this is not an FCM delivery test.
Use a fresh directory on each run; it contains private SDK identity files.
Current results are in [validation](../../docs/validation/push-service-2026-11-14.md).
The follow-up [code review](../../docs/validation/push-review-2026-11-14.md)
records the foreground, badge and Windows input fixes and their executed checks.

## Linux and macOS service hosts

Run the service with JDK 25 and the same `GOOGLE_APPLICATION_CREDENTIALS`,
`PUSH_STATE_DIR`, `PUSH_ALLOWED_SENDERS`, and Aether settings as on Windows.
From `services/push`, run `./gradlew test installDist` and then
`./build/install/aether-push-service/bin/aether-push-service`. Linux uses the
Hydrogen library packaged in the pinned Java dependency. On macOS the Gradle
build selects the matching WebRTC native artifact and builds pinned libhydrogen
with CMake for the local architecture. The resulting distribution carries it
under `lib/native`. Firebase service credentials remain only on the service
host. APNs delivery is a separate provider integration.
