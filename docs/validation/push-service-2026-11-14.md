# Binary push service — 2026-11-14

Branch: `feature/aether-fcm-push`, based on AppTraverse main `a3716dfc`.
Integration target: `examples/shared_node_chat`, not AeroAdmin.
Aether C++ SDK remains pinned to `6fae4c39307888f7338f7629109aeb01c5948f2d`;
Java uses published `io.aether:cloud-client:0.0.86` on JDK 25.

## Executed

- C++20 golden wire fixture and split/coalesced packet parser: PASS.
- Java service compile, distribution and three JUnit tests: PASS. Covers common
  wire fixture, parser, token replacement, restart, sender authorization,
  unregistered target and provider failure.
- Windows x64 Debug `shared_node_chat`: builds successfully.
- SharedChat UI model CTest, Debug configuration: 1/1 PASS after integration.
- Android x86_64 ordinary Debug APK (Firebase disabled): builds successfully,
  including updated JNI signatures and native transport.
- Android Firebase-enabled Java/resources/manifest compilation: PASS using a
  temporary synthetic Firebase project configuration. That file was removed;
  it was never a working project or a real credential. No configured push APK
  is claimed until the owner's Firebase configuration is supplied.
- Explicit real-cloud transport probe: PASS. Two C++ ChatSessions and a Java
  service registered actual identities, joined a room, sent an ordinary message,
  registered a test token, requested Notify and received successful binary
  responses for both methods. Both executables printed LIVE_TRANSPORT_PASS.
  Evidence: local `build/push-live-cpp-03.log`, `build/push-live-java-03.log` and
  `build/push-live-03/delivered`. Identity files stay uncommitted.

## Findings fixed during validation

The published Java API has no `AFuture.join()` despite its README example;
startup uses the supported completion callback and a bounded latch instead.
The Hydrogen FFM artifact embeds Linux's shared object but no Windows DLL;
the service distribution builds the matching pinned implementation using CMake.
The Java SDK also initializes WebRTC for ordinary peer streams, so the missing
Windows native WebRTC artifact was added. The live probe passed after these
runtime dependencies were included. AndroidX is enabled for Firebase dependencies.

## Firebase end-to-end follow-up — PASS

Using the owner's actual Firebase project configuration and service account,
the packaged Java service started on Windows. The key is stored outside this
repository; Android configuration is ignored by Git.

Installed the Firebase-enabled Debug APK on the Google APIs Android 14 x86_64
emulator (`emulator-5556`). Android joined the Windows SharedNode chat and
registered its FCM token (`method=1 status=0`). Sent `Push test from Windows`
through the real Windows chat composer after putting Android in the background
with Home. The Java service submitted the push successfully (`method=2 status=0`).
Android NotificationManager recorded the FCM notification on `chat_messages`;
the notification shade visibly showed `SharedNode Chat / New message`.
Tapped the notification: the app reopened, restored its session, and displayed
the exact ordinary SharedNode message. The first post-tap observation was during
startup; the next showed the restored message. No startup or message data is
inferred solely from FCM acceptance.

Local evidence: `build/firebase-service-allowed.log`,
`build/firebase-android-build.log`, `build/firebase-notification.png`.
Private SDK identities, database tokens, service credentials and Firebase app
configuration remain uncommitted.

The real packaged launch exposed cmd.exe's command-length limit: Gradle's
expanded classpath exceeded the limit at this checkout path. Windows startScripts
now uses Java's `%APP_HOME%/lib/*` classpath wildcard. The actual packaged launcher
started successfully after the fix and ran the end-to-end scenario above.

## Not verified

Foreground notification display, physical Android, iOS, Web Push, marketplace
billing and multi-device routing. Provider acceptance alone is not a display/read
receipt; background display and tap were verified separately in this scenario.

## Deliberate minimal scope

Two frozen binary methods with request IDs and replies. Device registration is
keyed by the authenticated Aether sender; Notify uses an explicit sender allowlist.
Windows asks for push after a successful local chat commit; ordinary SharedNode
replication remains responsible for message delivery. Registration retries until
ACK; notification delivery is best effort with no durable queue or deduplication.


## Repeated push / heads-up follow-up — 2026-11-14

The user's manual test exposed missing repeated pushes. Confirmed Android's
channel importance HIGH, global heads-up enabled and DND off. SystemUI NotifLog
showed the heads-up presentation lasting about 5–6 seconds, but subsequent
Windows requests could disappear before reaching the Java service.

Fixed two adapter boundaries:

- A Java service response can arrive through a separate reverse P2p port. Keep
  that inbound stream separately instead of replacing the outgoing service
  stream and its pending work/subscriptions.
- Starting a new frame after more than five seconds idle reused the previous
  write-progress timestamp. The stall detector immediately recreated the stream
  and discarded the newly queued frame. Start its timeout when it is queued.

Extended the explicit live C++/Java probe to require three Notify requests and
three matching response IDs, with six seconds idle between requests. Combined
fix passes: `build/push-idle-cpp2.log` and `build/push-idle-java2.log` each report
LIVE_TRANSPORT_PASS. A queue-timestamp-only diagnostic variant failed to receive
the first Notify response, so the separate reverse stream is retained in the
final change. No persistent model schema changed; the response ID is transient
ChatView diagnostics.

Final Windows Debug and Android Firebase-enabled Debug builds pass; the Android
APK was updated in place, preserving user data. SharedChat UI model CTest passes
1/1. Real Firebase validation sent two notifications from the Windows GUI with
an idle interval. Both responses report status 0 and the second heads-up was
captured on the Android home screen. Local visual evidence:
`build/popup-visible.png`, `build/popup-visible-repeat.png`,
`build/popup-repeat-result.txt`, `build/banner-final-host-err.log`.
The banner is temporary; the launcher dot can remain after the banner disappears.

## Foreground, badge and input follow-up — 2026-11-14

The later [code review and validation](push-review-2026-11-14.md) supersedes the
foreground behavior and badge limitation above. Foreground signals no longer
post notifications; opening the chat clears all its notifications and the dot.
Windows input now rejects stale model publications. The review also fixes burst
queueing, protocol validation and service lifecycle/error handling. Final checks:
8 Java tests, 4 CTests, six real C++/Java Notify exchanges and real Firebase
foreground/background/launcher-icon acceptance PASS. See the linked report for
precise scope, remaining limitations and a separate pre-existing Apple finding.
