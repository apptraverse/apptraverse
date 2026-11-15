# Push service integration — 2026-11-15

Integrated the application-owned push service into current AppTraverse main,
including its `ConnectionRuntime` transport boundary. The Java service excludes
`webrtc-java`, starts with `aether.webrtc.disabled=true`, and `verifyNoWebRtc`
checks the resolved runtime. It uses the existing Aether client/server connection.
Hydrogen is packaged for Windows and built for macOS; the published artifact
already embeds Linux's shared library.

## Executed checks

- Java `test verifyNoWebRtc installDist --offline`: PASS. The installed service
  distribution contains no WebRTC jar. Packaged Windows service started and
  registered a fresh Aether identity.
- Native Debug chat and protocol targets: build PASS. CTest push protocol,
  Aether link, SharedSync and connection runtime: 4/4 PASS. Chat UI model
  executable: PASS.
- Firebase-enabled Android Debug APK: build PASS; installed on Google APIs
  Android 14 `emulator-5556`.
- Fresh headless Windows `ChatSession` host and Android app joined a room.
  Android registered its FCM token (`method=1 status=0`). The host sent
  `SNBENCH:1:AndroidPushSmoke20261115` while Android was in the background.
  The service accepted Notify (`method=2 status=0`); Android's notification
  manager displayed `SharedNode Chat / New message`. Reopening the chat showed
  the exact message in its message view, with `Online | Connected`.
- Android sent `AndroidReply20261115` using its Send button; the host's
  `ChatView` contained that reply.

The test used the owner's existing Firebase project and a locally stored service
account. Neither credential nor Android Firebase app configuration is committed.
All test identities, SQLite data and build logs remain under ignored `build/`.

The real push/Android check ran on Windows and an emulator. Linux and macOS
distribution builds and physical Android delivery were not run here. A successful
FCM send confirms provider acceptance; the Android notification and chat view
were checked separately.
