# SharedNode chat

Minimal Windows demonstration: one host creates a room, participants join by
invitation, and each participant sends its own Events directly to every peer.
Fixed English labels; no localization or extra chat features.

Enable `APPTRAVERSE_BUILD_AETHER_LINK=ON` and
`APPTRAVERSE_BUILD_SHARED_NODE_CHAT=ON` in an existing Debug build. The SDK must
match the exact public revision in `cmake/shared_aether.cmake`.
`APPTRAVERSE_AETHER_CLIENT_SOURCE` accepts a clean checkout at that revision.
The default native configuration uses direct `P2pStream` over TCP, with no
SafeStream or adaptive stream. Native archive fragments carry whole SharedSync
messages; SharedSync owns durable ACK/retry and duplicate suppression.
Ordinary builds leave both options OFF. Build target `shared_node_chat`.

The current persistence acceptance uses embedded supply prepared by the Debug
`apptraverse_chat_supply_exporter OUTPUT.h`. Build/run that exporter without
`APPTRAVERSE_CHAT_SUPPLY_HEADER`, then configure the chat with that option
pointing to its output header. The validation below uses this load-from-supply
mode in Debug, including the same USER file across restarts.

```powershell
./examples/shared_node_chat/run_demo.ps1
```

This opens Host, Alice and Bob with separate persistent profiles under
`.artifacts/shared-chat-demo`. Click **Host room** in one window, copy its
**Room invite** into the other windows and click **Join**. Enter sends a
message. Room, history and pending delivery obligations are persisted in the
native USER graph (`user.bin`). Reopening the same profile restores the SDK
identity and existing room. Messages committed during an outage synchronize
when the peer reconnects, including after a forced process restart.

For individual processes:

```powershell
./build/shared-node-debug/examples/shared_node_chat/shared_node_chat.exe --profile .artifacts/chat-alice --name Alice
```

Each concurrently running process requires its own profile. Keep profiles
private: they contain SDK identity credentials.

The model publishes changed values after materialized Node changes or status
transitions, with runtime transport/journal diagnostics for acceptance tests.
The Windows consumer receives `PostMessage` notifications and reads `ChatView`
then; the Windows UI has no refresh timer. Native presence is sampled on the
model thread. Accepted model changes and their delivery obligations are saved
together before SharedSync acknowledges them. Runtime handles and fragment
assemblies never enter USER.

The chat observes native presence and displays local/remote status.
The link sends as soon as transport capacity permits. SharedNode keeps a
message until its application ACK and retries it every second if the ACK is
missing, including while the recipient is stopped. SDK write completion alone
does not confirm delivery. Duplicate Events are applied only once.

The library adapter keeps presence disabled by default. The chat explicitly
enables observation with `gate_delivery_on_presence=false`, so Unknown/Offline
status does not prevent durable delivery attempts. Historical acceptance is in
[historical validation](../../docs/validation/shared-node-ack-retry-2026-11-12.md).

Explicit cloud acceptance (not part of ordinary CTest):

```powershell
cmake --build build/shared-node-debug --target apptraverse_shared_chat_win32_test
./build/shared-node-debug/examples/shared_node_chat/apptraverse_shared_chat_win32_test.exe --exe "$PWD/build/shared-node-debug/examples/shared_node_chat/shared_node_chat.exe"
```

The acceptance controller owns three processes and fresh private profiles. It
requires actual admission, Unicode/all-peer delivery, continued use while a
peer is stopped, the same SDK identity and USER history after restart, delivery
of offline messages and clean shutdown.
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
The timing controller also checks idle notifications and rejects legacy
`native`/`root.id` sidecars. Its historical timing numbers do not establish
performance of the current P2pStream implementation.
The [previous measurements](../../docs/validation/shared-node-chat-latency-2026-11-12.md)
used the superseded polling/disk configuration and include an intermittent stall.
Current [event-driven RAM checks and timing](../../docs/validation/shared-node-chat-event-driven-2026-11-12.md)
are recorded separately.
The subsequent [backpressure latency fix and controlled measurements](../../docs/validation/shared-node-chat-latency-backpressure-2026-11-12.md)
remove the extra ACK-timeout wait after a locally refused send.
