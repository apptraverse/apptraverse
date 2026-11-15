# Apple push integration — 2026-11-15

Based on AppTraverse main `7fed1d64`. Native APNs registration is shared by
macOS/iOS SwiftUI hosts. The platform owns notification permission, device token,
foreground suppression, notification opening and clearing. Registration enters
ChatSession's model queue; no SDK handles or tokens enter the persistent UI graph.
Existing model/session Send still queues Notify after successful commit.

Protocol v1 adds method 3 / response 0x83. Its string is
`sandbox|bundle.identifier|lowercase-hex-device-token` (or `production`).
Methods 1/2 and their bytes/semantics remain unchanged. Deploy the updated
service before Apple clients; older servers do not support method 3. Service validates APNs
addresses, stores them under the authenticated Aether UID in a separate table,
and replaces that identity's earlier provider registration. Existing SQLite
FCM registrations remain readable. Notify uses the existing sender allowlist.

The APNs provider uses HTTP/2, TLS, ES256 JWT with raw P1363 signature, a cached
50-minute provider token, alert type/priority, and bounded request timeouts.
A non-200 reply is status 2, never delivery success. Provider acceptance does not
prove display. No credentials are committed. Android FCM routing is preserved.

## Build and deployment

Configure either Apple chat build with
`-DAPPTRAVERSE_APPLE_PUSH_ENVIRONMENT=sandbox` (or `production`). Default empty
keeps push disabled. The build writes ATPushEnvironment to the bundle and emits
`shared_node_chat.entitlements` beside the target. Sign the app with the correct
Apple identity and provisioning profile for its bundle identifier; the generated
entitlement is `com.apple.developer.aps-environment` on macOS and `aps-environment`
on iOS, with development/production as appropriate. CMake does not invent a team
or sign with an unrelated identity. Install/launch the signed app with
`APPTRAVERSE_PUSH_SERVICE_UID` set to the service identity, and allow notifications.
For simctl launch, pass it as `SIMCTL_CHILD_APPTRAVERSE_PUSH_SERVICE_UID`.

Run the service with `APNS_KEY_FILE` pointing to the Apple private .p8 file,
`APNS_TEAM_ID` and `APNS_KEY_ID`. Existing `PUSH_ALLOWED_SENDERS` still applies.
Set `PUSH_FCM_ENABLED=false` for an Apple-only server without Firebase ADC;
the default continues to initialize Firebase for mixed Android/Apple deployment.
Keep all private keys on the service host. Use the environment matching the
app's signing entitlement. A push opens the existing chat; ordinary SharedNode
synchronization delivers its content, rather than embedding messages in push.

## Executed checks

- Java provider/protocol/storage tests: 9/9 PASS on JDK 23, using the existing
  PushTest plus new ApplePushTest with isolated SQLite databases/test EC key.
  These classes do not require the Java Aether JDK-25 runtime.
- Swift type checking: macOS native push coordinator PASS; complete iOS Swift
  presenter/host/coordinator PASS against iPhoneSimulator17.2. Existing SwiftUI
  onChange deprecation warnings remain.
- Full JDK 25 Gradle `test verifyNoWebRtc installDist`: PASS, 11/11 tests
  (6 PushTest, 3 ApplePushTest, 2 EndpointTest), including HTTP destination/topic/
  alert headers. Native macOS Hydrogen dylib and service distribution built.
- Standalone C++ protocol/stream fixture: PASS, including method 3 / 0x83.
- Native macOS Debug chat and explicit cloud probe: build PASS, using current
  main's pinned SDK `0c2b8936`; no SDK sources modified. Bundle contains sandbox
  configuration and generated macOS entitlement passes plutil validation.
- Native CTest: 3/3 PASS (4.15s): push protocol, connection runtime and chat UI model.
- Real Aether Java/C++ cloud probe: PASS on both ends. Initial FCM registration,
  token replacement, APNs method 3/0x83 registration ACK, ordinary room/message,
  six Notify responses, six-second idle gaps and burst delivery all completed.
  The Java probe replaces the external provider; this is not an APNs delivery test.
- Bundle configuration script: enable/change/repeat/disable PASS.
- iOS binary was not rebuilt or installed; Swift type-checking is its only new
  executed platform check in this slice. Native notification display and full
  Mac/iPhone UI/lifecycle regression were not run.

## Limits

`security find-identity -v -p codesigning` reports zero valid identities here.
No APNs provider key/team configuration was supplied in this task. Real APNs token
registration, provider acceptance and notification display are therefore NOT
VERIFIED. Simulator injection alone would not prove service/APNs delivery.
Windows is not built. No previous cloud/GUI acceptance is implicitly extended to
this revision. Full service distribution requires JDK 25, per existing README.

Local evidence: task workspace `work/apple-push-build.log`,
`work/apple-push-ios-typecheck.log`, `work/apple-push-java-tests/results.log`,
`work/apple-push-gradle-dist.log`, `work/apple-push-ctest.log`,
`work/apple-push-live-{java,cpp}.log`. Probe profiles remain private in
`work/apple-push-live-1/`.

Apple references:
- https://developer.apple.com/documentation/usernotifications/sending-notification-requests-to-apns
- https://developer.apple.com/documentation/UserNotifications/establishing-a-token-based-connection-to-apns
