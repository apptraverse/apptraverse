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

## Not verified

Actual FCM submission, notification display/tap on Android, and foreground vs
background delivery: require the owner's Firebase configuration and server key.
No physical Android, iOS, Web Push, marketplace billing or multi-device test.
No claim that provider acceptance means notification display/read.

## Deliberate minimal scope

Two frozen binary methods with request IDs and replies. Device registration is
keyed by the authenticated Aether sender; Notify uses an explicit sender allowlist.
Windows asks for push after a successful local chat commit; ordinary SharedNode
replication remains responsible for message delivery. Registration retries until
ACK; notification delivery is best effort with no durable queue or deduplication.
