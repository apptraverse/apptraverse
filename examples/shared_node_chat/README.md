# SharedNode chat

Minimal Windows demonstration: one host creates a room, participants join by
invitation, and each participant sends its own Events directly to every peer.
Fixed English labels; no localization or extra chat features.

Enable `APPTRAVERSE_BUILD_AETHER_LINK=ON` and
`APPTRAVERSE_BUILD_SHARED_NODE_CHAT=ON` in an existing Debug build. The SDK must
match the immutable pin in `cmake/shared_aether.cmake`; its exact upstream
MSVC compile patch is applied automatically. This is the published
`feature/percentile8-policy-v1` branch from
[Cursor PR #550](https://github.com/aethernetio/aether-client-cpp/pull/550).
Ordinary builds leave both options OFF. Build target `shared_node_chat`.

```powershell
./examples/shared_node_chat/run_demo.ps1
```

This opens Host, Alice and Bob with separate persistent profiles under
`.artifacts/shared-chat-demo`. Click **Host room** in one window, copy its
**Room invite** into the other windows and click **Join**. Enter sends a
message. Closing a window saves its profile; reopening the same profile
restores its UID, room and message history.

For individual processes:

```powershell
./build/shared-node-debug/examples/shared_node_chat/shared_node_chat.exe --profile .artifacts/chat-alice --name Alice
```

Each concurrently running process requires its own profile. Keep profiles
private: they contain SDK identity credentials and native model state.

Presence is exclusively `Client::IsLocallyOnline()` and
`Client::QueryPeerPresence()`. The link starts business writes only with fresh
SDK Online. Offline/Unknown retain pending SharedNode delivery for retry.

**Current network blocker:** the native-API SDK pin compiles but does not
complete registration against the tested cloud. The working streaming/push
SDK branch has no native Client presence API. The chat therefore displays a
registration error and cannot yet form a room on that deployment. No echo
fallback is used. See the exact [validation and limits](../../docs/validation/shared-node-chat-native-presence-2026-11-11.md).

Explicit cloud acceptance (not part of ordinary CTest):

```powershell
cmake --build build/shared-node-debug --target apptraverse_shared_chat_win32_test
./build/shared-node-debug/examples/shared_node_chat/apptraverse_shared_chat_win32_test.exe --exe "$PWD/build/shared-node-debug/examples/shared_node_chat/shared_node_chat.exe"
```

The acceptance controller owns three processes and fresh private profiles. It
requires actual admission, Unicode/all-peer delivery, API Offline, catch-up
after same-profile restart, a new message after restart and clean shutdown.
A bootstrap failure does not count as a chat/presence PASS.
