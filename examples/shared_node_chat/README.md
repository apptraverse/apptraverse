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

Presence is disabled: participants show names without Online/Offline badges.
The link sends as soon as transport capacity permits. SharedNode keeps a
message until its application ACK and retries it every second if the ACK is
missing, including while the recipient is stopped. SDK write completion alone
does not confirm delivery. Duplicate Events are applied only once.

The SDK's native presence support remains available through the explicit
`AetherLinkOptions::use_presence` opt-in. The default chat makes no presence
queries and does not wait for GetClientTiming replies.
Real-cloud and three-window chat acceptance passed; see
[validation and scope](../../docs/validation/shared-node-ack-retry-2026-11-12.md).

Explicit cloud acceptance (not part of ordinary CTest):

```powershell
cmake --build build/shared-node-debug --target apptraverse_shared_chat_win32_test
./build/shared-node-debug/examples/shared_node_chat/apptraverse_shared_chat_win32_test.exe --exe "$PWD/build/shared-node-debug/examples/shared_node_chat/shared_node_chat.exe"
```

The acceptance controller owns three processes and fresh private profiles. It
requires actual admission, Unicode/all-peer delivery, stopped-peer catch-up
after same-profile restart, a new message after restart and clean shutdown.
A bootstrap failure does not count as a chat PASS.
