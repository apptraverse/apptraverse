# SharedNode chat

Minimal Windows demonstration: one host creates a room, participants join by
invitation, and each participant sends its own Events directly to every peer.
Fixed English labels; no localization or extra chat features.

Enable `APPTRAVERSE_BUILD_AETHER_LINK=ON` and
`APPTRAVERSE_BUILD_SHARED_NODE_CHAT=ON` in an existing Debug build. The SDK must
match the public `5ddf5eaa` pin in `cmake/shared_aether.cmake`; frozen patches
add streaming fixes and native Client presence. A manifest verifies the
source/test postimages. The equivalent isolated integration `423ab1e1` is
also accepted through `APPTRAVERSE_AETHER_CLIENT_SOURCE`.
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

**Current network blocker:** registration and ordinary SDK A/B messages now
work, but remote GetClientTiming queries receive no timing samples. Unknown
blocks admission sends: Host can create a room and store local messages,
while other windows remain waiting to join. Local Unicode/history/profile
restart was checked; multi-participant chat is not yet validated. No echo
fallback is used. See [validation and limits](../../docs/validation/shared-node-presence-streaming-2026-11-11.md).

Explicit cloud acceptance (not part of ordinary CTest):

```powershell
cmake --build build/shared-node-debug --target apptraverse_shared_chat_win32_test
./build/shared-node-debug/examples/shared_node_chat/apptraverse_shared_chat_win32_test.exe --exe "$PWD/build/shared-node-debug/examples/shared_node_chat/shared_node_chat.exe"
```

The acceptance controller owns three processes and fresh private profiles. It
requires actual admission, Unicode/all-peer delivery, API Offline, catch-up
after same-profile restart, a new message after restart and clean shutdown.
A bootstrap failure does not count as a chat/presence PASS.
