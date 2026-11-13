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
message. Chat objects, message history and pending ACKs live only in RAM.
Closing the window ends that chat; reopening starts with an empty room/history.
Only the SDK identity is retained in the profile.

For individual processes:

```powershell
./build/shared-node-debug/examples/shared_node_chat/shared_node_chat.exe --profile .artifacts/chat-alice --name Alice
```

Each concurrently running process requires its own profile. Keep profiles
private: they contain SDK identity credentials.

The model publishes only on materialized Node changes or status transitions.
The Windows consumer receives `PostMessage` notifications and reads `ChatView`
then; there is no periodic publication or UI refresh timer. ACK bookkeeping,
retry and idle service do not cause display notifications. Saving the owning
application's objects is its responsibility, outside this demonstration.

Presence is disabled: participants show names without Online/Offline badges.
The link sends as soon as transport capacity permits. SharedNode keeps a
message until its application ACK and retries it every second if the ACK is
missing, including while the recipient is stopped. SDK write completion alone
does not confirm delivery. Duplicate Events are applied only once.

The SDK's native presence support remains available through the explicit
`AetherLinkOptions::use_presence` opt-in. The default chat makes no presence
queries and does not wait for GetClientTiming replies.
Previous persistence-based acceptance is recorded in
[historical validation](../../docs/validation/shared-node-ack-retry-2026-11-12.md).

Explicit cloud acceptance (not part of ordinary CTest):

```powershell
cmake --build build/shared-node-debug --target apptraverse_shared_chat_win32_test
./build/shared-node-debug/examples/shared_node_chat/apptraverse_shared_chat_win32_test.exe --exe "$PWD/build/shared-node-debug/examples/shared_node_chat/shared_node_chat.exe"
```

The acceptance controller owns three processes and fresh private profiles. It
requires actual admission, Unicode/all-peer delivery, continued use while a
peer is stopped, a fresh RAM chat after restart, no chat disk checkpoint and
clean shutdown. SDK UID restoration is tested separately from chat state.
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
The benchmark also requires zero publication notifications during three
seconds of idle time and verifies that no chat checkpoint files were created.
The [previous measurements](../../docs/validation/shared-node-chat-latency-2026-11-12.md)
used the superseded polling/disk configuration and include an intermittent stall.
Current [event-driven RAM checks and timing](../../docs/validation/shared-node-chat-event-driven-2026-11-12.md)
are recorded separately.
The subsequent [backpressure latency fix and controlled measurements](../../docs/validation/shared-node-chat-latency-backpressure-2026-11-12.md)
remove the extra ACK-timeout wait after a locally refused send.
