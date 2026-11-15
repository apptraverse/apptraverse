# Push implementation review — 2026-11-14

Reviewed the changes from `a3716dfc` through `d619f32e`: binary C++/Java codecs,
raw Aether stream integration, ChatSession commands, Java/SQLite/FCM endpoint,
Android lifecycle/JNI/Gradle integration, packaging, credentials boundaries and
the accompanying tests. Also inspected the Windows input path reported by the
user and analogous Android/Linux/Web/Apple presentation paths.

## Findings fixed in this follow-up

| Finding | Cause and correction |
| --- | --- |
| Windows typed text/caret overwritten | An older model publication could replace newer native EDIT contents. Track submitted edit revisions per editable control and apply text only after its edit is acknowledged. Equal text is not rewritten. The same rule already exists on Android/Linux/Web. |
| Notification in an active Android chat | `onMessageReceived` explicitly posted a notification for foreground FCM delivery. Consume that signal silently; background notification messages continue to use the pinned SDK's automatic display. |
| Launcher dot remained after opening chat | Auto-cancel only removed the notification tapped, and opening through the icon removed none. `onResume` now cancels all notifications for this single-chat app, including FCM tags and the previous foreground notification ID. |
| Crash on declared Android 24/25 support | Channel creation requires API 26, but had no version guard. Channel setup is now guarded. Static lint passes; Android 7 runtime was not run. |
| Push requests lost during a burst | Direct sends hit the Aether link's one-frame queue. A bounded 64-request memory queue now waits for transport capacity; writes already accepted by the transport are not retried. |
| Redundant token registration on resume | Fetching an unchanged token discarded an existing or pending registration ACK. Same-token callbacks are now idempotent; changed tokens get a new correlated request. |
| C++/Java protocol validation differed | C++ accepted malformed UTF-8, and encoders accepted unknown methods/statuses. Both sides now enforce the frozen v1 contract, including strict Unicode scalar encoding. |
| Partial response could survive stream replacement | Reset the C++ incremental reader when its raw service stream is replaced, so bytes from different stream generations cannot form one response. Malformed responses are reported instead of silently ignored. |
| Provider validation failure reported as request rejection | `IllegalArgumentException` from the provider fell into request validation's status 1. Delivery exceptions now consistently produce status 2. Target UUID text must also have its canonical shape. |
| Unbounded server backlog / receive-thread shutdown exceptions | The endpoint had an unbounded executor queue and did not handle rejection during shutdown. Bound pending work at 256, reply with status 2 on overload, contain handler/reply exceptions, drain on close and serialize database close with operations. Add provider HTTP timeouts and independently attempt resource cleanup. Registration startup failure exits even if SDK threads remain alive. |

The two existing method IDs, successful v1 wire bytes, SDK identity, service UID,
application data and ordinary SharedNode replication format are preserved.
No persistent object schema or Firebase project settings changed.

## Executed verification

- **Java 8/8 tests PASS.** Golden bytes, every split point through a maximum
  request, coalesced frames, UTF-8 and length/header/method/status failures,
  recovery after malformed input, SQLite restart/token replacement, authenticated
  sender allowlist, provider exception classification, bounded backlog,
  exception containment and graceful dispatcher drain.
- **C++ CTest 4/4 PASS:** push protocol, SharedChat UI model, Aether link and P2P
  message fragments. The codec test additionally discards an incomplete frame
  before accepting a response on a replacement stream. This is a deterministic
  reader test, not a network fault-injection campaign.
- **Real Aether C++ ↔ Java PASS:** two C++ peers and the Java service exchanged
  registration and six successful Notify responses. Includes six-second idle
  intervals, replacement of the registration token and three immediately queued
  notifications. Ordinary chat admission/send also passed. Firebase is replaced
  by the probe recipient in this particular test.
- **Builds PASS:** Windows Debug chat, Java service distribution, Android Debug
  with Firebase enabled, Android Debug with push disabled, then the final
  Firebase-enabled APK. Android lint: **0 errors**, two existing example warnings
  (backup extraction rules and missing custom application icon).
- **Real Firebase foreground PASS:** Windows GUI sent
  `START-abcdefghijklmnopqrstuvwxyz 0123456789-END`; Android displayed the chat
  message, logged `Foreground push consumed without notification`, and had zero
  active app notifications. The service/Windows response was status 0.
- **Real Firebase background / launcher dot PASS:** sent two background
  messages from Windows. Launcher accessibility reported
  `SharedNode Chat has 2 notifications`, with a visible dot. Opening through that
  icon displayed both messages and removed all app notifications. Returning Home
  showed the icon without a dot or notification count. Android's automatic group
  summary was removed as well.
- **Windows GUI input PASS:** keyboard entry of an alphabet/numeric sequence,
  pause, Home plus prefix insertion, End plus suffix insertion, and Return to
  send. Text/order and the caret position were preserved; successful send cleared
  the input. The sent text was verified in the Android chat.

Local evidence (ignored build artifacts): `build/review-java-tests.log`,
`build/review-native-tests.log`, `build/review-live-{cpp,java}.log`,
`build/review-{native,android}-build.log`, `build/review-android-no-push.log`,
`build/review-android-final.log`, `build/review-service-package.log`,
`build/review-foreground-{log,notifications}.txt`, `build/review-android-foreground.xml`,
`build/review-home.xml`, `build/review-clean-home.xml`, `build/review-after-open.txt`,
`build/review-background-2.png`, `build/review-badge-cleared.png` and
`build/review-{service-out,host-err}.log`.

A test assertion initially treated a still-incomplete frame with a valid longer
length as malformed; corrected the invalid-length fixture to an actually invalid
length. The final Java and C++ tests above passed. Launcher text after clearing
was `Predicted app: SharedNode Chat`, so the badge check verifies absence of a
notification count instead of requiring an exact plain app label.

Android's dot represents active notifications, as documented in
[Android notification badges](https://developer.android.com/develop/ui/views/notifications/badges).
Foreground/background handling is for the pinned Firebase Messaging 24.1.2
notification payload, consistent with the
[Firebase notification-message behavior](https://firebase.googleblog.com/2018/09/handle-fcm-messages-on-android.html).

## Remaining scope and separate finding

- This is still the requested minimal Windows-to-Android demo: one Firebase
  project, one service and an explicit sender allowlist. No APNs/Web Push,
  per-tenant authorization or multi-device account model was added.
- Push remains best effort after transport enqueue. There is no durable outbox,
  retry/deduplication of Notify, provider-token invalidation policy or read receipt.
  A delayed push arriving after the user leaves the chat can still notify about
  a message previously seen. Suppression concerns notification display while the
  app is foreground; it does not eliminate the FCM network request.
- Physical Android, API 24/25 runtime, Apple runtime and long network-outage /
  service-restart fault campaigns were not run in this follow-up. Raw binary
  streams still require the documented TCP configuration.
- **Separate, pre-existing Apple finding:** `apple/ChatPresenter.swift::refresh`
  unconditionally copies model input/invite text into SwiftUI bindings without
  edit revision acknowledgement. It has the analogous stale-publication risk.
  This file predates the push changes; it was not modified or runtime-verified on
  this Windows host. Do not count the Windows fix as an Apple validation pass.

Firebase configuration, private credentials, device tokens and SDK identity
files remain outside tracked source. The service and both test applications were
left running with the user's existing profiles.
