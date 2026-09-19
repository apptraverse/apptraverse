# Endpoint availability (2026-09-19)

Status: implemented / verified. Not accepted-by-user.

Branch: `cursor/shared-node-join-3c1e`. Continues from `c1c8bfc13186f7ae41641a176d4cbb3fe11fa345`. That commit was not reverted.

## What changed

Outgoing availability is one observation on `IByteTransport`: Online, Offline, or Unknown. `MemoryNetwork::SetAvailability` changes it per direction and notifies the source only when the value changes. `Link` does not store it. A new transport does not reload a previous Online.

`SharedSyncRuntime` checks that observation before every `Send`: offer, request, accept, reject, repeat, snapshot, event, and ACK. Known Offline does not call `Send`. The persisted packet stays pending and is not marked Complete. Offline to Online makes that same packet due on the next `Service`. The availability callback only wakes; it does not send. A repeated Online does not skip the retry interval.

A response formed while Offline is the existing persisted packet. There is no second durable outbox. An ACK that could not be handed off is remembered only until this runtime sends it; after restart the peer's retry reproduces it.

## Reproduction

Baseline before this slice, Debug, HEAD `c1c8bfc`, four existing targets, all passed (join test 1.05s).

```
cmake --build build --target \
  apptraverse_shared_node_foundation_test \
  apptraverse_shared_node_initial_sync_test \
  apptraverse_shared_node_incremental_event_test \
  apptraverse_shared_node_join_test \
  apptraverse_shared_node_availability_test -j
ctest --test-dir build -R 'apptraverse_shared_node_' --output-on-failure

cmake --build build-release --target \
  apptraverse_shared_node_foundation_test \
  apptraverse_shared_node_initial_sync_test \
  apptraverse_shared_node_incremental_event_test \
  apptraverse_shared_node_join_test \
  apptraverse_shared_node_availability_test -j
ctest --test-dir build-release -R 'apptraverse_shared_node_' --output-on-failure
```

Release flags remain `-O3 -DNDEBUG -std=c++20 -fno-rtti`. Checks use `CHECK` / `std::exit`, so they stay active under `NDEBUG`.

## Results

Debug and Release, 2026-09-19, all five `apptraverse_shared_node_*` CTest targets passed. Send counts are taken from a test `IByteTransport` wrapper before `MemoryNetwork` can drop a packet.

Debug: foundation 0.01s, initial sync 0.06s, incremental event 0.11s, join 1.08s, availability 0.10s.

Release (`-O3 -DNDEBUG -std=c++20 -fno-rtti`): foundation 0.00s, initial sync 0.01s, incremental event 0.01s, join 0.09s, availability 0.01s.

Baseline before this slice, same Debug tree, HEAD `c1c8bfc`: the four existing targets passed (join 1.05s). Those tests were not weakened.

The availability target covers: initial Offline for `OfferNode` and `RequestJoin` with zero `Send`, then Online completion without a second user call; outage after the request, while awaiting a decision, after `AcceptJoin`, after the snapshot is dropped, after the snapshot is saved and before ACK, and with an unacked event; lost ACK while the link is still Online and the network drops packets; Unknown does not block; A?B can deliver while B?A cannot ACK until the reverse direction is Online; B offline does not stall C, and restoring B resumes both waiting nodes without mixing packet ids or share ids; restart from storage with a pending snapshot and a pending event sends nothing while Offline and then the same bytes; after a finished exchange, 500 retry intervals and a repeated Online notification add no `Send` and no journal record.

## Limits

Known Offline produces no outgoing `Send`. Restore continues the saved exchange. A lost ACK while Online retries the same packet and does not duplicate the event. Heartbeat, last-seen, and the real Æther client are not done. The next open test is full convergence of one node on A, B, and C, including events from each side and the same share list on all three. `TestRequestTwoNodesAndThirdParticipant` does not close that. Not accepted-by-user.

---

# Join request and deferred admission (2026-09-19)

Status: implemented / verified. Not accepted-by-user.

Branch: `cursor/shared-node-join-3c1e`. Continues from `e34e02c6f6c0bcb34a4b11869567e8974659f1f1`. That commit was not reverted.

## What changed

`OfferNode` still means the holder grants a node it already has. `RequestJoin(remote_endpoint, node_id, requested_access)` is the other direction: the caller does not have the node and does not create one. The holder accepts with `AcceptJoin` or refuses with `RejectJoin`. A `SetShareOfferPolicy` answer uses those same commands. No policy does not grant access.

An inbound attempt is stored as `AwaitingDecision` and can sit across `Service` calls. A repeat is the same attempt only when transport source, node, kind, class, and requested access match. A `Rejected` attempt does not block a later operation. `Service` does not keep sending a finished rejection; the stored decision is returned only when that same attempt arrives again.

`OnNodeState` calls `SetInitialNodeImportedCallback` before ACK on an admission that is not yet `Bound`. A repeated snapshot after `Bound` does not bind again.

Every `ShareOffer` is attached to `ShareAdmission` at `kShareAdmissionRootId`. A new runtime loads that root from its own storage. Tests no longer copy `LocalOfferIds()` across `Restart`.

## Reproduction

Same trees as the previous section. Do not wipe them.

```
cmake --build build --target \
  apptraverse_shared_node_foundation_test \
  apptraverse_shared_node_initial_sync_test \
  apptraverse_shared_node_incremental_event_test \
  apptraverse_shared_node_join_test -j
ctest --test-dir build -R 'apptraverse_shared_node_' --output-on-failure

cmake --build build-release --target \
  apptraverse_shared_node_foundation_test \
  apptraverse_shared_node_initial_sync_test \
  apptraverse_shared_node_incremental_event_test \
  apptraverse_shared_node_join_test -j
ctest --test-dir build-release -R 'apptraverse_shared_node_' --output-on-failure
```

Release flags remain `-O3 -DNDEBUG -std=c++20 -fno-rtti`. Join checks use `CHECK` / `std::exit`.

## Results

Debug and Release, 2026-09-19, all four CTest targets passed. The join test covers the previous grant scenarios plus: deferred reject then a new request, two requested nodes, a third participant on an already shared node, loss of the request and of the decision, restart from storage while waiting and after the snapshot is saved, and a tampered repeat. After a finished rejection and after a finished join, advancing logical time did not enqueue more admission traffic.

## Limits

Still standalone shared events on the chosen node, not arbitrary dynamic object graphs, multi-hop, presence, or the real Æther client. `MemoryTransport` is not authentication. `AcceptJoin` for a request needs a `Link` or `SetLinkForEndpoint`; the runtime does not construct a transport-specific Link.

---

# Share admission of a chosen SharedNode (2026-09-19)

Status: implemented / verified. Not accepted-by-user.

Branch: `cursor/shared-node-join-3c1e` from `origin/main` `d05d628547f6713b6f08eee93f452a577fa9bcef`.
`feature/messenger-v1` was not used.

## What landed

Public admission is `SharedSyncRuntime`, not a new manager. The application
calls `OfferNode` (node, remote `Link`, access) and `Service(now_us)`.
`SetShareOfferPolicy` accepts or rejects before any replica exists on the
receiver. `RegisterOffer` reattaches a persisted `ShareOffer` after the
runtime, transport, and Domain are created again from that replica's storage.

`ShareOffer` is local event-sourced state. It is not in the shared graph.
Operation id, node id, and share id stay distinct. `ExpectInitialNodeFromEndpoint`
is keyed by `(source_endpoint, node_id)`: a second node for the same endpoint
does not replace the first, and completing or forgetting one leaves the others.
An empty node id is still the single wildcard slot used by chat.

Frames `ShareOffer` and `ShareDecision` travel as opaque bytes on
`IByteTransport`. The receiver takes the source from the transport callback.
`MemoryNetwork` delivers those bytes between independent Domains. It is not
authentication and it is not the Æther client.

## Reproduction

Headless only. `APPTRAVERSE_BUILD_AETHER_DEMOS=OFF`. Compilers: `/usr/bin/gcc`
and `/usr/bin/g++`. Do not wipe an existing build tree.

```
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_COMPILER=/usr/bin/gcc \
  -DCMAKE_CXX_COMPILER=/usr/bin/g++ \
  -DAPPTRAVERSE_BUILD_AETHER_DEMOS=OFF
cmake --build build --target \
  apptraverse_shared_node_foundation_test \
  apptraverse_shared_node_initial_sync_test \
  apptraverse_shared_node_incremental_event_test \
  apptraverse_shared_node_join_test -j
ctest --test-dir build -R 'apptraverse_shared_node_' --output-on-failure
```

Release is a second tree so the Debug tree stays incremental:

```
cmake -S . -B build-release -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER=/usr/bin/gcc \
  -DCMAKE_CXX_COMPILER=/usr/bin/g++ \
  -DAPPTRAVERSE_BUILD_AETHER_DEMOS=OFF
cmake --build build-release --target \
  apptraverse_shared_node_foundation_test \
  apptraverse_shared_node_initial_sync_test \
  apptraverse_shared_node_incremental_event_test \
  apptraverse_shared_node_join_test -j
ctest --test-dir build-release -R 'apptraverse_shared_node_' --output-on-failure
```

Release compile line includes `-O3 -DNDEBUG -std=c++20 -fno-rtti`.
`apptraverse_shared_node_join_test` checks with `CHECK` / `std::exit`, not
`assert`, so they stay active under `NDEBUG`.

## Results

Debug and Release, 2026-09-19, all four CTest targets passed:

- `apptraverse_shared_node_foundation_test`
- `apptraverse_shared_node_initial_sync_test`
- `apptraverse_shared_node_incremental_event_test`
- `apptraverse_shared_node_join_test`

The join test offers an existing node, lets the receiver policy admit it, and
then only advances logical time and `MemoryNetwork` delivery. It does not
pre-build the receiver replica, call the other side's handler, or set
`Complete` by hand. Covered: both sides (20 events each) compared by shared
identity and payload; an event during initial sync; two nodes to one endpoint;
counter-offers; three replicas without mixing; loss, duplicate, reorder, and a
one-way partition; restart during the offer, after the snapshot is saved and
before ACK, and with an unacked event (network queue cleared); policy reject,
wrong node, unsupported class, wrong source, damaged frame, read-only write;
local fields absent from the transferred bytes.

## Limits

Verified for standalone shared Events that append independent records on the
chosen node. That is not support for arbitrary dynamic object graphs, multi-hop,
presence, or the real Æther client. `MemoryTransport` does not authenticate the
peer. The accept/reject policy is runtime-only and must be set again after
restart; the persisted decision is not asked again. The initiator must already
share the node with its own endpoint before `OfferNode`.

---

## Host	oClient delivery (2026-09-17) — NOT FIXED

Starting SHA: `d9b11a48771d077f0208543fbd1663ca71224d1e`

### Reproduced first broken edge
- Baseline: Client Join → Host Accept+NodeState → Client binds (Joined, same room) works after wrapping P2p with `P2pSafeStream`.
- First missing post-Join stage: Host journal Event `APP_TX` never gets `WRITE_OK` (SafeStream status callback absent) while Client has no `APP_RX` for that Event. Fake/bootstrap tests are not this failure.
- Live pair logs: `chat_live_nohb_*`, `chat_live_halfduplex_*`, `chat_live_hostreset_*`, `chat_live_newport_*`.

### Owning layer
`ChatAetherRuntime` / `ae::P2pSafeStream` post-Join send path — not admission, binding, or GUI. NodeState (840B) and Join ACK (14–24B) complete; next Event (~224B) stalls.

### Landed (partial)
- Join delivery trace (`APPTRAVERSE_JOIN_TRACE`) + live pair continuous readers
- `P2pSafeStream` for MTU fragmentation; serialize one Write; disable app heartbeats on that window
- NEW_PORT always binds (do not drop inbound when Linked outbound exists)
- Half-duplex: Client defers large APP TX once after Join ACK until Host Event RX
- Accepted 30s room-sync timeout + Win32 button `Syncing…` (was infinite Accepted / both labeled Joining…)
- Experimental post-Join CreatePort / hang recovery — does not reliably clear Event WRITE hang within live timeout

### Still open
Live cold join+bidirectional text over real Aether: **NOT FIXED**. First missing stage remains Host Event `WRITE_OK` / Client Event `APP_RX` after Join.

---
Status: implemented/verified on main. Not accepted.

# CURSOR — CHAT DEMO 05: FIRST WINDOWS CHAT + REUSABLE CHAT SESSION

## Identity

- Starting AppTraverse SHA: `b1b3a608f41dfdc9bcdff786edc1289fa9dfb306`
- Pinned aether SHA: `0b0e3b54b9ffa730c41597c8b18f6a75255bded3` (unchanged)
- Direct main only: tests → commits → push origin/main. NO PR. NO feature branch. NO force push.

## What Landed

1. **Endpoint-Authorized Room Bootstrap (`SharedSyncRuntime`)**:
   - Added `ExpectInitialNodeFromEndpoint(source_endpoint, expected_root_class_id)` and `SetInitialNodeImportedCallback(callback)` in `include/apptraverse/shared_sync_runtime.h` and `src/shared_sync_runtime.cpp`.
   - Admitted unknown incoming `SharedNode` snapshots when authorized by source endpoint without requiring advance knowledge of the room `ObjId`.
   - Validates that the received root object's most-derived class ID matches `expected_root_class_id` (`ChatRoom::kClassId`) in disposable scratch before writing to receiver storage or domain.
   - Enforces receipt and source-share uniqueness, registers node, and executes `initial_node_imported_callback_` on the model thread to bind `ChatEntry` before returning and acknowledging the snapshot.
   - Rejects unauthorized source endpoints, incompatible root class types, and duplicate/conflicting initial snapshots for already-existing nodes without touching local storage.

2. **Aether Link Descriptor (`AetherLink`)**:
   - Implemented `AetherLink` subclass of `apptraverse::Link` in `examples/chat_demo/aether/aether_link.{h,cpp}`.
   - Native Load/Save for `base_` and persistent `endpoint_uid` (version 0).
   - `EndpointUid()` returns `endpoint_uid` by reference; zero runtime pointers serialized.
   - Registration integrated into `EnsureAetherLinkRegistration()`.

3. **Reusable Chat Session (`ChatSession`)**:
   - Implemented common `ChatSession` in `examples/chat_demo/runtime/chat_session.{h,cpp}` for subsequent Linux and Android hosts.
   - Thread boundaries: GUI interacts via thread-safe public value-copying API (`OpenPeer`, `SelectChat`, `EditDraft`, `SendDraft`, `SaveScroll`, `SaveBounds`, `RequestStop`, `Join`); model thread owns `ChatWorkspace`, `ChatEntry`, `ChatRoom`, `DirectoryDomainStorage` at `state_dir/model`, `SharedSyncRuntime`, and `AetherByteTransport`.
   - Aether thread manages `AetherApp` and network state in `state_dir/aether`, dispatching all frame and lifecycle notifications onto the model thread queue.
   - Deterministic bootstrap role selection: `canonical local UID < canonical remote UID` designates room creator; peer waits for room admission from authorized endpoint.
   - Reopen peers from persistent links; restores and re-registers all rooms from persistent storage.
   - UI publication via `PublicationChannel<3>` transmitting initial and structural workspace changes alongside copied `ChatRuntimeStatus`.
   - Orderly shutdown: stops user commands, stops/joins Aether runtime, drains queued tasks, persists workspace, frees sync runtime and transport, and cleans up domain.

4. **Win32 Chat Application (`apptraverse_chat`)**:
   - Implemented Win32 host in `examples/chat_demo/windows/` (`main.cpp`, `win_chat_app.h`, `win_chat_app.cpp`, `CMakeLists.txt`).
   - Pure native Win32 controls: ListBox for chats, Msftedit RichEdit for read-only transcript, multiline Edit for draft, Send button, peer connection inputs, status line, and presence indicator.
   - UTF-8 / UTF-16 conversions at command boundaries.
   - Exclusive profile lock (`CreateFileW` with `FILE_SHARE_READ | FILE_SHARE_WRITE` disabled on `profile.lock`) preventing concurrent access to the same state directory.
   - Geometry restoration: restores normal bounds and maximized state via `WINDOWPLACEMENT` with off-screen monitor detection and clamping.
   - Transcript and draft management: Ctrl+Enter sends, local edit revision tracking prevents stale publications from overwriting active user input, caret position preserved across publications.
   - Scroll anchor restoration: message-based `ScrollAnchor` tracking with viewport preservation on non-tail viewing and 200ms scroll command coalescing.

5. **Headless Integration & Smoke Tests**:
   - Implemented comprehensive `tests/chat_session_integration_test.cpp`: verifies room discovery without advance ObjId knowledge, single creator selection, rejection of unauthorized sources and wrong root classes, workspace binding before ACK, restart persistence of room and queued messages, and private field isolation.
   - Implemented `tests/chat_windows_smoke_test.cpp` for native Win32 verification.

---

# CURSOR — CHAT DEMO 04: MODEL-THREAD DELIVERY + SAFE AETHER MUX + CROSS-PLATFORM BUILD

## Identity

- Starting AppTraverse SHA: `e443e5ce099070244b760636caed98d561f65d41`
- Pinned aether SHA: `0b0e3b54b9ffa730c41597c8b18f6a75255bded3` (unchanged)
- Direct main only: tests → commits → push origin/main. NO PR. NO feature branch. NO force push.

## What Landed

1. **Aether Thread Boundary & Model-Thread Delivery**:
   - `ChatAetherRuntime` remains owned by its dedicated Aether worker thread.
   - `SharedSyncRuntime`, `ae::Domain`, `ChatWorkspace`, `ChatEntry`, `ChatRoom`, and `LinkSyncState` are never mutated directly from the Aether thread.
   - Introduced `IAetherFrameEndpoint` in `examples/chat_demo/aether/aether_frame_endpoint.h` decoupling `AetherByteTransport` from the concrete runtime.
   - Added `ModelTask` and `ModelDispatch` callbacks to `AetherByteTransport`. Incoming frames on the Aether thread are enqueued via caller-supplied `ModelDispatch` to be executed on the model thread.
   - Completely removed `[this]` capture from runtime frame callbacks. Captured only `std::weak_ptr<ReceiveBinding>` where `ReceiveBinding` holds `mu`, `active`, `receive_ctx`, `receive_fn`, and `dispatch`.
   - In `~AetherByteTransport()`, unbinds frame callback, locks `receive_binding_->mu`, sets `active = false`, clears `receive_ctx` and `receive_fn`, and resets binding. Queued tasks in flight observe `active == false` and safely drop delivery.
   - `ClearReceive()` clears context and callback under mutex, suppressing queued delivery.

2. **Internal Aether Stream Framing Envelope & Multiplexer**:
   - Replaced old 14-byte heartbeat with an internal stream framing envelope in `examples/chat_demo/aether/aether_stream_frame.{h,cpp}`.
   - Wire format: `'A' 'T' 'R' 'N'` (4 bytes), `version = 1` (1 byte), `kind` (1 byte: 1=Application, 2=HeartbeatPing, 3=HeartbeatPong), `payload_size` (4 bytes little-endian), `payload`.
   - Maximum application payload size 16 MiB. Heartbeat payload exactly 8-byte little-endian nonce.
   - Strict decoder enforces exact magic, version 1, known kinds, exact length match (no trailing bytes), and drops malformed frames without touching application state.
   - Fully collision-free: arbitrary application payloads (even those containing `ATHB` sequences) round-trip with byte-for-byte fidelity without colliding with heartbeats.

3. **Presence & Transport-Local Duplicate Suppression**:
   - `PeerPresence` remains strictly runtime-only.
   - Reconnect transitions: when an Aether stream is newly bound or rebound from offline, presence transitions to `kConnecting` if not already `kOnline` (including `Offline -> Connecting`).
   - Outbound queue (`pending_out`) in `ChatAetherRuntime`: implemented transport-local duplicate suppression (`if (!pending_out.empty() && pending_out.back() == bytes) do not append`), preventing duplicate pileup before P2P stream link.

4. **Thread-Safe Test Handshakes & Model Dispatcher**:
   - Added `ManualModelDispatcher` to `tests/chat_aether_p2p_test.cpp` ensuring all `Domain`, `SharedSyncRuntime`, and chat model operations run on the model/test thread.
   - Protected all `ready` and `local_uid` handshakes with `std::mutex` and `std::condition_variable`.

5. **Unit Tests**:
   - `tests/aether_stream_frame_test.cpp`: Tests frame codec with empty payloads, binary payloads resembling heartbeats, 16 KiB random data, ping/pong nonces, invalid magic, invalid version, unknown kind, truncated headers, mismatched lengths, invalid heartbeat sizes, and oversized payloads.
   - `tests/aether_byte_transport_dispatch_test.cpp`: Tests deferred delivery via `ModelDispatch`, verified execution on `Drain()`, destruction before drain dropping delivery, and `ClearReceive` suppressing delivery.

6. **Cross-Platform Build & Model-Only Support**:
   - Updated `examples/chat_demo/CMakeLists.txt` so `chat_demo_model` is always created, while `chat_demo_aether` and `apptraverse_chat_aether_probe` are guarded by `if(TARGET aether)`.
   - Updated `tests/CMakeLists.txt`: `apptraverse_chat_aether_p2p_test` is guarded by `if(UNIX AND NOT EMSCRIPTEN AND TARGET chat_demo_aether)`. Unit tests `apptraverse_aether_stream_frame_test` and `apptraverse_aether_byte_transport_dispatch_test` compile whenever `chat_demo_aether` exists.

---

# CURSOR — CHAT DEMO 03: REAL AETHER TRANSPORT + HEARTBEAT PRESENCE

## Identity

- Starting AppTraverse SHA: `efd0838029fa0f836e44e97153046109556f472a`
- Pinned aether SHA: `0b0e3b54b9ffa730c41597c8b18f6a75255bded3` (unchanged)
- Direct main only: tests → commit → push origin/main. NO PR. NO feature branch. NO force push.

## What Landed

0. **Snapshot Admission Source-Share Uniqueness Fix**:
   - Modified `ImportValidatedNode` in `include/apptraverse/shared_sync_runtime.h` and `src/shared_sync_runtime.cpp`: moved source-share validation to candidate scratch inspection before `CommitObjectGraph` and production collision checks.
   - Verifies exactly one matching share whose `Link::EndpointUid() == source_endpoint`, ensures matching share ID is valid, and verifies candidate's `FindLinkSyncIndexForShare(matching_share_id)` identifies exactly one local `LinkSyncState`.
   - Returns `ImportedNode` struct `{ SharedNode::ptr node; ae::ObjId source_share_id; }`. If validation fails, returns empty with zero production writes.
   - `OnNodeState` uses returned `source_share_id` directly without duplicate lookup.
   - Added regression test `TestSnapshotWithTwoSourceSharesRejected` in `tests/shared_node_initial_sync_test.cpp` verifying rejection, zero storage entries, zero domain objects, no imported links, and no ACK.

1. **IByteTransport Header Ownership**:
   - Extracted `IByteTransport` from `include/apptraverse/memory_transport.h` to dedicated `include/apptraverse/byte_transport.h`.
   - Updated `memory_transport.h` and `shared_sync_runtime.h` includes.

2. **PeerPresence Enum (Runtime-Only)**:
   - Created `examples/chat_demo/common/chat_presence.h` defining `PeerPresence { kUnknown = 0, kConnecting = 1, kOnline = 2, kOffline = 3 }`.
   - Kept presence strictly runtime-only with no persistent fields in model classes.

3. **Common Native Aether Runtime (`ChatAetherRuntime`)**:
   - Implemented `examples/chat_demo/aether/chat_aether_runtime.{h,cpp}`:
     - Owns `AetherApp`, `Client`, dedicated `std::thread`, `P2pStream` peer states, and command queue (`OpenPeer`, `Send`, `ClosePeer`).
     - Public API matches exact specification (`Start`, `OpenPeer`, `Send`, `ClosePeer`, `RequestStop`, `Join`, `~ChatAetherRuntime`).
     - Initialization via `AetherApp::Construct` with `DirectoryDomainStorage`, `EthernetAdapter`, and `SelectClient(parent, config.client_name)`.
     - Single authoritative `PeerState` per peer UID (`std::unordered_map<std::string, PeerState> peers`).
     - Outbound `OpenPeer` constructs `P2pStream` via `message_stream_manager().CreatePort(uid)`.
     - Inbound P2P subscribed to `message_stream_manager().new_port_event()` with deterministic deduplication (keep linked stream, replace unlinked stream with old subscription cleanup).
     - Dedicated thread lifecycle: all Aether objects created, run, and destroyed on the Aether thread.

4. **Heartbeat Protocol and Presence Semantics**:
   - 14-byte binary heartbeat frame (`'A' 'T' 'H' 'B'`, version 1, type 1=Ping / 2=Pong, 64-bit nonce).
   - Heartbeat scheduling and validation (`Config.heartbeat_period_ms >= 250`, `Config.offline_after_ms >= heartbeat_period_ms * 2`).
   - Steady-clock monotonic timing for Ping interval and timeout-based Offline transition.
   - Automatic Pong reply with matching nonce. Application bytes and Ping/Pong frames promote peer to `kOnline`.
   - Stream link errors transition to `kConnecting`.

5. **AetherByteTransport Adapter**:
   - Implemented `examples/chat_demo/aether/aether_byte_transport.{h,cpp}`: thin adapter implementing `apptraverse::IByteTransport` over `ChatAetherRuntime`.
   - Bridges `runtime.Send` and routes application frames directly to registered `ReceiveFn`.

6. **Headless Probe Executable (`apptraverse_chat_aether_probe`)**:
   - Implemented `examples/chat_demo/aether/chat_aether_probe.cpp`:
     - CLI flags: `--state-dir`, `--client-name`, `--peer-uid`, `--heartbeat-ms`, `--offline-ms`.
     - Emits `READY uid=<uid>`, `PRESENCE peer=<uid> state=<state>`, and `RX peer=<uid> bytes=<n> text=<text>`.
     - Supports `send <peer_uid> <text>` stdin commands.

7. **Multi-Process Real Aether Verification Test**:
   - Implemented `tests/chat_aether_p2p_test.cpp` (`apptraverse_chat_aether_p2p_test`):
     - Test 1 (Probe Subprocesses): Spawns two independent probe processes with distinct state directories, verifies UID discovery, mutual P2P stream connection, heartbeat presence (`Online`), bidirectional application text delivery, and timeout-based `Offline` transition when process terminates.
     - Test 2 (Full Chat Sync): Spawns Replica A and Replica B over real Aether using `AetherByteTransport` and `SharedSyncRuntime`. Replicates `ChatRoom` via initial snapshot, completes initial sync, exchanges `MessageAddedEvent` in both directions, verifies ACK reception, and validates 2 synchronized messages on both replicas.

## Tests

- `apptraverse_chat_aether_p2p_test`: PASS (both Test 1 Probe P2P/presence and Test 2 two-process chat sync).
- `apptraverse_shared_node_initial_sync_test`: PASS (including snapshot duplicate source-share rejection regression).
- `apptraverse_chat_demo_sync_test`: PASS.
- `apptraverse_chat_demo_model_test`: PASS (15 scenarios + DirectoryDomainStorage).
- `apptraverse_shared_node_incremental_event_test`: PASS.
- `apptraverse_shared_node_foundation_test`: PASS.
- `apptraverse_event_sourced_core_test`: PASS.
- `apptraverse_dynamic_objects_add_test`: PASS.
- `apptraverse_journal_retention_test`: PASS.
- `apptraverse_model_runtime_stop_test`: PASS.
- `apptraverse_publication_channel_test`: PASS.

---
Status: implemented/verified on main. Not accepted.

# CURSOR — CHAT DEMO 02: SAFE TWO-WAY SCALAR CHAT SYNC

## Identity

- Starting AppTraverse SHA: `c96417288138a6954eae7054095b911962433784`
- Direct main only: tests → commit → push origin/main. NO PR. NO feature branch. NO force push.

## What Landed

1. **Chat Model Correctness Fixes**:
   - **Sequence Overflow Protection**: `SubmitDraft` rejects `next_message_sequence == 0` or `next_message_sequence == UINT64_MAX` before creating the reservation event. Added `ChatWorkspace::CanApply(MessageSequenceReservedEvent const&)` requiring exact `reserved_sequence == next_message_sequence` and strictly not 0 or `UINT64_MAX`. `Apply(MessageSequenceReservedEvent)` sets `next_message_sequence = event.reserved_sequence + 1` without redundant `>=` check.
   - **Chat Selection Membership Validation**: `SelectChat` verifies `entry_id` is valid and actually present in `workspace.chats` before updating selection or committing `ChatSelectedEvent`. Added `ChatWorkspace::CanApply(ChatSelectedEvent const&)` using the same membership rule.
   - **Message Payload and Journal Metadata Consistency**: Added public virtual `MatchesSharedMetadata(SharedEventId const&, SharedEventOrder const&)` to `Event`. In `Node::TryCommitSharedInto`, `event->MatchesSharedMetadata(identity, order)` is validated before constructing/inserting `EventRecord`. `MessageAddedEvent` overrides this to ensure `message.id == identity && message.timestamp_us == order.timestamp_us`.

2. **Standalone-Scalar Event Network Ingress (No OperationStorage)**:
   - Added `AllowStandaloneEventClass` and `IsStandaloneEventClassAllowed` to `SharedSyncRuntime`.
   - `SyncNextEvent` and `OnEvent` gate incremental sync strictly on allowed standalone Event classes.
   - Added `ValidateStandaloneEventGraph` and `ImportStandaloneEventGraph` in `shared_network_graph.{h,cpp}`: verifies single-object closed graph of the expected Event class, allocates a collision-safe receiver-local `ObjId`, and copies native stored class/version layers via `CopyStoredObjectAs` without `OperationStorage` or pointer remapping.
   - Wired `ImportStandaloneEventGraph` into `SharedSyncRuntime::PreflightHistoricalEventInsertion` and `SharedSyncRuntime::OnEvent`.

3. **Collision Detection for Initial Snapshots**:
   - In `SharedSyncRuntime::ImportValidatedNode`, before any production write (`CommitObjectGraph`), verified that none of the parsed snapshot object IDs collide with existing live objects in `domain_` or persisted records in `storage_`. On collision, snapshot is rejected with no partial writes and no ACK.
   - Added regression test `TestReceiverLocalSentinelCollisionRejected` in `shared_node_initial_sync_test.cpp`.

4. **Two-Way Initial Sync Semantics**:
   - Added `CompleteFromReceivedSnapshotEvent` on `LinkSyncState` (version 0 native serialization: `dnv(base_, delivered_event_ids)`).
   - In `SharedSyncRuntime::OnNodeState`, when a snapshot is imported (`imported == true`), finds the single source Share whose Link `EndpointUid() == source_endpoint`, collects all covered `SharedEventId`s from `node->journal`, and executes `source_state->CompleteFromReceivedSnapshot(covered_ids)` and saves `source_state` before sending ACK.
   - This transitions the source Share's `LinkSyncState` to `InitialSyncPhase::Complete`, allowing the receiving replica to immediately author and send replies over `SyncNextEvent`.

5. **Two-Way Chat Synchronization Product Test**:
   - Implemented `tests/chat_demo_sync_test.cpp` (`apptraverse_chat_demo_sync_test`):
     - Sets up independent replicas A and B over `MemoryNetwork`.
     - Replicates `ChatRoom` via initial snapshot from A to B.
     - Proves B's source share to A is immediately `InitialSyncPhase::Complete`.
     - A sends message to B, acknowledged and received.
     - B replies to A, acknowledged and received.
     - Exact ACK retry / duplicate handling verified when ACK dropped.
     - Persistence and domain restart verified from `RamDomainStorage`.
     - Tampered message payload with mismatched metadata rejected without ACK.

## Tests

- `apptraverse_chat_demo_model_test` (18 scenarios): all passed.
- `apptraverse_chat_demo_sync_test`: passed.
- `apptraverse_shared_node_initial_sync_test`: passed (including sentinel collision regression).
- `apptraverse_shared_node_incremental_event_test`: passed.
- All primary tests passing cleanly.

---
Status: implemented/verified on main. Not accepted.

# CURSOR — CHAT DEMO 01: SHARED MESSAGE MODEL + LOCAL WORKSPACE

## Identity

- Starting AppTraverse SHA: `75fe82d56d7df1ff86c1ddf2c25bc2f1115bbf71`
- Direct main only: tests → commit → push origin/main. NO PR. NO feature branch. NO force push.

## What Landed

1. **Common Model & Ownership Architecture**:
   - `ChatRoom` (derived from `SharedNode`): shared message room containing `std::vector<MessageValue> messages`, replicated over Aether without exposing local workspace state.
   - `ChatEntry` (derived from `Node`): local workspace entry binding peer Admin ID / display name to a draft, scroll anchor, peer `Link`, and shared `ChatRoom`.
   - `ChatWorkspace` (derived from `Node`): local persistence root managing `chats`, `desktop_bounds`, `local_endpoint_uid`, and monotonic `next_message_sequence`.
   - Value types: `MessageValue`, `ScrollAnchor`, `DesktopBounds`.
   - Object graph direction: `Workspace → ChatEntry → ChatRoom` and `Workspace → ChatEntry → Link`. No back-pointers from `ChatRoom` to local workspace state.

2. **Native Aether Serialization**:
   - Each registered class serializes base and its own fields via native `Load`/`Save` (`dnv(base_, ...)`):
     - `ChatRoom` (v0): `dnv(base_, messages)`
     - `ChatEntry` (v0): `dnv(base_, peer_admin_id, display_name, peer_link, room, draft, scroll)`
     - `ChatWorkspace` (v0): `dnv(base_, local_endpoint_uid, next_message_sequence, desktop_bounds, chats, selected_chat_id)`
   - All events (`ChatEntryAddedEvent`, `ChatSelectedEvent`, `LocalEndpointBoundEvent`, `MessageSequenceReservedEvent`, `DesktopBoundsChangedEvent`, `ChatBindingChangedEvent`, `DraftChangedEvent`, `ScrollChangedEvent`, `MessageAddedEvent`) use native version-0 serialization.

3. **Pure Projection, Events, and Commands**:
   - Commands: `OpenOrSelectChat`, `BindChat`, `BindLocalEndpoint`, `SetDraft`, `SetScroll`, `SetDesktopBounds`, `SelectChat`, `SubmitDraft`.
   - `Apply` methods only mutate materialized fields and call `NoteMaterializedChange()`.
   - Invariant: `SubmitDraft` and `MessageAddedEvent` do not alter `ScrollAnchor`.
   - Commands accept an optional `PersistLocalState` callback executed after local events are committed.

4. **AeroAdmin Launch Options Parser**:
   - `ParseChatLaunchOptions` parses `--admin-id`, `--state-dir`, `--name`, `--uid`.
   - Robust argument validation (e.g., rejecting `--name` or `--uid` without `--admin-id`, duplicate options, missing values).

5. **Test Proofs**:
   - `apptraverse_chat_demo_model_test`: 15 comprehensive model scenarios covering workspace creation, chat selection, unresolved persistence, room binding, draft submission, sequence reservation, persistence & domain destruction reload, scroll anchor preservation, desktop bounds, tie-breaking insertion, base replay preservation, and shared network scratch export proving no local workspace state leaks into shared state. Also verified `DirectoryDomainStorage` round-trip.
   - `apptraverse_chat_demo_launch_options_test`: Complete command-line parser regression coverage.

---
Status: implemented/verified on main. Not accepted.

# CURSOR — FIX 01: NATIVE CLASS-LAYER SERIALIZATION

## Identity

- Starting AppTraverse SHA: `6c21a17a6af6d00776cc9ad088aeab22ade2c245`
- Pinned aether-objects SHA: `1d30264737c9bcca8a181161116b66c7dbeeb5fb`
- Pinned aether-client-cpp SHA: `0b0e3b54b9ffa730c41597c8b18f6a75255bded3`
- Direct main only: tests → commit → push origin/main. NO PR. NO feature branch. NO force push.

## What Landed

1. **Native Class-Layer Dispatch Restored**:
   - Every registered concrete `Node`-derived class now serializes its direct registered base through `dnv(base_, ...)` and then its own fields.
   - Removed direct ancestor calls (`Node::Load`, `Node::Save`, `SharedNode::Load`, `SharedNode::Save`, `Link::Load`, `Link::Save`, etc.).
   - Passing `base_` to `dnv` routes back through `DomainGraph` to save/load the base class's own layer under the base class ID and native schema version.
   - Separate layers verified in `RamDomainStorage` without duplicate copies of base fields in derived layers.

2. **kNodeJournalFormat Removed & Schema Versioning Restored**:
   - Removed `kNodeJournalFormat` and format-word markers.
   - Removed comments claiming native class versions cannot distinguish base layers.
   - `Node` current registered version bumped from 3 to 4. `Node::Load(Version<4>)` and `Save(Version<4>)` serialize `dnv(base_, base, journal)`.
   - Versions 0–3 of `Node` retain explicit rejection loaders with informative error messages that do not mutate storage.
   - Concrete derived classes whose persisted layer changed increased their own current version by 1 and added rejection loaders for their old flattened layouts.

3. **Class/Version Inventory**:
   - `Node` (base `ae::Obj`): v3 → v4 (`dnv(base_, base, journal)`)
   - `Link` (base `Node`): v0 (`dnv(base_)`)
   - `MemoryLink` (base `Link`): v1 → v2 (`dnv(base_, endpoint_uid, heartbeat_interval_ms)`)
   - `SharedNode` (base `Node`): v1 → v2 (`dnv(base_, shares, link_sync_states)`)
   - `LinkSyncState` (base `Node`): v2 → v3 (`dnv(base_, share_id, ..., pending_event_packet)`)
   - `ApplicationRuntimeState` (base `Node`): v1 → v2 (`dnv(base_, run_id)`)
   - `NetworkState` (base `Node`): v1 → v2 (`dnv(base_, run_id, availability)`)
   - `AetherRegistrationState` (base `Node`): v1 → v2 (`dnv(base_, run_id, registered_run_id, phase, uid)`)
   - `SharedValueNode` (base `SharedNode`): v1 → v2 (`dnv(base_, value)`)
   - `Client` (base `Node`): v1 → v2 (`dnv(base_, name, link)`)
   - `ChildSharedNode` (base `SharedNode`): v1 → v2 (`dnv(base_, child_value)`)
   - `RootSharedNode` (base `SharedNode`): v1 → v2 (`dnv(base_, root_value, child)`)
   - `Surface` (base `Node`): v2 → v3 (`dnv(base_, number, ..., presenter)`)
   - `Surfaces` (base `Node`): v1 → v2 (`dnv(base_, surfaces, mobile_current)`)
   - `ItemList` (base `Node`): v1 → v2 (`dnv(base_, items, presenter, window)`)
   - `MainWindow` (dynamic_demo, base `Node`): v1 → v2 (`dnv(base_, x, y, width, height, item_list, add_item, presenter)`)
   - `MainWindow` (main_window_runtime_demo, base `Node`): v4 → v5 (`dnv(base_, x, y, width, height, presenter)`)
   - `ChatClient` (base `Node`): v1 → v2 (`dnv(base_, display_name, aether_uid, presence)`)
   - `ChatRoom` (base `Node`): v1 → v2 (`dnv(base_, clients, feed)`)
   - `Item` (closed_event_graph_test, base `Node`): v0 → v1 (`dnv(base_, name, scalar_old_id, metadata, local_link)`)
   - `ContainerNode` (closed_event_graph_test, base `Node`): v0 → v1 (`dnv(base_, items)`)
   - `StateDependentNode` (shared_node_incremental_event_test, base `SharedNode`): v1 → v2 (`dnv(base_, state_code)`)
   - `NoteTargetNode` (shared_node_incremental_event_test, base `Node`): v0 (`dnv(base_)`)
   - `Counter` (model_runtime_stop_test, base `Node`): v0 → v1 (`dnv(base_, value)`)
   - `RetentionDoc` (journal_retention_test, base `Node`): v2 → v3 (`dnv(base_, value)`)
   - `CounterDocument` (event_sourced_core_test, base `Node`): v1 → v2 (`dnv(base_, value, label)`)
   - `TextToolbar`, `ColorToolbar`, `CenterStrip`, `Window`, `PaintWindow`, `LayoutWindow` (model_ui_runtime_demo, base `Node` / `Window`): explicit v0 Load/Save with `dnv(base_, ...)`.

4. **Cross-Process Base-Version Evolution Test**:
   - `native_class_layers_writer_v1`: writes `LayerDerived` (LayerBase v0, LayerDerived v0) with `base_value = 11`, `derived_value = 22`.
   - `native_class_layers_reader_v2`: reads using `LayerBase` v1 (with v0 compatibility loader supplying default `999` for newly added field) while `LayerDerived` remains at v0.
   - Verified cross-process execution without ODR issues; reader restored `base_value == 11`, `derived_value == 22`, and `added_base_field == 999` without bumping `LayerDerived` version.

5. **Old Flattened Development State Rejection**:
   - Tested synthesized old entries for `Node` v3, `SharedValueNode` v1, `MemoryLink` v1, and `LinkSyncState` v2.
   - Confirmed explicit `std::runtime_error` throws without mutating or rewriting storage.

6. **Real Model Verification**:
   - `SharedValueNode`: separate storage layers for `Node` (v4), `SharedNode` (v2), and `SharedValueNode` (v2); verified no duplicate fields.
   - `MemoryLink`: separate storage layers for `Node` (v4), `Link` (v0), and `MemoryLink` (v2).
   - `LinkSyncState`: separate storage layers for `Node` (v4) and `LinkSyncState` (v3).
   - Verified Save → destroy Domain → Load root; base snapshot capture; mid-journal compaction and replay from base; `LinkSyncState` persistence across business replay.

7. **Known Remaining Limitations**:
   - Unsafe `OperationStorage` context handling (`Write` side-channel) remains a known separate issue outside this slice's scope.

---
Status: implemented/verified on main. Not accepted.

# CURSOR — Remove AppTraverse Custom Object Serialization

## Identity

- Starting AppTraverse SHA: `4e185b6a2d7883f8ec3236fa3ee93ce8129e0d40`
- Pinned aether-objects SHA: `1d30264737c9bcca8a181161116b66c7dbeeb5fb`
- Pinned aether-client-cpp SHA: `0b0e3b54b9ffa730c41597c8b18f6a75255bded3`
- Aether dependency changes required: NO (plugs directly into native `IDomainStorage` / `IDomainStorageWriter` and `Serializer<BinaryArchive<DomainBuffer>, ObjectLink>` extension points)
- Direct main only: tests → commit → push origin/main. NO PR. NO feature branch. NO force push.

## Architecture Note: Native Aether Serialization Investigation (Phase 1)

NATIVE MECHANISM FOUND:
- `ae::seri::BinaryArchive<Buffer>`: `aether-miscpp/serialization/binary_archive.h`, `details/binary_archive.h`
- `ae::seri::BinaryVectorBuffer<SizeType>` / `LimitedVectorBuffer<SizeType>`: `aether-miscpp/serialization/details/binary_vector_buffer.h`
- Standard serializers (`std::vector`, `std::map`, `std::set`, `std::optional`, `std::variant`, `std::string`, `std::pair`, `std::tuple`, numerics): `aether-miscpp/serialization/details/binary_std_serializers.h`
- Reflected type serializer: `aether-miscpp/serialization/details/reflectable_serializer.h`, `details/member_serializer.h`
- Object system & domain reflection: `aether-objects/obj/obj.h`, `aether-objects/obj/domain.h`, `aether-miscpp/domain_visitor/domain_visitor.h` (`AE_OBJECT_REFLECT`, `AE_REFLECT`, `AE_REF_BASE`, `DomainVisit`)
- Graph traversal & cycle detection: `DomainGraph::SaveRoot`, `DomainGraph::LoadRoot`, `DomainGraph::LoadCopy`, `DomainGraph::Save`, `DomainGraph::Load`, `DomainCycleDetector`: `aether-objects/obj/domain.h`, `domain.cpp`
- Class schema versioning: `ae::Version<N>`, `version_iterator<VersionSaveTrait>` / `version_iterator<VersionLoadTrait>`: `aether-miscpp/meta/version.h`, `aether-objects/obj/version_iterator.h`
- Object pointer serialization: `Serializer<BinaryArchive<DomainBuffer>, ObjPtr<T>>` in `aether-objects/obj/obj_ptr.h` and `ObjPtrBaseSerializer` in `aether-objects/obj/obj_ptr_base.cpp`
- Object registry & factories: `ae::Registry`, `ae::Registrar<T>`, `ae::Factory`, `GenerationDistance`: `aether-objects/obj/registry.h`, `registrar.h`
- Domain storage abstractions: `ae::IDomainStorage`, `ae::IDomainStorageWriter`, `ae::IDomainStorageReader`, `ae::DomainQuery`: `aether-objects/obj/idomain_storage.h`
- In-memory domain storage: `ae::RamDomainStorage` with `State = std::map<ObjId, std::optional<std::map<uint32_t, std::map<Version::Type, std::vector<uint8_t>>>>>`: `aether-objects/domain_storage/ram_domain_storage.h`
- Examples/tests in aether-client-cpp: `aether/domain_storage/spifs_domain_storage.cpp` (direct `BinaryArchive.Save`/`Load` of nested `object_map_`), `aether/api_protocol/api_pack_parser.h` (`ApiPacker` / `ApiPackParser` via `BinaryArchive`), `tests/test-domain-storage/test_ds_synchronization.cpp`.

WHAT IT ALREADY PROVIDES:
- One unified serialization model: `BinaryArchive` over buffer concepts (`BinaryVectorBuffer`, `DomainBuffer`).
- Automatic recursive graph traversal via `DomainGraph::SaveRoot` with `DomainCycleDetector`.
- Schema evolution through native `Save(ae::Version<N>, ...)` and `Load(ae::Version<N>, ...)`.
- Serialization of arbitrary reflected structs, nested structs, and containers (`std::vector`, `std::map`, etc.) with zero manual byte packing.
- Robust bounds checking on deserialization: `BinaryVectorBuffer` checks bounds and returns `ae::Error{read_eof}`, preventing crashes on truncated or malformed payloads.
- `RamDomainStorage::State` is entirely composed of native types (`std::map`, `ObjId`, `uint32_t`, `uint8_t`, `vector<uint8_t>`, `optional`) with existing `Serializer<BinaryArchive<Buffer>, T>` specializations, allowing direct `archive.Save(state)` and `archive.Load(state)` without manual object-table wire encoding or `AppendU32`/`ReadU32`.

WHAT SMALL CAPABILITY, IF ANY, IS MISSING:
- Native Aether `DomainGraph` persistence assumes sender and receiver share the same storage `ObjId` space. When receiving a network-shared graph, sender IDs can collide with receiver-local objects or receiver storage.
- AppTraverse's `ObjectLink` (`SharedPtr` / `LocalPtr`) bridges this via native `Serializer<BinaryArchive<DomainBuffer>, ObjectLink<T, Scope>>`:
  1. `LocalPtr` under `NetworkShared` scope serializes as null and skips traversal.
  2. `SharedPtr` remaps IDs to receiver-local `ObjId`s during operation-scoped transfer across domains.
  3. Deserialization into disposable scratch storage validates graph closure, class compatibility, and absence of external references before committing to receiver storage.

## Phase 2 & 3: Custom Object Serialization and Duplicate Reflection Removal

1. **Deleted Duplicate Object-Table Serialization**:
   - Removed `SerializeRamDomainStorage` and `ParseObjectGraphPayload`.
   - Removed `FreezeStandaloneEventPayload`, `ParseStandaloneEventPayload`, and `CommitStandaloneEventObject`.
   - Removed `FreezeClosedEventGraphPayload` and `ParseClosedEventGraphPayload`.
   - Removed `AppendU32`, `ReadU32`, `object_count`, `class_count`, `version_count`, and manual class/version table wire encoding from `src/shared_network_graph.cpp`.
   - Replaced with unified native Aether `BinaryArchive` serialization of `RamDomainStorage::State`: `SerializeObjectGraph`, `DeserializeObjectGraph`, `FreezeEventPayload`, and `ParseEventPayload`.

2. **Unified Event Graph Serialization**:
   - Standalone/scalar Event and closed Event graph are now ONE serialization format.
   - `FreezeEventPayload(event, boundary, out_payload)` serializes any reachable Event graph subject to the export boundary.
   - Overload `FreezeEventPayload(event, out_payload)` enforces standalone boundary (`{event.obj_id}`).
   - `ParseEventPayload(payload, parsed, out_root_id)` deserializes with native bounds checks.

3. **Removed Duplicate Pointer Remapping & Reflection Visitors**:
   - Deleted `include/apptraverse/remap_pointers.h` (`RemapField`, `RemapReflectedPointers`, `ValidateFieldPointer`, `ValidateReflectedPointers`).
   - Removed virtual `RemapPointers` and `ValidatePointers` from `Event`, `Node`, `EventFor`, and `NodeFor`.
   - Converted `Node::base` from `Node::ptr` to `SharedPtr<Node>`.
   - Implemented operation-scoped `detail::OperationStorage` and `detail::OperationStorageWriter` in `include/apptraverse/operation_storage.h`.
   - Validation and remapping hook directly into `Serializer<BinaryArchive<DomainBuffer>, ObjectLink<T, Scope>>`:
     - In `kValidate` mode: ensures all references are closed within the parsed bundle, validates target class inheritance via `Registry::GenerationDistance`, verifies `LocalPtr` is null.
     - In `kRemap` mode: updates IDs, Domains, and cached targets to receiver-local objects, and resets `LocalPtr`.
     - Standard `DomainGraph::SaveRoot` automatically traverses base classes, nested structs, and container elements without duplicate reflection code.

## Verification

- `apptraverse_native_object_graph_serialization_test`: PASS
- `apptraverse_closed_event_graph_test`: PASS (13/13 test cases, 50/50 consecutive runs clean)
- `apptraverse_shared_node_incremental_event_test`: PASS
- `apptraverse_shared_node_initial_sync_test`: PASS
- `apptraverse_shared_node_foundation_test`: PASS
- `apptraverse_event_sourced_core_test`: PASS
- `apptraverse_dynamic_objects_add_test`: PASS
- `apptraverse_journal_retention_test`: PASS
- `apptraverse_model_runtime_stop_test`: PASS
- `apptraverse_publication_channel_test`: PASS

## Code Size / Complexity

AppTraverse production code was reduced:
- Production LOC removed: 661
- Production LOC added: 544 (237 in existing files + 307 in `include/apptraverse/operation_storage.h`)
- Net reduction: -117 lines

---
Status: implemented/verified on main. Not accepted.

# CURSOR — Finish closed Event graph serialization and import

## Identity

- Scope: Implement self-contained closed Event graph serialization (Freeze, Parse, Validate, Import) with typed reference remapping, alias preservation, explicit export boundary, and receiver collision prevention on `main`.
- Direct push to `origin/main`. No PR. No feature branch.
- EventFrame / ACK / retry changes: NONE. Dynamic child updates/deletions: NOT IMPLEMENTED.

## What landed

- **Export Boundary**: Introduced `EventGraphExportBoundary` defining permitted network-shared object IDs for a bundle. Any reachable network-shared object outside the boundary causes `FreezeClosedEventGraphPayload` to fail explicitly.
- **Reflection-Based Pointer Remapping and Validation**:
  - Implemented `include/apptraverse/remap_pointers.h` with generic `RemapReflectedPointers` and `ValidateReflectedPointers` using `ae::reflect::make_reflection` without C++ RTTI or class-specific switches.
  - Added virtual `RemapPointers` and `ValidatePointers` to `Event` and `Node`.
  - Concrete `EventFor<Target, ConcreteEvent>` and `NodeFor<ConcreteNode, BaseNode>` invoke reflection-driven remapping and validation over all reflected fields.
  - `Node::RemapPointers` and `Node::ValidatePointers` explicitly remap and validate `Node::base` with generation distance type compatibility check.
  - `SharedPtr<U>` and `ae::ObjPtr<U>` references are remapped to freshly allocated receiver-local `ObjId`s.
  - `LocalPtr<U>` (and vectors of `LocalPtr`) are explicitly excluded and reset on import.
  - Ordinary scalar values (including integers equal to old sender `ObjId`s) are preserved untouched.
- **Zero-Mutation Validation Before Import**:
  - `ValidateClosedEventGraphStorage` validates object table, inheritance class chains, instantiable leaf classes, root `Event` derivation, and typed pointer closure / type compatibility in a disposable scratch domain and storage copy before touching the receiver.
  - Malformed payloads or missing references fail before any write to receiver domain or storage.
- **Receiver-Local ID Remapping & Alias Preservation**:
  - `AllocateUniqueReceiverObjId` allocates fresh `ObjId`s that do not collide with receiver storage, live receiver `Domain`, or other IDs reserved in the same import session.
  - Exactly one mapping entry per included object; multiple pointers to the same object preserve aliasing.
  - Imported `AddItemEvent::Apply` attaches the imported `Item` without creating duplicate objects during `Apply` or forced journal replay.
- **Source Independence**:
  - Freezing produces a self-contained payload without modifying source domain or storage.
  - Parsed payloads can be imported and survive destruction of the source domain and receiver domain restart (Save → destroy → Load).

## Tests

- `apptraverse_closed_event_graph_test`:
  - `TestImportAndAliasPreservation`: Event referencing `Item` (Node), duplicate reference to same `Item`, and `Metadata` (Obj) referenced by `Item` imports with alias intact and unique mapping entries.
  - `TestNodeBaseRemapping`: `Node::base` correctly remapped to receiver-local `ObjId` and loadable.
  - `TestAllSenderIdsOccupiedBySentinels`: Receiver with sentinel objects occupying sender `ObjId`s allocates completely disjoint IDs without collisions or overwrites.
  - `TestZeroSourceWritesDuringFreeze`: Freeze causes zero writes to source `IDomainStorage`.
  - `TestSourceDestroyedBeforeImport`: Freezing, destroying source domain and storage, and importing into receiver succeeds and produces fully functional object graph.
  - `TestLocalPtrExclusion`: `LocalPtr` referents are excluded and pointers are reset to null.
  - `TestExplicitExternalReferenceRefusal`: Objects pointing outside `EventGraphExportBoundary` are explicitly rejected at freeze.
  - `TestMalformedMissingReferenceRejectionWithoutReceiverWrites`: Corrupted payloads / missing referents fail validation with zero writes to receiver storage.
  - `TestReceiverSaveDestroyDomainLoad`: Receiver graph persists across domain destruction and reload.
  - `TestAddItemApplicationAndForcedReplay`: `AddItemEvent` application and subsequent `TryRebuildFromBaseAndReplay` preserve the identical imported `Item` instance without creating duplicates.
  - `TestScalarValueEqualToOldObjectIdNotRemapped`: Scalar fields with integer values matching sender `ObjId`s are preserved unchanged.
- Verified all passing:
  - `apptraverse_closed_event_graph_test`
  - `apptraverse_shared_node_incremental_event_test`
  - `apptraverse_shared_node_initial_sync_test`
  - `apptraverse_shared_node_foundation_test`
  - `apptraverse_event_sourced_core_test`
  - `apptraverse_dynamic_objects_add_test`
  - `apptraverse_journal_retention_test`
  - `apptraverse_model_runtime_stop_test`
  - `apptraverse_publication_channel_test`

---
Status: implemented/verified on main. Not accepted.

# CURSOR — Correct scratch replay and its tests

## Identity

- Scope: Correct scratch replay failure semantics, remove hardcoded scratch event ID, simplify Try* API, and rebuild class-chain tests with standalone wire format on `main`.
- Direct push to `origin/main`.

## What landed

- **Dynamic Scratch Event ObjId Allocation**: Replaced fixed `scratch_event_id{2}` in `PreflightHistoricalEventInsertion` with dynamic collision-free allocation via `AllocateUniqueStorageObjId(scratch_domain, scratch_storage)`. Eliminates collisions when `ObjId{2}` is used as a root SharedNode, Link, or other reachable object.
- **TryEnsureCurrentGeneration Failure Semantics**: Corrected cursor advancement in `TryEnsureCurrentGeneration` to advance `applied_journal_size_` only *after* successful `CanApplyTo` check and `ApplyEvent`. A rejected Event is never treated as applied on subsequent calls.
- **Try* API and Replay Unification**:
  - Removed dead/unreachable virtual methods `ReplayFromBaseImpl` and `InsertSharedImpl` in `Node` and `NodeFor`.
  - Unified on one underlying implementation per operation (`TryRebuildFromBaseAndReplay` for replay, `TryCommitSharedInto` for shared insert).
  - Production Node operations retain invariant assertion checks (`ReplayFromBase` and `InsertShared`). Speculative execution is explicitly designated for disposable scratch graphs only, which network admission discards without live mutation on failure.
- **Cleanup**:
  - Replaced redundant manual serialization loop in `FreezeNetworkSharedNodeState` with `SerializeRamDomainStorage(scratch)`.
  - Fixed strict weak ordering comparator in `ValidateStoredClassChains`: returns `false` when `a == b`.
  - Documented that `ValidateStoredClassChains` verifies the most-derived class is registered with create/load/save.

## Tests

- `TestMalformedClassLayersInEventRejected`: Rebuilt with actual standalone wire format (`ParseStandaloneEventPayload == true`, `ValidateStandaloneEventStorage == false`). Proved that disabling the guard causes `ae::Domain::KnownStoredClasses` assertion failure under LoadRoot, while with the guard it safely rejects without mutations or ACK.
- `TestOnlyBaseEventLayerRejected`: Standalone payload containing only base `Event` class layer while frame names `SetValueEvent::kClassId` is rejected by class-chain validation before LoadRoot.
- `TestDuplicateClassAndVersionEntriesRejectedByParser`: Verifies parser strictness on duplicate class IDs and duplicate versions for both standalone event payloads and full-graph NodeState payloads.
- `TestRepeatedFailedCheckingCannotSkipRejectedEvent`: Proves that a rejected Event is not skipped upon repeated calls to `TryEnsureCurrentGeneration`.
- `TestSharedNodeRootWithObjId2Succeeds` & `TestReachableObjectWithObjId2Succeeds`: Regression tests proving that `ObjId{2}` as a root SharedNode or reachable Link succeeds through initial sync, commit, and mid-journal incremental replication.
- `TestHistoricalCanApplyPreflight`: Expanded with a candidate valid at its own position but invalidating a later historical Event; asserted rejected preflight leaves live value, journal, generation, LinkSyncState, and storage untouched, with zero ACKs sent.
- `TestMalformedClassLayersInNodeStateRejected`: Explicitly proves parser succeeds and `ValidateStoredClassChains` fails before network delivery.

---
Status: implemented/verified on main. Not accepted.

# CURSOR — Incremental Event v1 hardening

## Identity

- Scope: Hardening of standalone incremental Event transport and initial NodeState admission on `main`.
- Pre-LoadRoot class chain validation, safe historical CanApply preflight, atomic snapshot freeze coverage, and receiver-local ObjId collision prevention.

## What landed

- **Pre-LoadRoot Class Chain Validation**: Implemented `ValidateStoredClassChains` and `ValidateStandaloneEventStorage` to audit class layers in `RamDomainStorage` prior to `scratch_graph.LoadRoot`. Verifies that all objects have valid IDs, non-empty supported class layers registered in `Registry`, coherent inheritance chains with no unrelated pairs, and that the most-derived class is an instantiable leaf class. Detects and rejects duplicate objects, duplicate class layers, duplicate versions, unknown class layers, and malformed class hierarchies without tripping `DomainGraph` assertions.
- **NodeState Structural Validation**: Wired `ValidateStoredClassChains` into `ImportValidatedNode` before `scratch_graph.LoadRoot(target_node_id)`, ensuring untrusted NodeState payloads cannot trigger assertions in `DomainGraph`.
- **Historical CanApply Preflight**: Introduced `Try` methods (`TryEnsureCurrentGeneration`, `TryReplayJournal`, `TryRebuildFromBaseAndReplay`, `TryInsertEvent`, `TryCommitSharedInto`, `TryReplayFromBase`, `TryInsertShared`) across `Node` and `NodeFor` returning `bool` instead of asserting on `CanApplyTo` failures.
- **Mid-Journal Historical Replay Safety**:
  - Incremental Event admission uses `PreflightHistoricalEventInsertion` to test Event application in a scratch copy of the target `SharedNode` graph. The Event is admitted only if insertion and entire historical replay from base succeed.
  - NodeState journal admission validates by replaying the candidate journal in scratch from base in timestamp order via `candidate.TryReplayFromBase()`.
- **Atomic Snapshot Freeze Coverage**: Refactored snapshot freeze into `FreezeNetworkSharedNodeState`, returning `FrozenNodeState { payload, covered_event_ids }` derived in one operation from the identical scratch state/traversal, eliminating snapshot-vs-coverage race conditions.
- **Receiver-Local Event ObjId Collision Prevention**: Implemented `AllocateUniqueStorageObjId` which checks both live Domain and underlying storage before picking a fresh local `ObjId` for imported Events.

## Tests

- `apptraverse_shared_node_incremental_event_test`: Added tests for malformed class layers in Event payload, historical `CanApply` preflight (event valid historically but invalid on current state, and event valid on current state but invalid historically), and verified all 18 incremental event scenarios.
- `apptraverse_shared_node_initial_sync_test`: Added test for malformed class layers in NodeState payload, and verified all 15 initial sync scenarios.

---
Status: implemented/verified on main. Not accepted.

# CURSOR — SharedNode incremental standalone Event sync v1

## Identity

- Fast-forwarded `main` from `5b1bd52` to `8c609a9` (completed initial-sync
  work) and continued on `main`. No feature branch. No PR.
- Scope: first incremental Event transport for an already-Complete Share.
  Standalone scalar Events only. One pending Event packet per Share.

## Model kept

- `SharedEventOrder` is `timestamp_us` only. No Lamport, no tie-break.
- `SharedEventId` is the only cross-replica logical identity.
- `Event::ObjId` is replica-local. The receiver creates a fresh Event object
  and never writes the sender Event ObjId as a storage key.

## What landed

- `Event::TargetClassId()` / `EventFor` returns `Target::kClassId`. An
  untrusted Event is checked through `Registry::GenerationDistance` against
  the live Node before `CanApplyTo` / `ApplyTo` would `static_cast`.
- `Node::InsertShared` / `FindSharedEvent` dispatch through `NodeFor` so
  `SharedSyncRuntime` never names a concrete business Node.
- Standalone Event payload: freeze the network-shared graph, refuse if it is
  not exactly one object, serialize class/version layers without making the
  sender ObjId authoritative. Receiver parses in scratch, validates, then
  `GenerateUnique` + load into production storage.
- Protocol v1 `EventFrame`: packet / node / share ids, `SharedEventId`,
  `timestamp_us`, `event_class_id`, payload. Strict canonical decode. Ack
  stays the generic `AckFrame`.
- `LinkSyncState` v2: `pending_initial_covered_event_ids` captured at
  NodeState freeze and moved into `delivered_event_ids` on initial ACK;
  `delivered_event_ids` plus one `pending_event_*` packet for incremental
  send. All mutations are Events. v1 layout is rejected at load.
- `SyncNextEvent`: no-op until initial Complete; exact retry of a pending
  packet; otherwise freeze/persist/send the first undelivered shared journal
  Event. No timers.

## Tests

`apptraverse_shared_node_incremental_event_test` covers the required
scenarios: normal A→B scalar Event, same `SharedEventId` / different Event
ObjIds (B occupies the sender ObjId first), timestamp preserved, mid-journal
100/200/300 replay, persist-before-send, lost ACK exact retry, duplicate no
reapply, sender/receiver restart, initial snapshot E1-vs-E2 coverage race,
wrong source, ReadOnly source, wrong destination, malformed payload, wrong
target Event class, conflicting duplicate identity, non-standalone Event
graph refused, and strict Event frame decoding.

## Still open

- Events carrying new object graphs / child Nodes
- topology Event replication
- multi-hop / 3-replica fanout
- presence, Æther, chat

---
Status: implemented/verified on feature branch. Not accepted.

# CURSOR — Legacy SharedInstance / SharedRuntime sync engine removed

## Identity

- Starting SHA: `599aaf7b68a03cdcf239c3e98a3f8fa28004b5e7`
- Branch: `cursor/sharednode-initial-state-sync-v1-9239`
- PR: https://github.com/apptraverse/apptraverse/pull/3 (draft, not merged)
- Scope: deletion only. No incremental Event replication, no presence, no
  change to the initial NodeState/ACK protocol or to the timestamp-only order
  model.

## Why

The tree carried two sharing architectures. The older one
(`SharedInstance<TNode>` + `SharedRuntime` over `ISharedTransport`) predates
SharedNode and cannot be extended into incremental Event replication without
duplicating delivery state that `LinkSyncState` already owns. It is gone rather
than deprecated: no aliases, no compatibility wrappers.

## Removed

Production:

- `include/apptraverse/shared_instance.h` — `SharedInstance<TNode>`,
  `PeerDeliveryState`, `PeerInFlightEntry`, `DeferredIncomingEvent`,
  `SharedWriteState`, `shared_room_id`, `peers[]`, `pending[]`, `in_flight[]`,
  `channel_ready`.
- `include/apptraverse/shared_runtime.h`, `src/shared_runtime.cpp` —
  `SharedRuntime` with `kSharedEventPipelineWindow`,
  `kSharedEventRetryInterval`, per-peer `Tick` scheduling, ACK bookkeeping,
  deferred incoming Events, local identity/order assignment.
- `include/apptraverse/shared_transport.h` — `ISharedTransport::SendEvent` /
  `SendAck`, `SharedEventFrame`, `SharedAckFrame`, room-and-peer addressing.
- `include/apptraverse/shared_frame_codec.h`, `src/shared_frame_codec.cpp` —
  the codec for those two frames only.
- `examples/chat_ui_runtime_demo/common/chat_shared.{h,cpp}` — the chat binding
  over that runtime, including its shared-Event payload serialization, remap
  and runtime-field stripping.
- `examples/chat_ui_runtime_demo/windows/aether_shared_transport.h` — the Æther
  Win32 adapter implementing `ISharedTransport`.
- `SharedEventIdLess` — no caller remained; dedup compares identities for
  equality.

Tests:

- `tests/shared_journal_test.cpp` — deleted. It only drove the deleted engine
  through a fake peer bridge. Its generic claims already live elsewhere:
  timestamp-only ordering and mid-journal replay in
  `apptraverse_event_sourced_core_test`, retention in
  `apptraverse_journal_retention_test`, packet duplicate/lost-ACK/restart
  behavior in `apptraverse_shared_node_initial_sync_test`. What it also covered
  — peer queues, in-flight windows, retry intervals, deferred remote Events —
  is behavior of the deleted architecture and is not preserved.
- `tests/chat_p2p_headless_test.cpp` — deleted, with its CMake target
  `apptraverse_chat_p2p_headless_test`. It was a Win32 Æther P2P convergence
  test built on `ISharedTransport` and the chat binding. Note that the
  repository chat-test instructions still name it; real P2P convergence
  coverage returns when chat is rebuilt on SharedNode (milestones 17–21).
- `apptraverse_chat_headless_check` now runs event-sourced core, journal
  retention, and chat presentation headless.
- `tests/chat_presentation_headless_test.cpp` — three scenarios deleted:
  `TestOfflineRetrySkippedWhileChannelDown` (peer retry over `channel_ready`),
  `TestTimestampCommitAndRemap` and `TestIncomingSharedCannotImportPresence`
  (incoming shared-Event payload remap and the runtime-field stripping guard,
  both of which lived in the deleted binding). The presence scenarios stay and
  now use `CommitPresenceChanged` and `ChatPresenceOverlay::ApplyToRoom`
  directly, which is what the binding wrapped.
- `tests/chat_ui_runtime_test.cpp` — `TestConnectToHostCommandRegistersPeer`
  deleted (peer registration and `shared_room_id`). The presence-overlay
  scenario stays, rewritten against the overlay.
- `tests/chat_ui_mirror_integration_test.cpp` — stale `chat_shared.h` include
  dropped; it never used the binding.

The presence guard that `TestIncomingSharedCannotImportPresence` protected —
an incoming shared Event must not import a peer's Presence — has no code left
to protect. It has to be re-established when chat Events cross the new
transport.

## Kept

- `SharedEventId` (identity and equality), `SharedEventOrder` (timestamp only),
  `EventRecord`, and the Node shared-event insertion machinery
  (`Node::CommitSharedInto`, `NodeFor::CommitShared`,
  `NodeFor::InsertSharedOrderedEvent`). Untouched by this change.
- `SharedSyncRuntime`, `NodeState` / `Ack` frames, `IByteTransport`,
  `MemoryTransport` / `MemoryNetwork`, `LinkSyncState`, and the initial-sync
  hardening from PR #3 (strict frame decoding, source-endpoint binding, scratch
  admission before any write to real storage).
- `Node::CommitInto` already carried the local strictly-increasing timestamp
  adjustment, so deleting `SharedRuntime::MakeLocalOrder` lost no behavior.

## Added

`apptraverse_event_sourced_core_test` gains
`TestSharedEventIdentityRoundTrip`: two Events committed through the public
`CommitShared` path with distinct identities, saved, reloaded, and checked for
identity, order, and materialized state, including the "same origin, next
sequence is a different Event" lookup. It replaces the deleted engine's dedup
test with the property that actually makes dedup possible — identity is
journal state that survives a restart.

## Architecture after cleanup

- One generic sharing runtime: `SharedSyncRuntime`.
- One persistent per-relationship sync state: `LinkSyncState`, keyed by
  `share_id`. No second pending/in-flight/retry structure exists.
- One transport abstraction: `IByteTransport` with `MemoryTransport` /
  `MemoryNetwork`, carrying opaque bytes.
- Repository search for `SharedInstance`, `PeerDeliveryState`,
  `PeerInFlightEntry`, `DeferredIncomingEvent`, `SharedWriteState`,
  `kSharedEventPipelineWindow`, `kSharedEventRetryInterval`, `shared_room_id`,
  `channel_ready`, `ISharedTransport`, `SharedEventFrame`, `SharedAckFrame`,
  `shared_transport`, `shared_frame_codec` returns nothing outside this
  document. `SharedRuntime` matches only `SharedSyncRuntime`.

## Verified

- Required set, all pass: `apptraverse_shared_node_initial_sync_test`,
  `apptraverse_shared_node_foundation_test`,
  `apptraverse_event_sourced_core_test`, `apptraverse_dynamic_objects_add_test`,
  `apptraverse_journal_retention_test`, `apptraverse_model_runtime_stop_test`,
  `apptraverse_publication_channel_test`.
- Remaining chat/model tests pass: `apptraverse_chat_presentation_headless_test`,
  `apptraverse_chat_ui_runtime_test`,
  `apptraverse_chat_ui_mirror_integration_test`.
- Incremental build only; the build tree was reused, not wiped. Two unrelated
  build-tree frictions were worked around without deleting anything: each
  configure re-runs the dependency patch steps and `patch --forward` exits 1 on
  an already-patched tree, so the dependency patches are reversed immediately
  before configuring; and `examples/aether_presence_monitor` includes
  `windows.h`, so it cannot build on Linux with
  `APPTRAVERSE_BUILD_AETHER_DEMOS=ON` — pre-existing, unrelated to this change,
  so the required targets were built by name.
- `apptraverse_model_ui_runtime_test` still fails at `TestInitialGraphCopy` on
  Linux and `apptraverse_surfaces_linux_smoke_test` is the known GTK flake.
  Both pre-date this change.

## Known limitations

- Incremental Event replication is not implemented. No `EventFrame`, no
  `EventAck`, no pending Event bytes, no retry, no forwarding, no presence.
  Milestone 08 extends `LinkSyncState` and `IByteTransport`; nothing from the
  deleted engine returns.
- Chat has no sharing path at all right now. The chat demo is local-only until
  it is rebuilt on SharedNode.
- Equal `timestamp_us` still has no defined order between replicas. Unchanged
  and deliberate.

---
Status: implemented/verified on feature branch. Not accepted.

# CURSOR — Shared Event order simplified to timestamp only

## Identity

- Starting SHA: `2c176a46f4b8a278cf6db0d097d99b5bdff5046b`
- Branch: `cursor/sharednode-initial-state-sync-v1-9239`
- PR: https://github.com/apptraverse/apptraverse/pull/3 (draft, not merged)
- Scope: the shared Event order model and the persisted journal shape it
  implies. No incremental Event transport, no presence, no change to the
  initial NodeState/ACK protocol beyond what the new EventRecord layout forced.

## The model now

- Shared Event **order**: `SharedEventOrder{ timestamp_us }`. One field, one
  comparison. `SharedEventOrderLess` is `a.timestamp_us < b.timestamp_us` and
  `EventRecordOrderLess` forwards to it.
- Shared Event **identity**: `SharedEventId{ origin_uid, origin_sequence }`,
  unchanged and separate. It recognizes the same logical Event and is what
  deduplication uses. It does not participate in ordering.
- `EventRecord` keeps the three concepts apart explicitly: `event`, `identity`,
  `order`, `retained_since_us`. Only `order.timestamp_us` decides position.
- What was removed: `SharedEventOrder` no longer carries `lamport`,
  `origin_uid`, or `origin_sequence`, and nothing compares that tuple any more.
  The legacy runtime's `lamport_clock` is gone; a remote Event no longer
  advances any local counter and keeps the `timestamp_us` it was sent with.

## Local timestamps

`Node::CommitInto` keeps a replica's own consecutive commits strictly
increasing: if the wall clock has not moved, the next commit takes the previous
timestamp plus one. It is a wall-clock adjustment over one local sequence, not
a logical clock. (The legacy runtime had a second copy of this in
`SharedRuntime::MakeLocalOrder` over `SharedInstance::last_local_timestamp_us`;
that runtime is deleted in the section above.)

## Equal timestamps: deliberately unresolved

Two different Events may carry the same `timestamp_us`. The insert-time
assertion that rejected equal orders is gone; the duplicate-`SharedEventId`
assertion stays. No tie-break replaced it. Where equal-timestamp records land
relative to each other is `std::lower_bound` behavior on that replica, written
down as such at the insertion site, and replicas are not claimed to converge
for that case.

## Persisted state

Measured, not assumed. A `SharedValueNode` with three journal Events was
written by `2c176a4` and read back by this revision: without a guard it loaded
"successfully" into a journal of three records of which two had dead Event
references and a materialized value of 0 instead of 21. A version bump on
`Node` does not fix that — aether-objects writes a Node's fields into the
storage layer of the *most derived* class, so `Node`'s version is not part of
the storage key of any concrete Node, and only the derived class's version is.

So the journal format is stated in the payload: `kNodeJournalFormat` is written
at the head of every Node payload and checked before the journal is decoded.
Old development state now fails at load with "AppTraverse Node journal predates
timestamp-only Event order; re-distill with a fresh state dir". Nothing is
migrated and nothing is rewritten during Load. `Node` is version 3, and its v1
and v2 loaders throw for the same reason.

This supersedes the earlier note (journal retention section below) that Node
`Load(Version<1>)` migrates v1 journals by stamping load time: that migration
and its `EventRecordWireV1` layout are deleted, together with the
`LegacyRetentionDoc` fixture and `TestNodeV1MigrationStampsRetainedSince` that
covered it.

## Tests

- `apptraverse_event_sourced_core_test`: two new scenarios.
  `TestOrderIgnoresIdentity` pairs timestamp 100/identity `z`/99 against
  timestamp 200/identity `a`/1 and requires the timestamps to decide, then
  takes two records with equal timestamps and different identities and requires
  the comparator to answer false in both directions while identity still
  reports them as different Events.
  `TestMidJournalRemoteEventReplaysByTimestamp` commits at 100 and 300 and then
  inserts at 200 from origin `z-origin`, which sorts last: the materialized
  label is `b123` rather than the `b132` an append would produce, so the whole
  journal was replayed in timestamp order and origin had no influence.
- `apptraverse_shared_journal_test`: the simultaneous-commit test asserted that
  equal lamport clocks converge through a lexicographic `origin_uid` tie-break.
  That claim no longer exists, so the test became
  `test_cross_replica_order_follows_timestamp`. (The whole file is deleted with
  the legacy runtime in the section above; the equivalent claim without that
  runtime is `TestMidJournalRemoteEventReplaysByTimestamp` in
  `apptraverse_event_sourced_core_test`.)
- Order literals across `journal_retention_test`, `shared_node_foundation_test`,
  and `event_sourced_core_test` are timestamps now; none of them construct an
  order out of identity fields.

## Verified

- Required set, all pass: `apptraverse_shared_node_initial_sync_test`,
  `apptraverse_shared_node_foundation_test`, `apptraverse_event_sourced_core_test`,
  `apptraverse_dynamic_objects_add_test`, `apptraverse_journal_retention_test`,
  `apptraverse_model_runtime_stop_test`, `apptraverse_publication_channel_test`.
- `apptraverse_chat_headless_check` (event-sourced core, shared journal,
  journal retention, chat presentation headless) passes, as does
  `apptraverse_chat_ui_mirror_integration_test`.
- Full `ctest` in this tree: everything passes except
  `apptraverse_surfaces_linux_smoke_test` (known GTK flake, out of scope).
  `apptraverse_model_ui_runtime_test` fails at `TestInitialGraphCopy`, and it
  fails identically when built from `2c176a4`: pre-existing on Linux, not
  caused by this change.
- Incremental builds only. The build tree was reconfigured with
  `APPTRAVERSE_BUILD_AETHER_DEMOS=ON` (the repository default) so the chat
  headless targets exist; nothing was wiped.

## Known limitations

- Equal timestamps have no defined order between replicas. Open by decision.
- No incremental Event transport and no presence. An Event frame carrying
  identity, `timestamp_us`, and payload is still milestone 08.
- `Node`'s journal format constant is a stand-in for per-class journal
  versioning. Real versioning would mean bumping every Node subclass.

---
Status: implemented/verified on feature branch. Not accepted.

# CURSOR — Initial sync v1 protocol hardening

## Identity

- Starting SHA: `7e9a60d7177d0ea92899da43cbc41a60bcb0c9ab`
- Commits: `8eec568` (frame decoding + payload import), `1f5ef34` (source
  endpoint binding + scratch admission), docs commit on top
- Branch: `cursor/sharednode-initial-state-sync-v1-9239`
- PR: https://github.com/apptraverse/apptraverse/pull/3 (draft, not merged)
- Scope: correctness of the existing initial-state protocol against untrusted
  bytes. No incremental Events, no presence, no chat, no protocol redesign.

## What changed

- ACKs are bound to the sender. `OnBytes` now passes `source_endpoint` into
  `OnAck`, which resolves the destination Share's Link endpoint and requires
  the bytes to have arrived from it before `CompleteInitialSync`. Previously
  any endpoint that knew the packet / node / share ids could complete a pending
  synchronization.
- NodeState is admitted before it is stored. An expected unknown root is parsed
  into an `ae::RamDomainStorage` and loaded in a scratch `ae::Domain`, where
  the runtime checks the root's class (`Registry::GenerationDistance`, no
  RTTI), that base and every journal Event loaded and can apply, that every
  Share carries a valid and unique `share_id` with a resolvable Link endpoint,
  that the destination Share ends at this replica, and that the source endpoint
  is a Link of the same topology. Only then is the parsed graph committed to
  real storage, and the Node is pushed into `nodes_` only after the import is
  durable. The scratch Domain and candidate are destroyed before any of that;
  nothing process-global is involved.
- The same endpoint rules now gate a NodeState for an already known Node, so a
  replay of an applied packet from the wrong endpoint is not acknowledged.
- `ImportObjectGraphPayload` is parse-then-commit: `ParseObjectGraphPayload`
  fills an intermediate RAM storage and `CommitObjectGraph` writes it out, so
  malformed input performs zero writes on the target. `CopyNetworkSharedObjectGraph`
  reuses `CommitObjectGraph` instead of its own transfer loop.
- Frame decoding is canonical: a NodeState's declared payload must end exactly
  at the end of the frame, an Ack must end after its three ids, and a zero
  `ObjId` is rejected for `packet_id`, `target_node_id`, and
  `destination_share_id` (each one names something the receiver must resolve).
  `ParseObjectGraphPayload` likewise rejects a zero object id.

## Coverage (`apptraverse_shared_node_initial_sync_test`, 14 scenarios)

Six scenarios added to the eight already there:

- ACK from the wrong endpoint: A is Pending toward B, with C also a participant
  of the topology. C sends a byte-valid ACK carrying the exact packet, node,
  and share ids — A stays Pending with unchanged pending bytes and writes
  nothing. The same bytes from B complete the sync.
- Wrong-source NodeState: the frozen packet delivered from non-participant C
  leaves `FindNode` invalid, B's storage empty for the Node and both Links, no
  write, and no ACK to anyone. The same packet from A is then accepted, and
  replaying it from C afterwards is still not acknowledged and rewrites nothing.
- Wrong-destination NodeState: a hand-built frame from A naming A's own
  relationship is rejected by B before any storage mutation.
- Malformed NodeState for an expected target: a truncated payload, and a
  payload that parses but whose root is a Link rather than a SharedNode, both
  leave B with no Node, no storage entry, no write, and no ACK. Without the
  class check the second case crashes the receiver, which is what the guard is
  for.
- Malformed payload: a `CountingStorage` sees zero `Store` calls for a
  truncated, a head-only, and a trailing-byte payload, and non-zero for the
  intact one.
- Strict decoding: round trip, trailing byte, truncation, and zero id for each
  required field, cross-type decode, and `PeekSyncFrameType` on junk.

Each new guard was mutation-checked: reverting it one at a time (ACK source,
NodeState canonical length, Ack canonical length, zero-id rejection,
parse-before-commit, source-in-topology, destination-is-local, root class)
makes the suite fail, and every mutation was reverted afterwards.

## Tests run

- `apptraverse_shared_node_initial_sync_test` PASS
- `apptraverse_shared_node_foundation_test` PASS
- `apptraverse_event_sourced_core_test` PASS
- `apptraverse_dynamic_objects_add_test` PASS
- `apptraverse_journal_retention_test` PASS
- `apptraverse_model_runtime_stop_test` PASS
- `apptraverse_publication_channel_test` PASS

Incremental builds only; the flaky headless-GTK surfaces smoke test was not
re-run, as agreed for this slice.

## Limitations

- Endpoint identity is whatever the transport reports. No signatures, MAC,
  certificates, or trust store: authenticating the endpoint is the Æther
  transport integration, not this runtime.
- Admission validates snapshot structure and identity, not the semantics of the
  imported history. A structurally valid but hostile journal can still reach
  replay; journal admission belongs with incremental Event sync.
- Storage write failure and torn writes remain out of scope. The guarantee is
  invalid input → no target writes, not crash-atomic storage.
- No incremental Events, no presence: unchanged from the previous slice.
- Not accepted-by-user

---
Status: implemented/verified on feature branch. Not accepted.

# CLOUD CURSOR — SharedNode initial state sync v1

## Identity

- Starting SHA: `144edb738c0731ab986594295a05bf6cbe27737d`
- Commits: `66e168b` (transport + frames), `d559721` (packet + ACK durability),
  test/docs commit on top
- Branch: `feature/shared-node-foundation-v1`
- PR: https://github.com/apptraverse/apptraverse/pull/2 (draft, not merged)
- Scope: initial-state synchronization between two independent replicas over
  bytes only. No incremental Event replication, no presence, no chat.

## What was built

- `IByteTransport` + `MemoryNetwork` / `MemoryTransport`: opaque bytes between
  endpoint uids, nothing about SharedNode / Event / ACK / access. Packets queue
  per direction and move only on `DeliverNext` / `DropNext` / `DuplicateNext`;
  `Disconnect` / `Reconnect` are directional. No threads, timers, or sleeps.
  Queues outlive endpoints, so a packet stays in flight across a restart.
- Protocol v1 frames: `NodeState{packet_id, target_node_id,
  destination_share_id, payload}` and `Ack{packet_id, target_node_id,
  destination_share_id}`. Routing is by `target_node_id`, never by source
  endpoint. Decoders return false on malformed bytes.
- `SerializeNetworkSharedObjectGraph` / `ImportObjectGraphPayload`: the same
  `GraphSerializationScope::NetworkShared` walk as the existing graph copy,
  as bytes, with object / class / version identities preserved. No
  `source.Save()` side effect.
- `LinkSyncState` gains `pending_initial_packet_id` +
  `pending_initial_packet` (sender) and `received_initial_packet_id`
  (receiver). All three transitions go through Events
  (`BeginInitialSyncEvent`, `CompleteInitialSyncEvent`,
  `NoteInitialSyncReceivedEvent`); no reflected field is written after the Node
  is live.
- Packet identity is the `BeginInitialSyncEvent` ObjId — the same facility
  `Share::share_id` already uses, so no new UUID system. It is known before
  Commit, which is what lets the frozen frame embed it.
- `SharedSyncRuntime`: one per replica, holding its Domain, storage, and
  transport. Sender order is freeze → persist → send; `Pending` resends the
  persisted bytes verbatim; `Complete` sends nothing.
- Receiver order is decode → bootstrap check → import → replay → persist →
  ACK. A root is created only for a `target_node_id` the replica explicitly
  expects.
- `Link::EndpointUid()` gives the transport address of a descriptor, so
  locality stays runtime-relative and nothing persists `is_local`.
- `Node::ReplayFromBase()` became a virtual entry point (implemented by
  `NodeFor`), so replay works on a base `SharedNode::ptr` without RTTI.
- `StashLocalPersistentAcrossRebuild` now carries only entries that hold state.
  An imported SharedNode has one empty local slot per Share; replay of the
  imported shared journal then creates the receiver's own `LinkSyncState` per
  relationship, keyed by the `share_id` that travelled with the topology.

## Coverage (`apptraverse_shared_node_initial_sync_test`, 8 scenarios)

- Two replicas, separate storage / Domain / transport, bytes only.
- Late attach: A already has business history and both Link descriptors.
- Imported graph: Node identity, Link identities and endpoints, Share topology,
  and every `Share::share_id` equal on both sides.
- Sender-local delivery state absent from the receiver's storage.
- Receiver-local `LinkSyncState` created by replay, distinct ObjIds, same
  `share_id`.
- Durability ordering proved by a storage wrapper that records the peer queue
  depth at every write: both sides write only while that queue is empty.
- Normal ACK, lost ACK with byte-identical retry, duplicate acknowledged with
  no write and no re-apply, sender restart while Pending, receiver restart
  after apply, sender restart after Complete.
- Mutation after freeze does not change the retry bytes, and the receiver stays
  at the frozen state.
- Routing: two SharedNodes over the same Link, a packet for one does not create
  or touch the other.
- Unexpected `target_node_id` creates nothing and is not acknowledged.
- Distinct C++ instances for Node and Link on the two replicas.

## Tests run

- `apptraverse_shared_node_initial_sync_test` PASS
- `apptraverse_shared_node_foundation_test` PASS
- `apptraverse_event_sourced_core_test` PASS
- `apptraverse_dynamic_objects_add_test` PASS
- `apptraverse_journal_retention_test` PASS
- `apptraverse_model_runtime_stop_test` PASS
- `apptraverse_publication_channel_test` PASS
- full Linux ctest suite (15 tests) PASS

`apptraverse_surfaces_linux_smoke_test` is flaky in this headless GTK
environment: `CHECK failed: is_active` in roughly two runs out of five, pass or
fail with the same binary. Untouched by this slice (no surfaces, GTK, or
presenter code changed) and not in the required set.

## Limitations

- No incremental Event replication: after the initial snapshot the receiver
  stays at that state by design.
- No heartbeat / presence / Online-Offline; transport connection toggles are
  deterministic test controls only.
- A second, different initial snapshot for an already imported relationship is
  rejected rather than applied.
- The payload reuses the storage encoding of each object layer and is not a
  portable wire format; fine for an in-process memory transport.
- Storage I/O failure and torn writes remain out of scope; only the logical
  durability ordering is enforced.
- aether-objects `DomainGraph` serialization-scope patch untouched in this
  slice. The dependency source tree had to be restored (`git checkout`/`clean`
  in `_deps/aether-objects-src`) before configure would re-apply it; build tree
  itself was never wiped.
- Not accepted-by-user

---
Status: implemented/verified on feature branch. Not accepted.

# CLOUD CURSOR — Share relationship identity / replay correctness

## Identity

- Starting SHA: `e8542b6111ac2ec24ae07fc40bd969b17e2577d5`
- Final SHA: `099d659` (fix) — this Progress entry commits on top
- Branch: `feature/shared-node-foundation-v1`
- PR: https://github.com/apptraverse/apptraverse/pull/2 (draft, not merged)
- Scope: stable Share relationship identity so local sync survives a real
  `RebuildFromBaseAndReplay`. No transport / packets / ACK / presence / chat.

## Regression proved before the fix

New `TestShareRelationshipIdentitySurvivesForcedReplay` inserts an older
business Event ahead of the journal head, which forces
`Node::RebuildFromBaseAndReplay` (not Save/Load). On `e8542b6` it failed twice:

- `CHECK failed: node->link_sync_states[0].id() == second_sync_id`
- `CHECK failed: node->GetInitialSyncPhase(link) == InitialSyncPhase::Complete`

Cause: the stash restored the second relationship's `LinkSyncState`, then
replay of the historical `AddShare(B)` matched it by link id, and the
historical `RemoveShare(B)` erased it; the second `AddShare(B)` then built a
fresh NotStarted state. The `SetLinkInitialSyncPhaseEvent` lives in the erased
Node's own journal, so Complete was gone.

## Fix

- `Share::share_id` / `LinkSyncState::share_id` = `ObjId` of the
  `AddShareEvent` that opened the relationship. No new identity infrastructure:
  Event identity already survives replay, Save/Load, and graph copy.
- `RemoveShareEvent` / `ChangeShareAccessEvent` carry `share_id`, not `link`.
- `FindLinkSyncIndex(link_id)` replaced by `FindLinkSyncIndexForShare(share_id)`
  plus `FindShareIndexForShare`; `SetInitialSyncPhase` / `GetInitialSyncPhase`
  resolve Link → active Share → relationship state.
- `Apply(AddShareEvent)` reuses a restored state with the same `share_id`,
  otherwise creates one with `share_id` + `link` + NotStarted assigned before
  `InitializeRuntimeNode`.
- Stash / restore unchanged; no `AfterReplayFixSyncState`, reconcile scan, or
  test flag.

## Coverage added

- Forced mid-journal rebuild: second relationship identity, second sync-state
  identity, and Complete all preserved; no duplicate sync state for the Link.
- Same test then Saves, reloads in a new Domain, and forces a second rebuild
  there — relationship identity is re-derived from the persisted Event.
- Live remove + re-add: new `share_id`, new sync state, NotStarted.
- Network copy: sender and receiver `share_id` equal; receiver has no
  `LinkSyncState` and reads NotStarted.
- One Link shared by two SharedNodes: different `share_id`, independent sync
  progress across restart.

## Tests run

- `apptraverse_shared_node_foundation_test` PASS
- `apptraverse_event_sourced_core_test` PASS
- `apptraverse_dynamic_objects_add_test` PASS
- `apptraverse_journal_retention_test` PASS
- `apptraverse_model_runtime_stop_test` PASS
- `apptraverse_publication_channel_test` PASS
- full Linux ctest suite (14 tests) PASS

## Limitations

- No Memory transport / initial sync / ACK / retry / presence / chat
- `share_id` is the local `AddShareEvent` ObjId; a transport that re-creates the
  Event on the receiver must carry the identity in the frame (recorded in
  `plan.md` open questions)
- aether-objects `DomainGraph` serialization-scope patch untouched in this slice
- Not accepted-by-user

---
Status: implemented/verified on feature branch. Not accepted.

# CLOUD CURSOR — SharedNode foundation v1.1 follow-up (policy + init)

## Identity

- Starting SHA: `62750540681ddd6755bca9059b53958428c8dfb4`
- Final SHA: `7563b80f29195c73a4b55961a2f0eb1a7308b846`
- Branch: `feature/shared-node-foundation-v1`
- PR: https://github.com/apptraverse/apptraverse/pull/2
- Scope: remove process-global GraphCopyPolicy registry; put serialization
  scope on DomainGraph; fix LinkSyncState field init before live. No transport.

## Corrections

- `ae::GraphSerializationScope` + `DomainGraph::serialization_scope` (aether-objects
  patch). Network export: `DomainGraph{&domain, NetworkShared}`. No static map /
  thread_local / singleton policy registry.
- `LinkSyncState`: assign `link` + NotStarted, then `InitializeRuntimeNode`.
- Fixtures set reflected Node fields before `InitializeRuntimeNode`.
- Concurrent DomainGraph LocalPersistent vs NetworkShared test (two threads).

## Tests run

- `apptraverse_shared_node_foundation_test` PASS
- `apptraverse_event_sourced_core_test` PASS
- `apptraverse_dynamic_objects_add_test` PASS
- `apptraverse_journal_retention_test` PASS
- `apptraverse_model_runtime_stop_test` PASS
- `apptraverse_publication_channel_test` PASS

## Limitations

- No Memory transport / initial sync / ACK / retry / presence / chat
- Not accepted-by-user

---
Status: implemented/verified on feature branch. Not accepted.

# CLOUD CURSOR — SharedNode foundation v1.1 hardening

## Identity

- Starting SHA: `3dbfe688e4a4b68bc1b00cbfda2838525c4cd0f7`
- Final SHA: `e02a150acfbe5c674fd16645e19191a9b4c4a66b`
- Branch: `feature/shared-node-foundation-v1`
- PR: https://github.com/apptraverse/apptraverse/pull/2
- Scope: foundation corrections only — Event-driven local sync,
  generic LocalPtr network exclusion, read-only network snapshot,
  RemoveShare+AddShare sync reset. No transport / ACK / presence / chat.

## Corrections

- Local-persistent sync state obeys Event-only mutation (`LinkSyncState` is a
  Node; phase via `SetLinkInitialSyncPhaseEvent`; AddShare creates NotStarted;
  RemoveShare drops stale sync)
- Generic `LocalPtr` network exclusion via DomainGraph serialization scope
  (empty/default ObjPtr on wire; referent not exported; nested SharedNode and
  non-SharedNode fixtures)
- `CopySharedNetworkGraph` / `CopyNetworkSharedObjectGraph` do not
  `source.Save()` / Store to source
- Link persistent config initialized before `InitializeRuntimeNode`
- Removed `ClearLocalPersistentEdges` / direct `EnsureLinkSyncState` Save paths

## Tests run

- `apptraverse_shared_node_foundation_test` PASS
- `apptraverse_event_sourced_core_test` PASS
- `apptraverse_dynamic_objects_add_test` PASS
- `apptraverse_journal_retention_test` PASS
- `apptraverse_model_runtime_stop_test` PASS
- `apptraverse_publication_channel_test` PASS

## Limitations

- No Memory transport / initial sync / ACK / retry / presence / chat
- Per-Link sync stores only `InitialSyncPhase` (no pending bytes yet)
- Not accepted-by-user

---
Status: implemented/verified on feature branch. Not accepted.

# WINDOWS CURSOR — SharedNode foundation v1

## Identity

- Starting main SHA: `45aa9db5b7bd8ca1685fcb6cdc57afa57471a4d1`
- Branch: `feature/shared-node-foundation-v1`
- Scope: persistent Link / SharedNode topology / LocalPtr sync metadata /
  network-serialization boundary tests. No transport, ACK, or chat.

## Implemented

- `Link` + `MemoryLink` persistent descriptors (no `is_local`)
- `SharedNode` with Event-only `shares[]` (RW/RO stored, not enforced)
- `LocalPtr` / `SharedPtr` scoped edges; `LinkSyncState` local-persistent
- `CopySharedNetworkGraph` includes Links, excludes local sync
- Rebuild stash so shared Event replay does not roll back local sync
- Headless fixtures under `examples/shared_node_demo/`
- `apptraverse_shared_node_foundation_test`

## Tests run

- `apptraverse_shared_node_foundation_test` PASS
- `apptraverse_event_sourced_core_test` PASS
- `apptraverse_dynamic_objects_add_test` PASS
- `apptraverse_journal_retention_test` PASS
- `apptraverse_model_runtime_stop_test` PASS
- `apptraverse_publication_channel_test` PASS

## Limitations

- No Memory transport / initial sync / ACK / retry / presence / chat
- Per-Link sync stores only `InitialSyncPhase` (no pending bytes yet)
- Not accepted-by-user

---
Status: documentation-only. Not accepted.

# WINDOWS CURSOR — surfaces frozen / land to main

## Identity

- Starting surfaces-demo SHA: `677a5f7adc5e401fb796a492f0fe7abe588a41b4`
- Starting main SHA: `de799bdc9aa308195d6dc83685b092856d5cb918`
- Branch path: `integration/finalize-surfaces` → merge into `main`
- Files changed for docs finalization: `plan.md`, `Progress.md` only

## Source-verified platform adaptive status

- Windows / Android / WASM / Linux GTK3 / macOS SwiftUI / iOS SwiftUI+rotation:
  adaptive presentation-size Event path present under `677a5f7`.
- `surfaces_demo` marked **COMPLETE / FROZEN**.
- SharedNode / Link roadmap retained from `docs/shared-node-plan-v1`.
- Old `feature/shared-chat-headless-v1`: abandoned experiment (not merged).

## Verification

- Production code changed during docs finalization: **NO**
- `origin/main` is ancestor of surfaces-demo (no unique main commits to preserve)
- Not accepted-by-user

---
Status: documentation-only. Not accepted.

# WINDOWS CURSOR — documentation reset before SharedNode

## Identity

- Starting SHA: `8c298fb71bcbf230484a82a4b33ad4d750e47833` (`origin/surfaces-demo` at fetch)
- Branch: `docs/shared-node-plan-v1`
- Files changed: `plan.md`, `Progress.md`

## Factual surfaces status observed at fetch

- Canonical `origin/surfaces-demo`: Windows / Android / Web model-driven adaptive
  orientation present (`presentation_*` Event → `IsWide`).
- Linux GTK3 adaptive branch present remotely:
  `feature/surfaces-linux-adaptive-final-v1` @
  `d7b47ac60c6fd94a8b96933e01d38e007fbf7f10` (not claimed merged into this
  checkout’s `surfaces-demo`).
- Apple adaptive finalization: **in progress / not yet verified** in this
  checkout (no Apple adaptive remote branch observed at fetch).

## Documentation outcome

- Stale “Linux X11/Xlib as current path” wording corrected; GTK3 is intended.
- Old shared-chat (`feature/shared-chat-headless-v1` @
  `63abddeeb57b78fdb7cfa4dc2a459785fa6566b8`) marked **experiment only**, not
  the new architecture base.
- New generic SharedNode / Link working design recorded (topology, local-
  persistent per-Link delivery state, ACK contract, Memory Link first,
  presence on Link, RW/RO vs visibility, `timestamp_us`-only order).
- Planned 21-step implementation ladder + open questions in `plan.md`.

## Verification

- Production code changed: **NO**
- Tests: **NOT RUN** (documentation-only)
- Not accepted-by-user

---
Status: implemented, verified in the iPhone simulator. Not accepted.

# IOS CURSOR — iOS SwiftUI view layer

## Identity

- Starting SHA: `062f01e2c983dc02036346e71e3f4825e24df05c`
- Branch: `surfaces-demo`
- Final SHA: `28711174f6d5086e5d9d3e106a5719c43cc86374`

## Behavior

- Page content is SwiftUI (`SurfacePageView`): caption from `PageTitle()` and
  the same per-Surface hue (`fmod(number * 0.17, 1.0)`, saturation 0.18,
  brightness 1.0). `IOSSurfacePresenter` still creates and retains the page
  container `UIView`, because `IOSApp::RelayoutPages` sets its frame.
- The bottom bar is SwiftUI (`SurfaceBarView`). The two `UIButton`s are gone;
  `SurfacesRootViewController` now owns a plain `bar` container whose frame
  still comes from `viewDidLayoutSubviews`.
- Boundary: SwiftUI → `id<IOSSurfaceActions>` (pure ObjC protocol adopted by the
  existing `SurfacesRootViewController`, replacing `onAdd:` / `onRemoveCurrent:`)
  → `IOSApp` → current `IOSSurfacePresenter` → model event.
- ObjC++ → Swift is three `@_cdecl` functions and no object ownership:
  `ApptraverseInstallIOSSurfacePage(container, title, hue)`,
  `ApptraverseInstallIOSSurfaceBar(container, actions, can_remove)`,
  `ApptraverseUpdateIOSSurfaceBar(container, can_remove)`.
- `Remove current` availability stays model-derived: the update call is made from
  the two publication paths that previously set `removeButton.enabled`, so Swift
  holds no state. The reveal after the initial publication is still
  `bar.hidden = NO`.
- `UIHostingController` is retained by `objc_setAssociatedObject` on its
  container (a hosting controller is not retained by its own view); that is also
  how the update entry point finds it. No global/singleton Swift state.
- No `TabView(.page)` and no SwiftUI app lifecycle: page set, page order,
  current page, frames and the pager stay in `IOSApp` / the presenter. All of
  `IOSApp`'s current / desired / index / reconcile logic is untouched.
- `Loading` stays a `UILabel`: host chrome shown before the model loads, with no
  model state behind it.

## Toolchain

- Xcode 15.2 / Swift 5.9.2 with C++ on `clang++-mp-20`.
- Swift target `surfaces_demo_ios_content` (static, own target — Swift cannot
  share a target with ObjC++); `-import-objc-header ios_surface_actions.h`.
- The simulator needs both `CMAKE_Swift_FLAGS "-sdk ..."` and
  `CMAKE_Swift_COMPILER_TARGET x86_64-apple-ios17.0-simulator`: CMake leaves
  `CMAKE_Swift_COMPILE_OPTIONS_SYSROOT` unset and swiftc otherwise targets the
  host macOS. Verified on the real command line.
- Both executables pin `LINKER_LANGUAGE OBJCXX` (the Swift driver rejects
  `-fno-rtti`) and add `-L<sdk>/usr/lib/swift` plus
  `-L<toolchain>/lib/swift/iphonesimulator` for the autolinked Swift runtime.
- `-fno-rtti` confirmed on every OBJCXX compile line of both targets; no
  generated `-Swift.h` (it needs clang modules, unavailable under
  `clang++-mp-20`).
- Incremental build only, in `build/ios-sim-x86_64-debug-surfaces-demo`;
  `ios_surfaces_demo` and `ios_surfaces_demo_load_only` both link.

## Tests

- No iOS test target exists; verification is a simulator run on iPhone 15 Pro
  Max `EB9ED0F9-3C43-48A2-A18B-66C654FAAD4D` (iOS 17.2), bundle id
  `com.apptraverse.surfaces`, uninstalled before each install.
- Launch: process stays alive; page shows the SwiftUI caption and tint;
  `Remove current` disabled at one Surface.
- Add → new page appended, pager settles on it, `Remove current` becomes
  enabled; second Add → `Surface 3` with its own hue.
- Swipe back and forward → `Surface 3` ↔ `Surface 2`, page settles.
- Remove current → neighbor becomes current; removing down to one page leaves
  `Remove current` disabled again.
- Relaunch reopened the persisted current Surface (`mobile_current`, not the
  first page).
- Clicks/swipes driven by synthetic `CGEvent`s against the Simulator window,
  each step verified from `simctl io screenshot`.

## Known limitations

- SwiftUI rendering is asserted only by screenshot; there is no headless or
  XCTest assertion for the iOS host.
- `simctl terminate` does not deliver `applicationWillTerminate`, so
  `Application::Save` does not run and the newest topology change is lost on a
  simulator kill. Reproduced identically with the pre-change UIKit binary, so it
  is pre-existing, not a port regression. Recorded in `plan.md`.
- The bundle has no launch storyboard, so iOS scales the app from a 320×480
  logical screen and in the light appearance (also pre-existing). Recorded in
  `plan.md`.
- Swift compiles are slow on this Intel host.

Not accepted-by-user.

---
Status: implemented, verified locally on macOS. Not accepted.

# MAC CURSOR — macOS SwiftUI view layer

## Identity

- Starting SHA: `3055daee7cf34220c29971b8714f98aef3894366`
- Branch: `surfaces-demo`
- Final SHA: `24521e7f638e8b45ff95cda747e493806ebc4523`

## Behavior

- Per-Surface window content is now SwiftUI (`SurfaceContentView.swift`,
  `NSHostingView`). The two `NSButton`s are gone, and the presenter no longer
  owns `add_button` / `close_button`.
- `MacSurfacePresenter` still owns the `NSWindow`, so persisted `desktop_*`
  bounds, `mobile_current` Z-order restore, red-X app stop, and
  Close-this-window Remove semantics are unchanged.
- Boundary: SwiftUI → `id<MacSurfaceActions>` (pure ObjC protocol adopted by
  the existing `SurfaceWindowDelegate`) → presenter → model event. The single
  ObjC++ → Swift call is `@_cdecl ApptraverseInstallMacSurfaceContent`.
- No `WindowGroup` / SwiftUI app lifecycle: the window set stays model-driven.

## Toolchain

- Xcode 15.2 / Swift 5.9.2 with C++ on `clang++-mp-20`.
- Swift target `surfaces_demo_macos_content` (static, own target — Swift cannot
  share a target with ObjC++); `-import-objc-header mac_surface_actions.h`;
  `CMAKE_Swift_FLAGS` must pass `-sdk` explicitly.
- Executables and the smoke test pin `LINKER_LANGUAGE OBJCXX` plus Swift
  runtime search paths: the Swift driver rejects `-fno-rtti`.
- `-fno-rtti` still applied to all OBJCXX TUs; no generated `-Swift.h`
  (it requires clang modules, unavailable under `clang++-mp-20`).

## Tests

- `apptraverse_surfaces_macos_smoke_test` PASS (4 cases, including
  `TestActiveZOrderRestored` and a new assertion that the SwiftUI content view
  is installed with both Surface controls).
- Click simulation now drives `MacSurfaceActions` instead of `NSButton` titles:
  SwiftUI macOS buttons are private `NSControl` subclasses with no title or
  `accessibilityIdentifier` on the `NSView`, and SwiftUI populates its
  accessibility tree only for an attached AX client (verified by spike).

## Known limitations

- SwiftUI's own rendering is not asserted headlessly; only that controls exist.
- iOS host is still UIKit (next slice).
- Swift compiles are slow on this Intel host (first SwiftUI build ~6 min).

Not accepted-by-user.

---
Status: implemented. Not accepted.

# MAC CURSOR — macOS demo auto-close with existing state

## Identity

- Starting SHA: `0296b03676a091fba0f96e35dd0729dce3e1abaa`
- Branch: `surfaces-demo`
- Final SHA: `cc1abaccc9a14f439d299bda73e0a3168d5dbf4d`

## Root cause

- Not an AppKit quit path (`windowShouldClose` / `RequestApplicationStop` /
  Loading teardown / Z-order restore). Instrumented startup with existing
  state completed presenters + restore; no stop was requested.
- Agent `shell &` left the GUI in the tool process group; when the tool
  session ended, Cursor reaped the process — windows vanished (“auto-close”).
  Confirmed: same binary stays alive with `fork`+`setsid` / new session.

## Fix (macOS-only)

- `main.mm`: default `fork` + `setsid` before AppKit; `--foreground` keeps
  parent session for job control / Ctrl-C.
- `mac_app.mm`: `SIGHUP` ignored; `applicationShouldTerminateAfterLastWindowClosed`
  returns NO; Loading `orderOut` keeps ownership until `Run` teardown.
- Smoke: `TestActiveZOrderRestored` waits 2s after restore and asserts
  windows / key Surface remain.

## Tests

- `apptraverse_surfaces_macos_smoke_test` PASS.
- Manual: agent-pattern `&` launch with `surfaces_runtime_state_clean` stays
  alive after shell end; Surfaces 1/2/4 visible.

Not accepted-by-user.

---
Status: implemented. Not accepted.

# MAC CURSOR — desktop active Surface Z-order

## Identity

- Starting SHA: `b4753c6471c6ea5f57f09268cd82974e3eb2b73a`
- Branch: `surfaces-demo`
- Final SHA: `60bf0a6a2330672b6cc3d81fea4c5eff80d97a74`

## Behavior

- macOS desktop persists the focused Surface via existing
  `Surfaces::mobile_current` (`windowDidBecomeKey` / shutdown key window →
  `PageShown` → `MakeCurrent`).
- On startup after presenter init: `makeKeyAndOrderFront` +
  `activateIgnoringOtherApps` for `mobile_current` (fallback: last Surface
  when empty — pre-z-order state dirs).
- All Surface windows still shown; only the active one is restored on top.
- Common model unchanged; mirrors Windows/Linux desktop Z-order contract.

## Tests

- `apptraverse_surfaces_macos_smoke_test` PASS (includes
  `TestActiveZOrderRestored`).
- Incremental build: `macos-x64-debug-surfaces-demo` (`-fno-rtti` on
  OBJCXX with ARC).
- Manual: activate Surface 2, close, reload → Surface 2 key / front.

Not accepted-by-user.

---
Status: implemented, verified locally on Linux. Not accepted.

# LINUX CURSOR — desktop active Surface Z-order

## Identity

- Starting SHA: `b234c7d67af517c7f20981d1f2c6a17c6fd3e4cf`
- Branch: `surfaces-demo`
- Final SHA: `91fca1a329aad21886b563e788a691d9e84d7600`

## Behavior

- Linux desktop persists the focused Surface via existing
  `Surfaces::mobile_current` (`FocusIn` / shutdown focused window →
  `PageShown` → `MakeCurrent`).
- On startup after presenter init: `XRaiseWindow` + best-effort
  `XSetInputFocus` for `mobile_current` (fallback: last Surface when empty —
  pre-z-order state dirs).
- All Surface windows still shown; only the active one is restored on top.
- Common model unchanged; mirrors Windows desktop Z-order contract.

## Tests

- `apptraverse_surfaces_linux_smoke_test` includes `TestActiveZOrderRestored`.
- Manual: activate Surface 2, close, reload → Surface 2 topmost.

Not accepted-by-user.

---
Status: implemented, verified locally. Not accepted.

# WINDOWS CURSOR — desktop active Surface Z-order + quiet console

## Identity

- Starting SHA: `cf0b7e6b81f431fc4fe420fb0759c35c7def8a06`
- Final SHA: `e8adadc17d0d8eb0787d03687c041bac7fed1211`
- Branch: `surfaces-demo` → `origin/surfaces-demo`

## Behavior

- Windows desktop persists the focused Surface via existing
  `Surfaces::mobile_current` (`WM_ACTIVATE` / shutdown foreground →
  `PageShown` → `MakeCurrent`).
- On startup after presenter init: `SetWindowPos(HWND_TOP)` + best-effort
  `SetForegroundWindow` for `mobile_current` (fallback: last Surface when
  empty — pre-z-order state dirs).
- All Surface windows still shown; only the active one is restored on top.
- Linux/macOS desktop hosts unchanged (still leave `mobile_current` empty).

## Console / logs

- `win32_surfaces_demo` `main`: `FreeConsole()` + `EnableNoninteractiveCrt()`.
- `aether-objects` built with `AE_NO_DEBUG_LOG=1` (no OBJ_SYS Save spam in
  Debug).

## Tests

- `apptraverse_surfaces_win32_smoke_test` PASS (includes
  `TestActiveZOrderRestored`).
- Manual: activate Surface 2, close, reload → Surface 2 topmost.

Not accepted-by-user.

---
Status: implemented, verified locally. Not accepted.

# MAC CURSOR — macOS + iOS merge into canonical surfaces-demo

## Identity

- Starting canonical SHA: `1bca4c495f50cf4fd0498e69cee7be65537b5181` (`origin/surfaces-demo`)
- macOS source SHA: `ab9e170620b4ed6c19f085aadd19d4faac0bcdd6` (`origin/feature/surfaces-macos-v1`)
- iOS source SHA: `4c782e39c6869a7a09cfe7fbd1d06a0560135c95` (`origin/feature/surfaces-ios-v1`)
- Merge commits:
  - macOS: `226e90fe817316a999a9efb8cd918129ddd049c9`
  - iOS: `7c4c60d16d91a7e4ab7c43c1edcebc6e55c3be91`
- iOS `mobile_current` adaptation: `5b195b5f5a4165199fc13250085f204de9e6a028`
- Final canonical SHA: `6e89129aa479c5a6bcbc48292528c9f485dc7799`

## Conflict files

- macOS merge: `examples/surfaces_demo/CMakeLists.txt`, `tests/CMakeLists.txt`, `plan.md`
- iOS merge: `examples/surfaces_demo/CMakeLists.txt`, `mobile/mobile_surface_presenter.{h,cpp}`, `plan.md`, `Progress.md`

## Common / mobile resolution

- Kept current `surfaces-demo` common model (geometry, `mobile_current`,
  `PageShown` / `MakeCurrent`, Web checkpoint, keepalive).
- One `MobileSurfacePresenter`: Android siblings + iOS helpers
  (`PageTitle`, `RemovableFromPager`).
- CMake: mutually exclusive WIN32 / EMSCRIPTEN / Linux / macOS AppKit /
  `if(IOS)` UIKit.

## iOS restore behavior

- Canonical current = `Surfaces::mobile_current` (Surface identity).
- Runtime pager index + `desired_current_id_` for rapid-swipe races.
- Initial: restore live `mobile_current` or seed `surfaces[0]` via `PageShown`.
- Swipe / Add / Remove settle → `PageShown` → `MakeCurrent`.
- Last page: Remove disabled (no `exit()`).

## macOS geometry

- Unchanged AppKit port: snapshot all frames before `RequestStop`; primary-screen
  conversion; red X / Cmd-Q = whole-app stop.

## Tests / toolchain

- Host: MacPorts clang++-mp-20, macOS SDK, `-fno-rtti` on CXX/OBJCXX.
- `apptraverse_surfaces_model_test` PASS (includes identity-after-index-change).
- `apptraverse_surfaces_macos_smoke_test` PASS.
- iOS Simulator build + install/launch PASS on iPhone SE (3rd gen) iOS 17.2;
  process stayed alive (`com.apptraverse.surfaces`).
- iOS OBJCXX compile commands include `-fno-rtti`.

## Ancestry

- `origin/feature/surfaces-macos-v1` ancestor of HEAD
- `origin/feature/surfaces-ios-v1` ancestor of HEAD
- starting canonical SHA ancestor of HEAD

Not accepted-by-user.

Status: implemented, verified locally on Linux. Not accepted.

# LINUX CURSOR — X11 merge into canonical surfaces-demo

## Starting / final

- Base canonical: `origin/surfaces-demo` @ `9da6672396d74f99bf61a75966895f1490607d8b`.
- Linux feature: `origin/feature/surfaces-linux-v1` @ `10a2db16f3bfcd1f88a9cc09dc72b2f910d22eee`.
- Merge: `git merge --no-ff origin/feature/surfaces-linux-v1` into local `surfaces-demo`.
- Merge commit / final canonical SHA: `a4d86db18c67782085a4e4e9c7ae9fc24203424e`.
- Pushed to `origin/surfaces-demo`. Remote `feature/surfaces-linux-v1` deleted after ancestry proof.

## Conflicts

- `examples/surfaces_demo/CMakeLists.txt` — kept Windows + Emscripten/web + Linux;
  Linux gated `UNIX AND NOT APPLE AND NOT ANDROID AND NOT EMSCRIPTEN`.
- `plan.md` — kept canonical `surfaces-demo` roadmap; marked Linux as landed (X11/Xlib).

Auto-merged: `Progress.md`, `tests/CMakeLists.txt` (Linux smoke condition tightened
to exclude ANDROID/EMSCRIPTEN).

## Common model decisions

Canonical `surfaces-demo` common model retained untouched:

- `Surfaces::mobile_current`, `SetCurrentSurfaceEvent`, `Surface::MakeCurrent()`,
  `SurfacePresenter::PageShown()`
- desktop geometry + `SurfaceBoundsChangedEvent`
- Web keepalive (`Domain::Find` structural keepalive)
- schema versions / no RTTI policy

Linux desktop does not call `PageShown` / does not mutate `mobile_current`.

## Backend note

Shipped Linux feature is **X11/Xlib** (not GTK3). No backend migration in this
merge. Prior plan text asked for GTK3; actual `feature/surfaces-linux-v1` code
links X11 only.

## Presenter hierarchy after merge

```
SurfacePresenter
  ├─ DesktopSurfacePresenter
  │  ├─ Win32SurfacePresenter
  │  └─ LinuxSurfacePresenter
  ├─ MobileSurfacePresenter
  │  └─ AndroidSurfacePresenter
  └─ WebSurfacePresenter
```

## Paths

- Add: X11 ButtonPress → `OnCommand` → `AddClick` → proxy → `AddSurface`
- Close this window: `RemoveClick`; last → app STOP without Remove
- Native WM_DELETE_WINDOW: whole-app STOP (never Remove)
- Geometry: snapshot all live windows → `SetDesktopBounds` → `RequestStop`

## Tests (Linux host)

PASS: `apptraverse_surfaces_model_test`, `apptraverse_surfaces_linux_smoke_test`,
`apptraverse_presenter_load_order_test`, `apptraverse_publication_channel_test`,
`apptraverse_event_sourced_core_test`, `apptraverse_journal_retention_test`.

`-fno-rtti` on Linux TUs via `apptraverse_compile_policy`.

Windows/Android/WASM binaries not executed on this Linux machine; source/CMake
conditions retained and mutually exclusive.

Not accepted-by-user.

---
Status: implemented, verified on emulator. Not accepted.

# surfaces_demo — Android pager port

## Starting / final

- Starting HEAD: `7e86814` (origin/prep/deps-objects-assert-mcp-v1).
- Branch: `feature/surfaces-android-v1` (separate worktree).
- Windows branch untouched; not merged into the prep branch.

## Toolchain

- SDK `C:/Users/nickc/AppData/Local/Android/Sdk`, NDK `29.0.14206865`, cmake `4.1.2`.
- AGP 8.7.3, Gradle wrapper 8.9, JDK 20, build-tools 36.0.0.
- compileSdk 34, targetSdk 34, minSdk 24. ABI: x86_64 only.
- AVD `Aether_NDK_Smoke_x86_64`, API 34, serial `emulator-5554`.
- Framework widgets only: no AndroidX, no Compose.

## Presenter hierarchy

`SurfacePresenter` → `MobileSurfacePresenter` → `AndroidSurfacePresenter`,
sibling to `DesktopSurfacePresenter`. GUI Domain resolves the most-derived
registered child; no RTTI. Proof of `-fno-rtti` on the real Android command
line (`compile_commands.json`, `jni_bridge.cpp`):
`--target=x86_64-none-linux-android24 -DANDROID -std=c++20 -fno-rtti`.

## Presentation

- Single Activity, top bar `[ Add ] [ Remove current ]`, one page per Surface.
- Pager via `GestureDetector.onFling`.
- Portrait locked in the manifest for this slice; orientation is not persisted.

## Persisted current page (explicit spec change, approved by the user)

The original slice kept the current page out of the model, so a restart always
reopened the first page. On request the current page became model state:

- `Surfaces` schema 0 → 1, new persisted `Surface::ptr mobile_current`.
  `Load(ae::Version<0>)` throws, so pre-existing state dirs on every platform
  must be recreated — the same policy `Surface` v0 already uses.
- New `SetCurrentSurfaceEvent`; `Surface::MakeCurrent()` commits it on the
  model thread and no-ops when the page is already current or already removed.
- `Apply(RemoveSurfaceEvent)` clears `mobile_current` when the removed Surface
  was the current one. Which page becomes current next stays presentation
  policy: the Activity picks the neighbour and reports it.
- Every settled page reports once through
  `SurfacePresenter::PageShown()` → `ModelObjectProxy` → `Surface::MakeCurrent`,
  so a swipe does produce an Event, a commit and a publication. The Activity
  tracks the last reported id, so the publication it triggers is not echoed
  back as a second report.
- Desktop hosts show every Surface at once and leave `mobile_current` empty,
  the same way mobile ignores `desktop_*`.

## Paths

- Add: button → JNI `nativeAddFromSurface(objId)` → current
  `SurfacePresenter::AddClick()` → `ModelObjectProxy` → model thread.
- Remove current (count > 1): `RemoveClick()` → `RemoveSurfaceEvent`; the
  neighbor `min(old_index, new_size - 1)` becomes current and is reported.
- Remove current on the last page: `SURFACES_LAST_PAGE_STOP` → `RequestStop`;
  no `RemoveSurfaceEvent`, the Surface stays persisted.
- Back: `BACK_REQUESTED_STOP` → model stop first, Activity finishes only after
  `onModelStopped`.
- Persistence: `DirectoryDomainStorage` under `filesDir/surfaces_state`; no
  external storage, no permissions.

## Threads / JNI

- Android main thread = GUI thread; `"apptraverse-model"` native thread = model.
- Publications: model thread → `NativeUiBridge` main-thread Handler → GUI
  mirror → presenters → page list.
- JNI carries Surface ObjIds and page numbers only. No model pointers, no
  `jlong` object pointers, no Java refs in reflected state.

## Headless proof

`apptraverse_surfaces_model_test` (own MSVC/Ninja tree in this worktree,
`-DAPPTRAVERSE_BUILD_AETHER_DEMOS=OFF`) — OK, including three new cases:

- `TestCurrentPageReplay`: Event committed once, repeat is a no-op,
  `ReplayFromBase` restores the current page, removing it clears the reference,
  a stale Surface cannot become current.
- `TestCurrentPagePersistence`: current page survives save/load.
- `TestCurrentPageThroughGuiProxy`: `PageShown()` → model thread → publication
  → GUI mirror resolves `mobile_current` to the mirror's own Surface.

`apptraverse_surfaces_win32_smoke_test` — OK after the schema bump, and
`win32_surfaces_demo` still links.

## Emulator verification (`tools/android/run_surfaces_smoke.ps1`, 7 phases)

- Clean start → `numbers=1, count=1`, page `Surface 1 (1 / 1)`.
- Add ×2 → `numbers=1,2,3, count=3`; swipes reach `Surface 2`, `Surface 3`.
- Remove current on `Surface 2` → `numbers=1,3`; neighbor page becomes current.
- Back → `SURFACES_STATE_SAVED` → `SURFACES_UI_UNLOADED` →
  `SURFACES_APP_STOPPED`, process gone.
- Relaunch → `numbers=1,3, count=2` and the pager reopens on `Surface 3`,
  the page that was current at shutdown.
- Remove down to the last page → `SURFACES_LAST_PAGE_STOP`, save, exit;
  relaunch shows the surviving `Surface 1` (`count=1`).
- Logcat free of fatal/assert/SIGSEGV/JNI errors.

## Known limitations

- Back (and the last Remove current) is the only save point. A force-stop or a
  system kill leaves the newest topology and current page unsaved; the state
  then reloads from an earlier point. No autosave was added.
- `apptraverse_surfaces_model_test` is not yet wired into an Android native
  test target.

## Common files changed

- `examples/surfaces_demo/CMakeLists.txt`: added the `surfaces_demo_mobile`
  target.
- `examples/surfaces_demo/common/surfaces_model.h/.cpp`: `Surfaces` v1 with
  `mobile_current`, `SetCurrentSurfaceEvent`, `Surface::MakeCurrent`,
  `SurfacePresenter::PageShown`. Desktop presenters and the Win32 host are
  unchanged, but existing state dirs must be recreated.
- `tests/surfaces_model_test.cpp`: current-page cases.

Not accepted-by-user.

---
Status: implemented, verified locally. Not accepted.

# surfaces_demo — Linux desktop port (X11)

## Starting / final

- Base: `origin/prep/deps-objects-assert-mcp-v1` @ `7e86814`.
- Branch: `feature/surfaces-linux-v1` (separate worktree; parallel with macOS).
- Final SHA: `d465f2b5182d0d5d05d0253561c424805010a113`.
- Pushed to origin/feature/surfaces-linux-v1. Not merged into prep.

## Backend

X11/Xlib only (no Qt/GTK/SDL). Drawn hit-test buttons; `WM_DELETE_WINDOW`
for native close. `_NET_FRAME_EXTENTS` for best-effort outer geometry.

## Presenter hierarchy

```
SurfacePresenter
  ↓
DesktopSurfacePresenter
  ├─ Win32SurfacePresenter   (unchanged)
  └─ LinuxSurfacePresenter   (new)
```

Object-system registration; GUI Load picks Linux as most-derived on this host.
No `dynamic_cast`; hierarchy via `Registry::GenerationDistance`. Common model
and `DesktopSurfacePresenter` API unchanged.

## Native paths

- **Add:** ButtonPress hit → `OnCommand` → `AddClick` → proxy → `AddSurface`
- **Close this window:** → `RemoveClick` / last → app STOP without Remove
- **Native WM close:** → whole-app STOP (never Remove)
- Shutdown: `QueueAllWindowBounds` → `RequestStop` → model drain → Save

## Targets

- `linux_surfaces_demo` / `linux_surfaces_demo_load_only`
- `apptraverse_surfaces_linux_smoke_test`

## `-fno-rtti` proof

Ninja FLAGS for `linux_presenters.cpp` / `linux_app.cpp` include `-fno-rtti`
(via `apptraverse_compile_policy`).

## Tests

PASS: `apptraverse_surfaces_model_test`, `apptraverse_presenter_load_order_test`,
`apptraverse_surfaces_linux_smoke_test` (DISPLAY=:0.0). Geometry restore uses
40px tolerance for WM decoration variance.

Not accepted-by-user.

---
Status: implemented, verified locally. Not accepted.

# surfaces_demo — macOS desktop port

## Starting / final

- Starting HEAD: `7e86814` (`origin/prep/deps-objects-assert-mcp-v1`).
- Branch: `feature/surfaces-macos-v1` (worktree
  `/Users/nick/Projects/apptraverse-surfaces-macos-v1`).
- Common model / Windows / `DesktopSurfacePresenter`: unchanged.
- Final SHA: `3ccb5d9100992da49a02af2534cae9d0f327bed7`.

## Implementation

```
SurfacePresenter
  ↓
DesktopSurfacePresenter
  ├─ Win32SurfacePresenter
  └─ MacSurfacePresenter   [THIS]
```

- `examples/surfaces_demo/macos/` — `MacApp`, `MacSurfacePresenter` (`.mm` + ARC)
- NSWindow per Surface; Add / Close this window; red X + Cmd-Q → one stop path
  (snapshot all frames → `RequestStop`)
- Primary-screen coordinate conversion (common top-left ↔ AppKit); multi-monitor
  out of scope
- Targets: `macos_surfaces_demo`, `macos_surfaces_demo_load_only`
- `-fno-rtti` on CXX and OBJCXX; no C++ `dynamic_cast`

## Toolchain note

Apple Clang 15 rejects pinned `aether-miscpp` parenthesized aggregate init
(P0960). Build used MacPorts `clang++-mp-20` / `clang-mp-20` with macOS SDK.

## Tests

| check | result |
| --- | --- |
| `apptraverse_surfaces_model_test` | PASS (`-fno-rtti`, MacPorts Clang 20) |
| `apptraverse_surfaces_macos_smoke_test` | PASS (Add / Close middle / red X / geometry restore / last Close) |

Compile commands confirmed `-fno-rtti` on `.cpp` and `.mm`.

Not accepted-by-user.

---
Status: implemented, verified locally. Not accepted.

# surfaces_demo — iOS simulator port

## Starting / final

- Starting HEAD: `7e86814` (`origin/prep/deps-objects-assert-mcp-v1`).
- Branch: `feature/surfaces-ios-v1` (worktree
  `/Users/nick/Projects/apptraverse-surfaces-ios-v1`).
- Feature SHA: `0816f6c`.
- Pushed to `origin/feature/surfaces-ios-v1`.
- Common model / desktop / Windows: unchanged.
- macOS / Linux ports: not claimed finished on this branch.

## Implementation

```
SurfacePresenter
  ↓
MobileSurfacePresenter   (platform-neutral; no desktop geometry)
  └─ IOSSurfacePresenter [THIS]  (UIKit page UIView)
```

- `examples/surfaces_demo/mobile/` — `MobileSurfacePresenter`
- `examples/surfaces_demo/ios/` — `IOSApp`, `IOSSurfacePresenter`, `main.mm`,
  `Info.plist.in`, `run_simulator.sh`
- One UIWindow / root VC; `UIScrollView` paging; pages follow `Surfaces::surfaces`
- Host buttons: `[ Add ] [ Remove current ]`; current page = host
  `current_index_` (runtime-only; not model / not persisted)
- Add → current `IOSSurfacePresenter::AddClick` → model `AddSurface`
- Remove current → `RemoveClick` when count > 1; disabled when count == 1
- `desktop_*` ignored on mobile
- State dir: Application Support/`AppTraverseSurfaces` (optional `--state-dir`)
- Targets: `ios_surfaces_demo` (distill), `ios_surfaces_demo_load_only`
- `-fno-rtti` on OBJCXX; ARC on `.mm`
- CMake: `if(IOS)` only (not macOS `APPLE`)

## Toolchain / simulator

- Intel Mac `x86_64`; Xcode 15.2; iOS Simulator SDK 17.2
- Clang: MacPorts `clang++-mp-20` (Apple Clang 15 cannot build pinned aether-miscpp)
- Device used for launch proof: **iPhone 15**
  `88714D26-E069-4135-9F63-35B02D75DDFA` (iOS 17.2)
- Bundle id: `com.apptraverse.surfaces`
- App:
  `build/ios-sim-x86_64-debug/examples/surfaces_demo/ios/ios_surfaces_demo.app`

## Build / launch

```
cmake -S . -B build/ios-sim-x86_64-debug -G Ninja \
  -DCMAKE_SYSTEM_NAME=iOS \
  -DCMAKE_OSX_SYSROOT=.../iPhoneSimulator.sdk \
  -DCMAKE_OSX_ARCHITECTURES=x86_64 \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=17.0 \
  -DCMAKE_C_COMPILER=/opt/local/bin/clang-mp-20 \
  -DCMAKE_CXX_COMPILER=/opt/local/bin/clang++-mp-20 \
  -DCMAKE_OBJCXX_COMPILER=/opt/local/bin/clang++-mp-20 \
  -DAPPTRAVERSE_BUILD_AETHER_DEMOS=OFF -DBUILD_TESTING=OFF
cmake --build build/ios-sim-x86_64-debug --target ios_surfaces_demo
# or: examples/surfaces_demo/ios/run_simulator.sh
```

simctl: install exit 0; launch `com.apptraverse.surfaces: 17397`; process stayed
alive after launch.

## Tests / manual

- Host macOS `apptraverse_surfaces_model_test`: not re-run in this iOS build tree
  (`BUILD_TESTING=OFF`); common model sources compile for iphonesimulator.
- Automated UI XCTest: not added (scope).
- Manual checklist (Add / swipe / Remove / last-disable / relaunch topology):
  for user in Simulator.

## Files outside `ios/`

- `examples/surfaces_demo/mobile/` (new mobile presenter layer)
- `examples/surfaces_demo/CMakeLists.txt` — `surfaces_demo_mobile` + `if(IOS)`
- `plan.md` / `Progress.md` — this section

No common/desktop/windows API changes. No common API blocker.

Not accepted-by-user.

---
Status: implemented, verified locally. Not accepted.


# surfaces_demo — Windows semantics + persisted window geometry

## Starting / final

- Starting HEAD: `430b16f`.
- Branch: `prep/deps-objects-assert-mcp-v1`.
- Final SHA: `9ad6df2`.
- Pushed to origin/prep/deps-objects-assert-mcp-v1.

## Semantics

- UI: `[ Add ] [ Close this window ]` per Surface HWND
- Native X → whole-app STOP (never RemoveSurface)
- Close this window → RemoveSurfaceEvent; last Close → STOP without Remove
- `OnCommand(command_id, notification_code)` routing (dynamic_objects updated)

## Geometry

- Persisted on model `Surface`: `desktop_x/y/width/height` (schema v1)
- `SurfaceBoundsChangedEvent` + `Surface::SetDesktopBounds`
- No Events on WM_MOVE/WM_SIZE
- Shutdown: `QueueAllWindowBounds` → `RequestStop` → model drain → Save
- `ModelObjectProxy::Invoke` captures by-value args
- `DesktopSurfacePresenter::UpdateModelBounds`

## Proof

- Headless: bounds replay + geometry persistence
- Win32 smoke: Close removes one; X keeps all + restores rects; last Close keeps Surface
- `/GR-` on desktop/Win32/smoke TUs
- Regressions: dynamic_objects_add, dynamic_objects_win32_smoke, presenter_load_order, publication_channel

Manual: `build/win64-ninja-msvc-debug/examples/surfaces_demo/windows/win32_surfaces_demo.exe`

Not accepted-by-user.

---
Status: implemented, verified locally. Not accepted.

# surfaces_demo slice 2 — Windows minimal multi-window

## Starting / final

- Starting HEAD: `ddd85bd`.
- Branch: `prep/deps-objects-assert-mcp-v1`.
- Final SHA: `d8bdb97`.
- Pushed to origin/prep/deps-objects-assert-mcp-v1.

## Cleanup (slice 1 residuals)

Removed redundant checks from `surfaces_model.cpp`:

- Apply Add/Remove: no `is_valid` / `is_loaded` / parent relation re-checks
- `Surface::AddSurface` / `Remove`: no `domain != nullptr`
- `AddClick` / `RemoveClick`: no `model_proxy != nullptr`

Kept: live-membership miss → no-op; Apply Remove miss → assert (broken Event).

## Presenter hierarchy

```
SurfacePresenter
  ↓
DesktopSurfacePresenter
  └─ Win32SurfacePresenter  (HWND hwnd, HWND add_button)
```

Object-system registration; GUI LoadRoot picks Win32 as most-derived.
No `dynamic_cast`; hierarchy proven via `Registry::GenerationDistance`.

## Native paths

- **Add:** BUTTON → `DispatchChildCommand` → `OnCommand` → `AddClick` → proxy →
  `Surface::AddSurface` → Event → structural pub → new `OnLoad` / HWND
- **X non-last:** `WM_CLOSE` → `RemoveClick` → Event → `OnUnload` / DestroyWindow
- **X last:** `WM_CLOSE` → `WM_APPTRAVERSE_STOP` (no Remove); Surface persisted

## HWND ownership

One WNDCLASS for all Surface windows. HWND created only in `OnLoad`, destroyed
only in `OnUnload`. Survivors keep identity across Add/Remove.

## Manual executable

`build/win64-ninja-msvc-debug/examples/surfaces_demo/windows/win32_surfaces_demo.exe`
(`--state-dir <path>`). Load-only: `win32_surfaces_demo_load_only.exe`.

## `/GR-` proof

Ninja FLAGS for desktop/win presenters, WinApp, smoke test include `/GR-`.

## Tests

PASS: `apptraverse_surfaces_model_test`, `apptraverse_surfaces_win32_smoke_test`
(add from any window, middle close, last-window exit + restart Surface 3).
Regressions PASS: dynamic_objects_add, presenter_load_order.

Not accepted-by-user.

---

Status: implemented, verified locally. Not accepted.

# surfaces_demo slice 1 — common model + headless

## Starting / final

- Starting HEAD: `f6bbdc8`.
- Branch: `prep/deps-objects-assert-mcp-v1`.
- Final SHA: `e0b33a8`.
- Pushed to `origin/prep/deps-objects-assert-mcp-v1`.

## Object graph

```
Application
 └── surfaces → Surfaces : Node
      └── Surface : Node
           └── presenter → SurfacePresenter
```

Initial distill: Application → Surfaces → Surface #1 → SurfacePresenter.

## Event paths

- `Surface::AddSurface()` → create Surface + Presenter → `InitializeRuntimeNode` → `AddSurfaceEvent` → `Surfaces::Commit`
- `Surface::Remove()` → live-membership check → `RemoveSurfaceEvent` → `Surfaces::Commit` (stale = no-op)
- Topology vector mutates only in `Surfaces::Apply(Add|Remove)`

## Dynamic Node init

Runtime Surface uses canonical `InitializeRuntimeNode` (same as distill `FinalizeDistilledGraph`) before Commit. No second Node-init mechanism.

## Remove current (no model current)

`SurfacePresenter::RemoveClick()` proxies that Surface ObjId. Presentation (future pager / window X) selects which presenter; model has no `current_surface`.

## Proven headless

- initial graph; Add from Surface1 and from Surface2; Remove; replay [1,3]; restart + Add→4
- dynamic Node structural publication (GUI Surface2 new ObjId match, different C++ instance, OnLoad once)
- GUI proxy AddClick / RemoveClick
- presenter lifecycle (no reload of survivors; historical not reactivated)
- shutdown drain of accepted Adds
- `/GR-` on `surfaces_model.cpp` and `surfaces_model_test.cpp`

## Tests

PASS: `apptraverse_surfaces_model_test`
Regressions PASS: dynamic_objects_add, presenter_load_order, publication_channel, event_sourced_core, journal_retention, main_window lifecycle/window.

Windows UI not started.

Not accepted-by-user.

---

Status: implemented, verified locally. Not accepted.

# Disable RTTI and enforce AppTraverse invariants (before surfaces_demo)

## Starting / final

- Starting HEAD: `42536ac`.
- Branch: `prep/deps-objects-assert-mcp-v1`.
- Final SHA: `baa5ed9`.
- Pushed to origin/prep/deps-objects-assert-mcp-v1.

## Permanent rules

Updated `.cursor/rules/apptraverse-coding-agent.mdc`:

- **No C++ RTTI** (`dynamic_cast` / `typeid` / `std::type_info` / `std::type_index` forbidden)
- Use Æther class IDs, `Registry::GenerationDistance`, typed `Ptr`/`ObjPtr`, `Ptr::as<T>()` when type is guaranteed
- **Do not re-check established invariants**
- **No defensive programming** (invariant-driven; applies to all future ports)

## Build policy

- `cmake/apptraverse_compile_policy.cmake` → INTERFACE `apptraverse_compile_policy`
  - MSVC: `/GR-`
  - GCC/Clang: `-fno-rtti`
- `apptraverse` PUBLIC-links the policy (consumers inherit automatically)
- Targets that do not link `apptraverse` link the policy PRIVATE
  (`publication_channel_test`, `win32_fatal_ndebug_*`)

## Compile-time guard

- `include/apptraverse/no_rtti.h` (`_CPPRTTI` / `__GXX_RTTI` → `#error`)
- Included from `include/apptraverse/object_macros.h` (central AppTraverse object header)

## Removed `dynamic_cast` (AppTraverse-owned)

Replaced with `Registry::GenerationDistance` + `static_cast`, or typed `ObjPtr`/`Ptr`:

| location | replacement |
| --- | --- |
| `src/object_serialization.cpp` | local `AsObjOf<T>` / `AsPresenter` |
| `src/graph_mirror.cpp` | `GenerationDistance` + `static_cast` for Node |
| `examples/dynamic_objects_demo/common/dynamic_lifecycle.cpp` | structural apply cleanup + no Session repair |
| `examples/chat_ui_runtime_demo/common/chat_shared.cpp` | class-id / typed path |
| `tests/chat_p2p_headless_test.cpp` | class-id / typed path |
| `tests/chat_presentation_headless_test.cpp` | class-id / typed path |
| `tests/dynamic_two_main_win32_test.cpp` | `GetClassId` + typed presenter |
| `tests/journal_retention_test.cpp` | typed path |
| `tests/main_window_window_changed_test.cpp` | typed path |
| `tests/shared_journal_test.cpp` | typed path |

`git grep dynamic_cast` in `*.cpp`/`*.h`: only the intentional string CHECK in
`tests/dynamic_objects_add_test.cpp` (WndProc source must not contain the token).

No `typeid` / `std::type_info` / `std::type_index` in AppTraverse-owned source/tests.

## Defensive-check cleanup

- `ApplyItemListStructural`: call `OnModelChanged()` on live presenters directly
  (no `is_valid` / `is_loaded` / `presentation_loaded` gates after structural apply)
- `EnsureItemListWindowLink` **removed**: Session must not repair graph schema.
  `ItemList::window` is schema v1; pre-v1 persisted state requires re-distill /
  fresh state (development demo policy).
- Left real alternatives: WndProc `presenter == nullptr` before userdata attach;
  `CreateWindowExW` failure → `FatalWin32`; event Apply asserts at Commit boundary.
- Removed unused `#include "aether/clock.h"` from `event_sourced_core_test.cpp`
  (test links only `apptraverse`, not full Aether client).

## `/GR-` proof (MSVC incremental `build/win64-ninja-msvc-debug`)

Actual ninja `FLAGS` for AppTraverse-owned CXX objects include `/GR-` and not bare `/GR`.
Examples after regenerate:

```
object_serialization.cpp.obj ... /Zc:preprocessor /GR-
publication_channel_test.cpp.obj ... -std:c++20 -MDd /GR-
win32_fatal_ndebug_child.cpp.obj ... -std:c++20 -MDd /GR-
```

Verbose `cl.exe` lines for AppTraverse TUs also showed `/GR-`.

GCC/Clang policy is wired as `-fno-rtti` (not exercised on this Windows slice).

## plan.md

Next: **surfaces_demo common model + headless only**.
Then Windows minimal multi-window (no resize/Z-order/DPI), then macOS/Linux,
then mobile, then SharedNode / Chat / AeroAdmin-X.
`surfaces_demo` **not started** in this slice.

## Tests (local incremental)

PASS:

- `apptraverse_presenter_load_order_test`
- `apptraverse_dynamic_objects_add_test`
- `apptraverse_dynamic_two_main_win32_test`
- `apptraverse_publication_channel_test`
- `apptraverse_main_window_window_changed_test`
- `apptraverse_main_window_lifecycle_test`
- `apptraverse_main_window_win32_smoke_test`
- `apptraverse_dynamic_objects_win32_smoke_test`
- `apptraverse_event_sourced_core_test`
- `apptraverse_journal_retention_test`

MCP: first build attempt failed on a stale target list mid-reconfigure; local
MSVC/ninja incremental script completed successfully. Chat headless suite not
re-run as primary gate this slice (casts removed; core/dynamic/main_window set above).

Not accepted-by-user.

---

Status: implemented, verified locally. Not accepted.

# dynamic_objects_demo final cleanup (before surfaces_demo)

## Starting / final

- Starting HEAD: `4e6098f`.
- Branch: `prep/deps-objects-assert-mcp-v1`.
- Final SHA: `5042132`.
- Pushed to `origin/prep/deps-objects-assert-mcp-v1`.

## Changes

- `ReadyForPresentation` only checks parent `presentation_loaded`.
- After multipass `InitializeNewPresenters`, every live Presenter must be
  `presentation_loaded` (assert; broken graph is not "not ready").
- No `dynamic_cast` in dynamic demo / ModelObjectProxy / presenter-walk path.
  Typed conversion: `ae::Ptr::as<T>()`, `ObjPtr` AbleToCast conversion,
  `Registry::GenerationDistance` for Presenter discovery (`AsPresenter`).
- `Presenter::OnCommand` + Win32 `DispatchChildCommand`; parents do not know
  Add/Remove. (Separate `Win32Presenter` Obj base avoided — would diamond with
  `MainWindowPresenter` / `AddItemPresenter`.)
- `WM_APPTRAVERSE_CLOSE_WINDOW` → `RequestStop()` directly.
- `Item::Remove`: required `list` relation; only live-membership miss is no-op.

## Tests (local incremental `build/win64-ninja-msvc-debug`)

PASS: dynamic add/proxy/Ready/WndProc-source, Win32 smoke, two-main,
presenter load order, publication_channel, main_window lifecycle/window/smoke.

Not started: surfaces_demo.

Not accepted-by-user.

# UI ownership + ModelObjectProxy (before surfaces_demo)

## Starting / final

- Starting HEAD: `67fb538` (foundation hardening).
- Branch: `prep/deps-objects-assert-mcp-v1`.
- Final SHA: `4327336`.
- Pushed to `origin/prep/deps-objects-assert-mcp-v1`.

## Object graph

```
Application
 └── MainWindow
      ├── presenter → MainWindowPresenter (top-level HWND only)
      ├── add_item → AddItem
      │                └── presenter → AddItemPresenter (Win32: BUTTON HWND)
      └── item_list → ItemList
                       ├── presenter → ItemListPresenter
                       └── Item → ItemPresenter (label + [x] HWNDs)
```

Presenter does not own child presenters; composition is the object graph.

## GUI → model path

```
Win32 click → Win32*Presenter → AddItemPresenter::Click / ItemPresenter::RemoveClick
  → ModelObjectProxy::Invoke(ObjId, &T::method)
  → session.Post(ModelWork)
  → model Domain::Find(ObjId) → AddItem::Click / Item::Remove
  → AddItemEvent / RemoveItemEvent → ItemList::Commit
  → NoteMaterializedChange → structural publication → GUI mirror
```

- No model pointers across domains/threads.
- GUI Presenter never Commits Events.
- Session/WinApp no longer know Add/Remove semantics (`AddItemCommand`,
  `RemoveItemCommand`, `WM_APPTRAVERSE_ADD/REMOVE_ITEM`, free `CommitAddItem` /
  `CommitRemoveItem` removed).
- Shutdown drain preserved on generic `pending_work` (accepted before stop runs;
  after stop rejected).

## Tests (local incremental `build/win64-ninja-msvc-debug`)

| check | result |
| --- | --- |
| `apptraverse_dynamic_objects_add_test` (graph, Click/Remove, GUI proxy, drain) | PASS |
| `apptraverse_dynamic_objects_win32_smoke_test` (Add BUTTON owned by Win32AddItemPresenter) | PASS |
| `apptraverse_dynamic_two_main_win32_test` | PASS |
| `apptraverse_presenter_load_order_test` | PASS |
| `publication_channel` + `main_window_*` lifecycle/window/smoke | PASS |

MCP `apptraverse_build_start` hit a transient `unknown target` against this tree;
proof is the local MSVC+ninja incremental script.

Not started: surfaces_demo.

Not accepted-by-user.

# Foundation hardening for surfaces_demo

## Starting point

Branch `prep/deps-objects-assert-mcp-v1` at `6a63fd6` (Remove Item Progress SHA).

## Explicitly NOT in scope

Storage Save/Load / DirectoryDomainStorage / filesystem failures are assumed
impossible in this architecture. No error propagation work.

SharedNode, transport, presence, chat, surfaces_demo implementation, structural
delta/bandwidth optimization — not started. TODO recorded for structural payload
size only.

## Issues fixed

1. **Presenter load order** — runtime `presentation_load_order` set on successful
   OnLoad; UnloadPresenters / structural unload sort descending (child before parent),
   independent of ObjId / Save collect order. Win32 no longer uses IsWindow
   “parent already destroyed child” branches.

2. **Explicit parent** — `ItemList::window` (`ae::ObjPtr<MainWindow>`); graph sets
   both directions. Win32 ItemList uses `list->window`, not fixed MainWindow ObjId.
   v0 saves migrate via `EnsureItemListWindowLink`.

3. **Native class lifetime** — `RegisterDynamicWin32Classes` /
   `UnregisterDynamicWin32Classes` around WinApp; per-HWND OnLoad/OnUnload only
   create/destroy windows.

4. **WM_CLOSE** — presenter posts `WM_APPTRAVERSE_CLOSE_WINDOW(window ObjId)`;
   WinApp stops only when that id is the single MainWindow.

5. **Generic structural keepalive** —
   `CaptureStructuralPresentationKeepalive` / `ApplyStructuralPublicationAndUpdatePresenters`
   hold `ae::Ptr<ae::Obj>` + `Presenter::ptr`. `ApplyItemListStructural` no longer
   enumerates concrete Item/presenter vectors for lifetime.

6. **Shutdown drain** — after `RequestStop`, accepted deque commands Commit in order
   without requiring GUI publication consumption; then Save. Submit after stop is no-op.

7. **Distill separation** — `dynamic_distill.cpp` / `BuildDynamicObjectsGraph` linked
   only by distill demo + fixture tests; load-only does not link it.

8. **CMake** — `APPTRAVERSE_BUILD_AETHER_DEMOS` (default ON) gates full `aether` client
   + chat/model_ui/presence. Core + dynamic_objects + main_window build without it.
   `NOMINMAX` / `WIN32_LEAN_AND_MEAN` only `if(WIN32)`.

## Tests (rebuilt/relinked/run locally)

| check | result |
| --- | --- |
| `apptraverse_presenter_load_order_test` | PASS |
| `apptraverse_dynamic_objects_add_test` (incl. shutdown drain) | PASS |
| `apptraverse_dynamic_two_main_win32_test` | PASS |
| `apptraverse_dynamic_objects_win32_smoke_test` | PASS |
| `publication_channel_test`, `main_window_*` lifecycle/window/smoke | PASS |

MCP used only for an early configure probe (`already_configured`); proof is local
incremental `build/win64-ninja-msvc-debug` with MSVC env.

Commits: `7fab099` (presenter lifecycle / structural keepalive), `2ce939a`
(Win32 multi-window prep), `52c386f` (shutdown drain / distill / CMake / plan).
Pushed to `origin/prep/deps-objects-assert-mcp-v1`.

Not implemented: surfaces_demo.

Not accepted-by-user.

# Dynamic objects demo — Remove Item

## Starting point

Branch `prep/deps-objects-assert-mcp-v1` at `ef26be6` (Add Item Progress SHA note).

## Representations

- `RemoveItemCommand { ae::ObjId item_id }` — copyable identity only; no model pointer.
- `RemoveItemEvent : EventFor<ItemList,…> { Item::ptr item }` — Apply erases that Item from live `items` and `NoteMaterializedChange()`. No physical destroy, no journal GC, no presenter/Win32 calls.
- Command queue: `std::variant<AddItemCommand, RemoveItemCommand>` deque (discrete; not coalesced).

## Live vs historical

Removing from `ItemList::items` does not remove historical reachability via `AddItemEvent` in the journal. GUI presentation walks **live** topology only (`CollectLiveReachableObjects` temporarily clears Node `base`/`journal` for Save-based collect). Historical Item/presenter may remain graph-reachable but must not re-`OnLoad`.

## Presenter unload

- Capture active presenters before structural apply (held via `Presenter::ptr`).
- Apply structural publication fully.
- `UpdatePresentersAfterStructuralPublication`: OnUnload presenters in previously-active minus live; then `InitializeNewPresenters` for newly live only.
- `UnloadPresenters` iterates reverse collect order so child HWNDs go before parents; child OnUnload tolerates already-destroyed HWND if parent won the race.
- Win32 Item `[x]` → `WM_APPTRAVERSE_REMOVE_ITEM` → command → Event → publication → OnUnload → `DestroyWindow` row/button.

## Numbering

`CommitAddItem` uses `max(existing.number)+1` (not `size()+1`). After remove Item2, next Add is Item4.

## Stale / double Remove

`CommitRemoveItem`: if ObjId not in live `items`, **no-op** (`return false`, no Event). Chosen because GUI can race a second click; must not delete another Item or abort.

## Persistence

No Save on Remove. Shutdown `Application::Save()` after model stop. ItemList retention unchanged (unlimited). Empty live list is valid.

## Tests

| check | result |
| --- | --- |
| model remove + journal still holds historical Item | PASS |
| replay Add+Remove → live list without Item2 | PASS |
| mirror remove: survivor pointer identity, OnUnload==1 | PASS |
| historical presenter: no second OnLoad after remove | PASS |
| middle remove + Add → numbers 1,3,4 | PASS |
| sequence Add/Add/Remove/Add/Remove → live 3,4 | PASS |
| Win32 smoke Add/Remove + child load-only restore Item3,Item4 | PASS |
| `publication_channel_test`, `main_window_window_changed_test` | PASS |
| `main_window_lifecycle_test`, `main_window_win32_smoke_test` | PASS (existing bins) |

MCP `user-apptraverse` not used as proof for this slice (local incremental `build/win64-ninja-msvc-debug`).

Commits: `19c31a9` (presenter unload / live topology collect), `1c24de2` (dynamic_objects_demo Remove Item). Pushed to `origin/prep/deps-objects-assert-mcp-v1`.

Not implemented: SharedNode / transport / presence / chat / GC.

Not accepted-by-user.

# Dynamic objects demo — Add Item

## Starting point

Branch `prep/deps-objects-assert-mcp-v1` at `460ee2a` (journal retention/compaction predecessor).

## What landed

New example `examples/dynamic_objects_demo`:

```
Application → MainWindow → ItemList → Item(s)
```

- Initial graph: one `Item` (`number=1`).
- GUI Add → `AddItemCommand` (deque, not coalesced) → model `CommitAddItem`.
- `AddItemEvent` carries pre-created `Item::ptr` so replay keeps the same ObjId (`Apply` only `push_back`).
- ItemList keeps default unlimited journal retention (AddItemEvent survives restart/replay).
- Shutdown: `Application::Save()` only; no Save on Add.

### Structural publication (generic)

- `SerializeStructuralNodePublication` / `ApplyStructuralPublication`: envelope like incremental node pub, payload is `SerializeObjectGraphToBuffer` so newly referenced Item/presenter layers enter the GUI Domain.
- Field-only MainWindow updates still use `SerializeIncrementalNodePublication`.

### Presenter activation (generic)

- `Presenter::presentation_loaded`, `ReadyForPresentation()`.
- `InitializeNewPresenters` multipass: only not-yet-loaded + ready presenters get `OnLoad`.
- `InitializePresenters` delegates to it. Existing presenters are not re-OnLoad'd after Add.

### ObjId path

```
model Create Item (GenerateUnique)
→ AddItemEvent.item
→ Commit/Apply (topology)
→ SerializeStructuralNodePublication(ItemList)
→ GUI ApplyStructuralPublication (LoadRoot/shells for new ids)
→ InitializeNewPresenters → Win32ItemPresenter::OnLoad (new STATIC row)
```

Mirror Item ObjId == model Item ObjId; C++ pointers differ. Application/MainWindow/ItemList/old Item/old presenters preserve identity.

## Tests

| check | result |
| --- | --- |
| `apptraverse_dynamic_objects_add_test` (model add, replay identity, structural pub + OnLoad counts, two Adds, restart) | PASS |
| `apptraverse_publication_channel_test` | PASS |
| `apptraverse_main_window_window_changed_test` | PASS |
| `apptraverse_dynamic_objects_win32_smoke_test` (in-process Add row; child distill Add then load-only restore) | PASS |

MCP `user-apptraverse` build against this checkout: failed with `unknown target` before local reconfigure of new targets; not used as proof. Local incremental build in `build/win64-ninja-msvc-debug`.

Commits: `629162f` (structural publication / InitializeNewPresenters), `6102c03` (dynamic_objects_demo Add Item). Pushed to `origin/prep/deps-objects-assert-mcp-v1`.

Not implemented: Delete/Remove Item. Next slice after Add verification.

Not accepted-by-user.

# Dynamic Node journal retention / compaction

## Problem

Live MainWindow state accumulated 533 `WindowChangedEvent` objects. Startup replayed all of them; shutdown rewrote the whole graph. Resize itself was already RAM-only; the journal was unbounded.

## Policy API

- `JournalRetentionPolicy`: `max_events` (`0` = retain none; `kUnlimitedEvents` = unlimited count) and optional `max_age` (inclusive `now_us - retained_since_us <= max_age`).
- Default policy: unlimited (Chat/Shared unchanged).
- `SetJournalCompactionBlocked` / `IsJournalCompactionBlocked`: synchronization hold, independent of retention.
- `EventRecord::retained_since_us`: local replica acceptance time; not order/id. `Commit` / shared insert stamps `SystemUtcMicros()`. Lamport is not used as age.
- Schema: Node journal wire v2 includes `retained_since_us`. Node `Load(Version<1>)` migrates by stamping load time (conservative). MainWindow bumped to v4 (`Node` Save/Load v2); v3 Load still migrates old resize states.

## Compaction

`CompactJournal(now_us)` collapses only a contiguous unsafe prefix into `base` via existing `RebuildFromBaseAndReplay` + `CaptureBaseStateInto`, keeps retained suffix, restores Generation, suppresses materialized-change notifications.

When the retained suffix is empty (MainWindow `max_events=0`), compaction clears the journal and snapshots already-materialized live fields into `base` without reloading the old base into the live object.

MainWindow sets `max_events=0` after load; shutdown: `CompactJournal` then `Application::Save()`.

## Proof

- Count 100→10; age boundary inclusive; count+age union (3+age→7, 10+age→10); blocked then unblock; dynamic policy; shared order preserved; mid-insert while blocked; 500→0 reachable events; retention=10 reachable=10.
- Node journal v1 (`LegacyRetentionDoc`) save → load stamps `retained_since_us` → compact → reload.
- Unreferenced event dirs may remain after SaveRoot; reload does not load them.
- MainWindow 50 session commits → journal 0 / reachable WindowChangedEvent 0; direct 500 WindowChangedEvent compact → 0, then retention=10 → 10.

## Filesystem orphans

`DirectoryDomainStorage` SaveRoot does not delete previously stored event directories. Startup does not load unreferenced events. Safe GC of orphan event dirs is a separate TODO.

## Manual

Prior interactive resize verification still stands. Compaction-on-shutdown of a large existing temp state: next graceful close will compact; not re-measured by the agent. Not marked accepted.

## Tests actually run

Cursor `user-apptraverse` MCP is not bound to this checkout (`source_dir` missing). Local incremental `cmake --build --preset win64-ninja-msvc-debug`.

| target | artifact | status |
| --- | --- | --- |
| journal retention + core + shared | local cmake | ok (`journal_retention_test`, `event_sourced_core_test`, `shared_journal_test`) |
| main-window headless | local cmake | ok (`publication_channel_test`, `main_window_lifecycle_test`, `main_window_lifecycle_load_only_test`, `main_window_window_changed_test`, `main_window_missing_load_test`) |
| Win32 smoke | local cmake | ok (`main_window_win32_smoke_test`) |

`CMAKE_HOME_DIRECTORY`: `C:/Users/nickc/Projects/apptraverse-prep-deps-assert`
Build dir: `build/win64-ninja-msvc-debug` (incremental; no clean)

Incidental: `cmake/aether_object.cmake` falls back to `CPM_PACKAGE_libbcrypt_SOURCE_DIR` when `libbcrypt_SOURCE_DIR` is unset.

Not accepted-by-user.

# Persist model state only on shutdown

Runtime resize no longer calls `main_window.Save()`. `WindowChangedEvent` / `Commit` / journal / generation stay in memory. Incremental publication still uses `SerializeObjectToBuffer` (`RamDomainStorage` scratch) and does not touch `DirectoryDomainStorage`.

After `RequestStop`, before the model `Application` / Domain / storage leave scope, `Application::ptr::Save()` writes the live graph once (`DomainGraph::SaveRoot`). Not distillation. `Run` returns only after that save, so Windows `SetEvent` still means persistence shutdown finished.

Disk geometry stays the loaded snapshot until that save. A crash before graceful model shutdown may lose runtime commits. Sequence/ack fields stay unserialized.

## Proof

- Coalesce A/B/C: disk remains default until `RequestStop`; after join, geometry C, journal size 0 (post-compaction).
- Three consumed publications A then B then C: filesystem snapshot unchanged between commits; after join, geometry C, journal size 0.
- No-op seq 7: snapshot unchanged before stop; after join, journal size 0.

## Manual

Sequence/ack interactive resize: successful user verification (right/left drag no longer rolls back). Not marked accepted.

MANUAL interactive drag after removing runtime Save — not re-run by the agent. Disk writes during drag: none by test, not a measured latency number.

## Tests actually run

Cursor `user-apptraverse` MCP is not bound to this checkout. Local incremental `cmake --build --preset win64-ninja-msvc-debug`.

| target | artifact | status |
| --- | --- | --- |
| headless + Win32 smoke + demos | local `cmake --build --preset win64-ninja-msvc-debug` (no MCP artifact) | ok (`publication_channel_test`, `main_window_lifecycle_test`, `main_window_lifecycle_load_only_test`, `main_window_window_changed_test`, `main_window_missing_load_test`, `main_window_win32_smoke_test`) |

`CMAKE_HOME_DIRECTORY`: `C:/Users/nickc/Projects/apptraverse-prep-deps-assert`
Build dir: `build/win64-ninja-msvc-debug` (incremental; no clean)

Not accepted-by-user.

# Stale model feedback during live resize

## Failing interleaving

Publication of geometry A can arrive after the user has already moved the HWND to B. `OnModelChanged` compared actual B with desired A and `SetWindowPos(A)`, which echoed A back as a native command and replaced the newer pending B. Left-edge drags jumped because the stale publication also restored x.

## Fix

`WindowChangedCommand` carries a GUI-side monotonic `sequence`. Latest-state coalescing keeps the newest sequence. Every taken command, including a geometry no-op, publishes `processed_window_change_sequence` ahead of the existing MainWindow incremental payload. The GUI mirror always applies the model state. `OnModelChanged` drives the HWND only when `ack >= last_submitted`. No suppression flag.

Creation-time `WM_WINDOWPOSCHANGED` is not user input: presenter userdata is attached only after `ShowWindow`/`UpdateWindow`. Teardown clears userdata before `DestroyWindow` instead of nulling `hwnd`.

PublicationChannel remains single-unread. The model cannot publish the next buffer until `TakePublishedCopy` releases the slot, so one notify message still matches one buffer. Sequence is application protocol, not a channel field.

## Counts from tests

- Coalesce seq 1/2/3 before processing: 3 commands, 1 committed `WindowChangedEvent`, 1 incremental publication, journal size 1, ack=3.
- No-op seq 7: journal unchanged, generation unchanged, no Save, ACK publication received, `publish_count` 1 → 2.
- Stale A then B: mirror becomes A while native-emulation stays B; then ack 2 applies B. Same Application/MainWindow/Presenter. `OnLoad` once. Journal size 2.

## Tests actually run (local runner, not attached MCP)

Verified tree before this fix: `84c23688319bf5425e5f51ad983f68066a0d0c3c`.
Commit / push: `f3a74c53bc3531920bb3a0937615da676272db83` on `origin/prep/deps-objects-assert-mcp-v1`.

Cursor `user-apptraverse` MCP is not bound to this checkout (`source_dir` missing). This turn's MCP `apptraverse_build_start` (`20260909-044756-5b4ff3`) failed immediately with `unknown target 'apptraverse_main_window_headless_check'` and did not write an artifact here. Local incremental `cmake --build --preset win64-ninja-msvc-debug` (same targets, MSVC env) is the verification.

MANUAL INTERACTIVE DRAG — BLOCKED / requires user verification. Automated `SetWindowPos` burst (including left+width) is not a mouse-drag proof. Do not treat jitter as fixed from unit tests alone.

| target | artifact | status |
| --- | --- | --- |
| headless + Win32 smoke + demos | local `cmake --build --preset win64-ninja-msvc-debug` (no MCP artifact) | ok (`publication_channel_test`, `main_window_lifecycle_test`, `main_window_lifecycle_load_only_test`, `main_window_window_changed_test`, `main_window_missing_load_test`, `main_window_win32_smoke_test` including left-edge burst then load-only C) |

`CMAKE_HOME_DIRECTORY`: `C:/Users/nickc/Projects/apptraverse-prep-deps-assert`
Build dir: `build/win64-ninja-msvc-debug` (incremental; no clean)

Not accepted-by-user.

# WindowChanged round-trip

## Finding

A most-derived factory load does not walk ancestor layers; that was already fixed in aether-objects. This iteration adds the first functional event path on top of that load.

Derived `load` still does not replace App Traverse command/event semantics. Native geometry must be copied into a plain command, committed as `WindowChangedEvent` on the model thread, and published as one changed MainWindow.

## Semantics

- `WindowChangedCommand` (`main_window_lifecycle.h`): four `int32_t`, latest-state under `ModelSession::mu`. Newer geometry replaces an older pending command.
- Model waits on `stop` or (pending command and publication slot free). Stop wins if both are ready.
- Unchanged geometry does not commit an event or change generation. Later resize-ack iteration still publishes the processed sequence (see above).
- `WindowChangedEvent` (`main_window_model.h`) is committed on `MainWindow`. Journal gets one event per applied command. `MainWindow::Save()` persists it.
- Incremental envelope: object id, generation, payload length, `SerializeObjectToBuffer` payload. GUI mirror journal/base stay empty. Generation is adopted.
- Existing presenter is held across deserialize so the native instance is not replaced.
- Publication backpressure is unchanged: unread publication is not overwritten. GUI `TakePublishedCopy` under `session.mu`, then `cv.notify_all()`, then apply.
- `PublicationKind::{Initial,Incremental}` chooses the Win32 notify message. GUI does not guess from bytes.
- Win32 source is `WM_WINDOWPOSCHANGED` + `GetWindowRect`. Feedback stops by comparing geometry. No suppression flag.
- aether-objects pin unchanged: `81d5f86f3d6184f86763b4556334dda9471bfd4a`

## Coalescing / no-op / restart

- Coalescing test journal size: 1 `WindowChangedEvent` (commands A/B/C collapsed to C).
- No-op command does not change generation or persist a new `EventRecord`.
- Restart load-only initial publication restores the committed geometry.

## Tests actually run (local runner, not attached MCP)

Cursor `user-apptraverse` MCP still has no `source_dir` (**BLOCKED**).

| target | artifact | status |
| --- | --- | --- |
| headless + demos | `apptraverse-build/20260909-042307-cd233d` | ok (`publication_channel_test`, `main_window_lifecycle_test`, `main_window_lifecycle_load_only_test`, `main_window_window_changed_test`, `main_window_missing_load_test`) |
| Win32 smoke | `apptraverse-build/20260909-042240-bce900` | ok (`main_window_win32_smoke_test`, including resize then load-only restored rect) |

`CMAKE_HOME_DIRECTORY`: `C:/Users/nickc/Projects/apptraverse-prep-deps-assert`
Build dir: `build/win64-ninja-msvc-debug` (incremental; no clean)

Not accepted-by-user.

# Load persisted ancestor layers into aether-objects

## Finding

Derived factory `load` is `DomainGraph::Load<T>` for that class only. `LoadVersion` returns immediately when that class has no stored bytes, so it never walks `AE_REF_BASE`. `GetMostRelatedFactory` already filters unknown classes and sorts the known stored chain base→derived, then creates a further registered descendant. Ancestor state was missing because only that descendant's factory `load` ran. The fix belongs in `DomainGraph::LoadRoot` / `LoadCopyImpl`, reusing that chain.

## aether-objects

- Branch: `fix/load-ancestor-layers-v1`
- Base: `68df7973014fdd366875b3af725a69750a847e8b`
- Final / remote: `81d5f86f3d6184f86763b4556334dda9471bfd4a`
- `LoadRoot` and `LoadCopyImpl` now load known stored class layers base → derived
- Unknown classes stay filtered. Version handling inside a class layer is unchanged
- Tests: `test-object-system` (including new ancestor-layer cases and existing version tests) exit 0

## AppTraverse

- Branch: `prep/deps-objects-assert-mcp-v1`
- Pin: `68df7973014fdd366875b3af725a69750a847e8b` → `81d5f86f3d6184f86763b4556334dda9471bfd4a`
- Configure log: `APPTRAVERSE_aether-objects_SHA=81d5f86f3d6184f86763b4556334dda9471bfd4a`
- Removed `LoadStoredAncestorLayers` and `LoadStoredAncestorLayersFromRoot` (declarations, definitions, model and GUI calls)
- `plan.md` TODO closed
- Commit: `688cfb7b0796d37ecdef0fae8855a1a4e0dea644`

## Tests actually run (local runner, not attached MCP)

Cursor `user-apptraverse` MCP still has no `source_dir` (**BLOCKED**).

| target | artifact | status |
| --- | --- | --- |
| demos + headless + Win32 smoke | `apptraverse-build/20260909-035302-dfe39b` | ok (`publication_channel_test`, `main_window_lifecycle_test`, `main_window_lifecycle_load_only_test`, `main_window_missing_load_test`, `main_window_win32_smoke_test`) |

`CMAKE_HOME_DIRECTORY`: `C:/Users/nickc/Projects/apptraverse-prep-deps-assert`
Build dir: `build/win64-ninja-msvc-debug` (incremental; no clean)

Not accepted-by-user.

# Model lifecycle without Win32 notifications; Win32 fatal helper

## Identity

- Branch: `prep/deps-objects-assert-mcp-v1`
- Worktree / source: `C:\Users\nickc\Projects\apptraverse-prep-deps-assert`
- Base SHA: `a0629ab9d992aafff9ad55803ffa8bdb93409f2d`
- `CMAKE_HOME_DIRECTORY`: `C:/Users/nickc/Projects/apptraverse-prep-deps-assert`
- Build dir: `build/win64-ninja-msvc-debug`
- Profile: `win64-ninja-msvc-debug` (incremental; no clean/rebuild)

## ModelSession API before / after

Before:

- `state_dir`, `PublicationChannel`, `mu`/`cv`, `stop`, `RequestStop`, `Run()`
- `notify_hwnd` / `done_event` on the shared session
- `windows.h` / `_WIN32` / `PostMessageW` / `SetEvent` in `main_window_lifecycle.cpp`

After:

- only `state_dir`, `PublicationChannel`, `mu`, `cv`, `stop`, `RequestStop`, `Run(std::function<void()> on_published)`
- no `HWND`/`HANDLE`/`void*`/`uintptr_t` platform stand-ins
- `on_published` is required (no default, no `if (callback)`); called on the model thread after publish, without holding `mu`
- return from `Run` means Application, reachable graph, Domain, and storage are already destroyed on the model thread

## Where Win32 notification and completion live

- `WM_APPTRAVERSE_PUBLISHED` / `WM_APPTRAVERSE_STOP`: `windows/main_window_win32_messages.h` (presenter does not include WinApp)
- notify HWND: `WinApp` (`notify_`), created before the model thread, alive until join
- completion event: local `HANDLE` in `WinApp::Run`, created before the thread, closed after join
- publication: short lambda captures notify HWND and calls `PostMessageW`; fatal via `FatalWin32` on failure
- `SetEvent(done_event)` is in the Windows thread lambda after `Run` returns, not in `ModelSession`

Order: model destruction (scope end) → `Run` returns → `SetEvent` → GUI sees completion → one `join` → `UnloadPresenters` → GUI graph/Domain destroyed → DestroyWindow notify / UnregisterClass / CloseHandle.

## Mutex / cv protocol (unchanged)

- `RequestStop`: lock `mu`, set `stop`, unlock, `cv.notify_all()`
- serialize outside `mu`; under `mu`: `NotePublished` + `PublishProducer`; unlock; `cv.notify_all()`; then `on_published()`
- model wait: predicate `stop` under the same `mu`
- `stop` remains a plain `bool`, not atomic

## Diagnostics

- `FatalWin32(operation, DWORD)` in the Windows example; `GetLastError` captured at the failing call
- teardown result checks: `DestroyWindow` Main/Loading/notify, `CloseHandle(done_event)`; DestroyWindow Main failure does not continue to `UnregisterClassW`
- `WriteFatalStderr`: one write — CRT stderr when attached, otherwise `STD_ERROR_HANDLE`. No unconditional dual print.

## What NDEBUG actually covers

- `apptraverse_main_window_missing_load_test` is a **Debug** binary (not NDEBUG). It checks missing Application: non-zero exit, `fatal: LoadApplication failed` once, publication callback not reached. It does not prove Release/NDEBUG of the application.
- `apptraverse_win32_fatal_ndebug_child` is compiled with `-DNDEBUG`, links only `FatalWin32` + `WriteFatalStderr`, no aether-objects. Parent `apptraverse_win32_fatal_ndebug_test` checks `fatal: RegisterClassW Main GetLastError=5` once, non-zero exit, no execution after fatal. That proves the helper, not the whole app. No full Release rebuild of dependencies.

## Tests actually run (this checkout, local runner)

Cursor `user-apptraverse` MCP schema still has no `source_dir` (**BLOCKED**). Results below are the local `tools/runners/run_apptraverse_build.py` runner, not attached MCP stdio.

| target | artifact | status |
| --- | --- | --- |
| demos + `apptraverse_main_window_headless_check` | `apptraverse-build/20260909-013026-1737b7` | ok (`publication_channel_test`, `main_window_lifecycle_test`, `main_window_lifecycle_load_only_test`, `main_window_missing_load_test`) |
| `apptraverse_main_window_win32_smoke_check` + `apptraverse_win32_fatal_ndebug_check` | `apptraverse-build/20260909-013107-b228ae` | ok (`win32_fatal_ndebug_test OK`, `main_window_win32_smoke_test OK`) |

Exes (this build tree, not a neighboring checkout):

- `build/win64-ninja-msvc-debug/tests/apptraverse_main_window_lifecycle_test.exe`
- `build/win64-ninja-msvc-debug/tests/apptraverse_main_window_lifecycle_load_only_test.exe`
- `build/win64-ninja-msvc-debug/tests/apptraverse_main_window_missing_load_test.exe`
- `build/win64-ninja-msvc-debug/tests/apptraverse_win32_fatal_ndebug_child.exe` (`DEFINES = -DNDEBUG`)
- `build/win64-ninja-msvc-debug/tests/apptraverse_win32_fatal_ndebug_test.exe`
- `build/win64-ninja-msvc-debug/tests/apptraverse_main_window_win32_smoke_test.exe`
- `build/win64-ninja-msvc-debug/examples/main_window_runtime_demo/windows/win32_main_window_runtime_demo.exe`
- `build/win64-ninja-msvc-debug/examples/main_window_runtime_demo/windows/win32_main_window_runtime_demo_load_only.exe`

## Limitations / TODO

- `LoadStoredAncestorLayers` still in App Traverse (`plan.md`)
- Do not restore close-during-Loading
- MCP attached tools remain BLOCKED (no `source_dir`)
- Not accepted-by-user

# Main-window startup registration cleanup — progress

## Identity

- Branch: `prep/deps-objects-assert-mcp-v1`
- Worktree: `C:\Users\nickc\Projects\apptraverse-prep-deps-assert`
- Status: implemented, verified. Not accepted.

## Registration before / after

Before:

- `main.cpp`: `EnsureMainWindowRegistration()` then `EnsureWin32MainWindowPresenterRegistration()`
- tests: `EnableNoninteractiveCrt()` then those wrappers
- both wrappers only called `EnsureObjectRegistration()`

After:

- one process-startup call: `EnsureObjectRegistration()` (enables CRT, then pulls library registrars)
- `EnsureMainWindowRegistration` / `EnsureWin32MainWindowPresenterRegistration` deleted
- model thread and `WinApp::Run` do not register
- `APPTRAVERSE_REGISTER(Application/MainWindow/MainWindowPresenter)` moved into `main_window_lifecycle.cpp` so both distill and load-only executables link them without a second Ensure* (that TU is a direct source of every consumer)
- `Win32MainWindowPresenter` registrar stays in `win_presenters.cpp` (direct exe/smoke source)
- `lifecycle.h` `#undef RegisterClass` after `windows.h` so `ae::Registry::RegisterClass` is not rewritten to `RegisterClassA`

## Helpers removed / inlined

- deleted the two Ensure* wrappers (decls + defs)
- inlined smoke `PreparePersistedState` into the load-only child-process test
- not inlined: `RequestStop` (WM_CLOSE after Main, WM_QUIT after Main, headless/smoke tests)
- not inlined: `WaitPublished` / `TestDir` / `LoadUiFromSession` / `PersistFixture` / `PersistedApplicationExists` (reused in tests)
- not inlined: `RegisterWindowClasses`, `PaintLoading`, `WndProc`, `OnPublished`

## ModelSession (kept)

Still the shared Win32 + headless model-thread path:

- `state_dir`, `PublicationChannel`, `mu`/`cv`, `stop`, `RequestStop`, `Run`
- `notify_hwnd` / `done_event` (WinApp vs headless that never sets them)

No stage, getters, or test instrumentation added.

## WM_QUIT

While `loading_ != nullptr`, `WM_QUIT` is dropped (startup is not cancelable). After `OnPublished` sets `loading_ = nullptr`, `WM_QUIT` may `RequestStop()`. `WM_CLOSE` on Loading is still ignored. No deferred-quit queue.

## Distill / load-only

Unchanged architecture. Distill bootstrap remains `#ifdef APPTRAVERSE_ENABLE_DISTILLATION`. Load-only lifecycle has no missing-state branch, no `BuildMainWindowGraph` / `FinalizeDistilledGraph` / `SaveDistilledRoot`. Missing Application is fatal (`LoadApplication` assert).

## Remaining exceptions (not this slice)

- `LoadStoredAncestorLayers` / `LoadStoredAncestorLayersFromRoot` after LoadRoot — TODO in `plan.md` (aether-objects)
- `if (notify hwnd)` / `if (done_event)` — real WinApp vs headless branch
- WndProc `app == nullptr` before `WM_NCCREATE`
- `loading_ == nullptr` as the “Main exists” flag for close/quit
- Cursor `user-apptraverse` MCP schema still has no `source_dir` (**BLOCKED**). Local runner is not attached MCP success.

## Tests / artifacts

Worktree runner, incremental, no clean/rebuild:

| target | run_id / artifact | status |
| --- | --- | --- |
| demos + headless + Win32 smoke | `apptraverse-build/20260908-220020-c095cc` | ok (`publication_channel_test`, `main_window_lifecycle_test`, `main_window_lifecycle_load_only_test`, `main_window_win32_smoke_test`) |

## Commits / push

- `cedf9fa7e193e3fcc5a01d71f73e9e68c31a3aab` — Simplify main-window startup and registration

Pushed to `origin/prep/deps-objects-assert-mcp-v1`. `main` not changed.

# Coding-agent rules — progress


## Identity

- Branch: `prep/deps-objects-assert-mcp-v1`
- Status: implemented, verified. Not accepted.

## Where the rules live

Existing format: `.cursor/rules/*.mdc` (same as `apptraverse-headless-chat-tests.mdc`).
No `AGENTS.md` in this repo; none was added.

Added always-apply:

- `.cursor/rules/apptraverse-coding-agent.mdc`

Left unchanged: `.cursor/rules/apptraverse-headless-chat-tests.mdc`.

No application rebuild (instructions/docs only). No clean/rebuild.

## Current skeleton review (next iterations; not fixed here)

Not treated as violations:

- `EnsureMainWindowRegistration` + `EnsureWin32MainWindowPresenterRegistration` in `main()` — one startup point, two TUs
- `if (notify hwnd)` / `if (done_event)` — WinApp vs headless
- `if (app == nullptr)` in WndProc before `WM_NCCREATE`
- `RegisterWindowClasses` / `PaintLoading` — allowed Win32 groups / WndProc
- Distill bootstrap is behind `APPTRAVERSE_ENABLE_DISTILLATION`
- Close-during-Loading tests/API are gone; Loading has no system Close

Recorded for later:

- `WM_QUIT` in the GUI loop still calls `RequestStop()` even before Main exists, so startup is not fully non-cancelable
- Tests (and smoke) still call `EnableNoninteractiveCrt()` and then `Ensure*`, which also enables CRT via `EnsureObjectRegistration`
- Ancestor-layer reload remains an App Traverse pass after LoadRoot (already TODO in `plan.md`)
- Cursor `user-apptraverse` MCP still has no `source_dir` (**BLOCKED**)

## Commits / push

- `dcc14902fb6828582d1d7d123faa30ac45fd6c64` — Add App Traverse coding-agent rules

Pushed to `origin/prep/deps-objects-assert-mcp-v1`. `main` not changed.

# Main-window startup simplify and distill/load-only split — progress

## Identity

- Branch: `prep/deps-objects-assert-mcp-v1`
- Worktree: `C:\Users\nickc\Projects\apptraverse-prep-deps-assert`
- Status: implemented, verified. Not accepted.

## Removed

- `ModelStartupStage`, `EnterStage`, `hold_stage`, `SetHoldStage`, `--hold-stage`
- Cancelable startup and close-during-Loading (tests and production)
- Test instrumentation on `ModelSession`: `published`, `finished`,
  `distilled_this_run`, `stage`, thread ids, model object addresses/ids/geometry
- `ApplicationStateExists` helper; `accept_input_`; `stop_requested_`;
  `GuiPresenterClassId`; `DestroyGuiMirror` wrapper
- Duplicate registration / `EnableNoninteractiveCrt` inside `WinApp::Run` and
  the model thread

## ModelSession (kept)

Shared production model-thread path for Win32 and headless tests:

- `state_dir`, `PublicationChannel`, `mu`/`cv`, `stop`, `RequestStop`, `Run`
- `notify_hwnd` / `done_event` (WinApp vs headless that never sets them)

## Dev / load-only

- Compile definition: `APPTRAVERSE_ENABLE_DISTILLATION`
- `win32_main_window_runtime_demo` — distill-enabled; bootstrap if state missing
- `win32_main_window_runtime_demo_load_only` — no definition; load only; missing
  Application is fatal
- Lifecycle `.cpp` is compiled per target (not one STATIC lib), so the ifdef is
  real. Load-only lifecycle obj has `LoadApplication` and no
  `FinalizeDistilledGraph` / `SaveDistilledRoot` / `BuildMainWindowGraph`.

Registration: `main()` registers model + Win32 presenter once, then `WinApp::Run`.

## Tests / artifacts

Cursor `user-apptraverse` still has no `source_dir`. **BLOCKED**.
Worktree runner (incremental, cmake regenerated ninja; no clean/rebuild):

| target | run_id / artifact | status |
| --- | --- | --- |
| simplify startup | `apptraverse-build/20260908-213039-bcd573` | ok |
| distill + load-only + smokes | `apptraverse-build/20260908-213326-84d24d` | ok (`publication_channel_test`, `main_window_lifecycle_test`, `main_window_lifecycle_load_only_test`, `main_window_win32_smoke_test`) |

Missing-state fatal is the load-only child-process smoke (non-zero, no Main, no Application object).

## Commits / push

- `628174df8a5b006b9d783a331751a829034e671f` — Simplify main-window startup and remove test state machine
- `1e91b0e15c484ed4fa2584d694dcfc26a06ea415` — Split development distillation and load-only targets

Pushed to `origin/prep/deps-objects-assert-mcp-v1`. `main` not changed.

# Main-window lifecycle defensive-check cleanup — progress

## Identity

- Branch: `prep/deps-objects-assert-mcp-v1`
- Worktree: `C:\Users\nickc\Projects\apptraverse-prep-deps-assert`
- Status: implemented, verified. Not accepted.

## Removed checks (unreachable or already guaranteed)

- `Win32MainWindowPresenter::OnLoad`: no `hwnd != nullptr` early return, no
  `!window` check, no `assert(hwnd == nullptr)` / `assert(window)`, no
  bool-return recovery if `CreateWindowExW` fails.
- `DestroyNative` / destructor `DestroyWindow`: gone. Native teardown is
  `OnUnload` only, called from `UnloadPresenters` after successful GUI init.
  Model-side Win32 objects never run that pass, so the destructor does not
  guess whether an HWND exists.
- `presentation_initialized` and per-presenter "already initialized" / second
  `OnLoad` protection.
- `InitializePresenters` bool return and "OnLoad failed" recovery.
- `OnPublished`: empty-bytes return, stacked graph asserts, init-failure
  abort-to-Loading, `assert(loading_)` before destroying Loading.
- Startup recovery (`AbandonStartup`, `exit_code_`, `joinable()`, repeated
  `assert(notify_)` / `assert(done_event)` at shutdown).
- WndProc `assert(msg == WM_GETMINMAXINFO)` when `app == nullptr`.
- ModelSession duplicate `assert(buffer)` / `assert(presenter)` after the
  stage that already produced them.
- Tests: idempotent second `InitializePresenters`; `TestPresenterInitFailure`
  (recoverable OnLoad failure is not a supported state).

## Kept `if`s (real state-machine branches)

- `InitializePresenters` / `UnloadPresenters`: `dynamic_cast` skip of
  non-Presenter objects on the reachable walk.
- `OnPublished`: `stop_requested_` — publication can arrive after close during
  Loading; skip presentation init.
- `RequestStop`: already requested (Loading and Main can both send WM_CLOSE).
- `loading_ != nullptr` at shutdown: close during Loading vs after Main replaced
  it. Success path destroys Loading then sets `loading_ = nullptr` because
  that nullable handle is the two-stage machine.
- `ui_application_` before `UnloadPresenters`: graph exists only after a
  completed presentation pass; close during Loading never loaded it.
- WndProc: `WM_NCCREATE` binds userdata; `app == nullptr` is legal before that
  (`WM_GETMINMAXINFO`); after bind, dispatch to `Handle`.
- Message loop: `WAIT_OBJECT_0` vs queued input; `WM_QUIT`.
- ModelSession: `EnterStage` / `stop`; first launch vs existing state;
  `done_event` / notify HWND (WinApp vs headless tests that never set them).

## Fatal (unrecoverable) conditions

- `CreateEventW` for `done_event` returns null.
- `CreateWindowExW` for notify or Loading returns null.
- `CreateWindowExW` for Main returns null (`OnLoad`).
- `std::thread` construction throws (uncaught; never enters the loop).

No recovery, no keep-Loading, no presenter-without-HWND, no bool `OnLoad`.

## Tests

Headless: startup, existing-state, mirror identity, most-derived Test
presenter, `InitializePresenters` once then `UnloadPresenters` once,
model-side OnLoad/OnUnload not called, object destructor does not `OnUnload`,
thread ownership, stop during Loading, stop after Ready.

Win32 smoke: Loading then Main; GUI class `Win32MainWindowPresenter`; close
during Loading does not create Main; GUI teardown destroys Main HWND.

## MCP / artifacts

Cursor `user-apptraverse` still has no `source_dir`. **BLOCKED**.
Worktree runner (incremental, no clean/rebuild):

| target | run_id / artifact | status |
| --- | --- | --- |
| `apptraverse_main_window_headless_check` + `apptraverse_main_window_win32_smoke_check` | `apptraverse-build/20260908-211920-2b7853` | ok (`publication_channel_test OK`, `main_window_lifecycle_test OK`, `main_window_win32_smoke_test OK`) |

Did not rebuild `apptraverse_event_sourced_core_test`: it pulls the full Aether
client (sodium) and is outside this slice. `presenter.h` there is only a
class-id check.

## Commits / push

- `c0008de19a234eda79b63bb7c08e316f5bcf763b` — Remove defensive state checks from main-window lifecycle

Pushed to `origin/prep/deps-objects-assert-mcp-v1`. `main` not changed.

# Object-graph presenter — progress

## Identity

- Base SHA: `9bae06bd881e043b4f67ded7a3d731e68726dfa5`
- Branch: `prep/deps-objects-assert-mcp-v1`
- Worktree: `C:\Users\nickc\Projects\apptraverse-prep-deps-assert`
- Status: implemented, verified. Not accepted.

## App Traverse library changes

- `LoadInitialPublication` injects serialized layers then calls
  `DomainGraph::LoadRoot` so aether-objects picks the most-derived registered
  factory. No App Traverse preferred-class map.
- Distilled `Node::base` object layers are omitted from UI publication buffers.
- After LoadRoot, `LoadStoredAncestorLayers` loads stored ancestor class
  layers onto the already-constructed most-derived object (needed when
  persisted data has only `MainWindowPresenter` and the registry created
  `TestMainWindowPresenter` / `Win32MainWindowPresenter`).
- `InitializePresenters` walks reachable live objects from the GUI root and
  calls `Presenter::OnLoad()` once. Object Load does not call it.
- `Presenter` documents OnLoad/OnUnload as GUI presentation hooks; runtime-only
  `presentation_host` is not serialized. `presentation_initialized` was removed
  in the defensive-check cleanup (init runs once per GUI mirror).

## Example changes

- Graph: `Application` → `MainWindow` (Node, schema v3: x,y,width,height,presenter)
  → `MainWindowPresenter` (not Node) → `Win32MainWindowPresenter` (HWND runtime-only).
- Cycle: `presenter->window` is the same MainWindow in that Domain.
- `WinApp` no longer owns a presenter member or calls `Create`. It loads the
  GUI graph, runs `InitializePresenters`, then destroys Loading. No
  `dynamic_cast` to Win32. If `RequestStop` already happened, skip init.
- Model-side most-derived Win32 presenter exists after Load and does not create HWND.

## Tests

Headless `apptraverse_main_window_lifecycle_test` (no Win32):

- A. Neutral `MainWindowPresenter` in the buffer + registered
  `TestMainWindowPresenter` → LoadRoot materializes Test
- B. OnLoad not called after model load / serialize / GUI deserialize
- C. `InitializePresenters` once; `presenter.window` resolved; back-pointer
- D. Model vs GUI: same ObjIds, different addresses/Domains
- E. Cycle walk terminates; dropping root+keepalive does not leak the GUI graph
- F. Model-side Test exists after LoadApplication; OnLoad not called there

Windows smoke (existing checks plus):

- GUI presenter class is `Win32MainWindowPresenter`
- exactly one Main window
- close during Loading does not create Main

## MCP jobs / artifacts

Cursor `user-apptraverse` schema still has no `source_dir` argument.
**BLOCKED** for that attached MCP process. Local incremental runner is not
claimed as MCP success.

Worktree runner `tools/runners/run_apptraverse_build.py` (MSVC env, stage=build,
no clean/rebuild):

| target | run_id / artifact | status |
| --- | --- | --- |
| `apptraverse_main_window_lifecycle_test` | `apptraverse-build/20260908-203315-e7a483` | ok (test exe OK) |
| `apptraverse_main_window_headless_check` + `apptraverse_main_window_win32_smoke_check` | `apptraverse-build/20260908-203339-eb1692` | ok |

## Known limitations

- Cursor user-level MCP is still the original checkout binary (no `source_dir`).
- Ancestor-layer reload is an App Traverse pass after LoadRoot; aether-objects
  `Load(Derived)` does not itself load ancestor class layers when the derived
  class has no stored version.
- Presenter local state (DPI, monitor, scroll) is not implemented.
- `main` was not changed.

## Commits / push

1. `86f5d38019c88a5397cb8dc19d3efcf8971bbca4` — Use aether-object descendant resolution for GUI mirror loading
2. `63a6eff6080d883819855ce28c9459af065c8e52` — Move main window presentation into object graph

Pushed to `origin/prep/deps-objects-assert-mcp-v1`. `main` not changed. History not rewritten.

## git status --short

Untracked caches only (not committed):

```
?? tools/mcp/__pycache__/
?? tools/runners/__pycache__/
?? tools/runtime/__pycache__/
```

# MCP worktree-aware — progress

## Problem

User-level MCP (`user-apptraverse`) launches `tools/mcp/apptraverse_mcp.py` from the
checkout that contains that file. `repo_root()` was `Path(__file__).resolve().parents[2]`.
Build/test tools therefore always used that original tree, even when Cursor was
opened on another git worktree.

Observed against this worktree, from the still-running original MCP process:

- job `20260908-195143-ed5b76` / artifact `apptraverse-jobs/20260908-195143-ed5b76`
- nested `apptraverse-build/20260908-195145-6ee81a`
- `ninja: error: unknown target 'apptraverse_main_window_headless_check'`
- response had no `source_dir` (stale server schema)

Cursor MCP config was not rewritten. No per-worktree MCP server was registered.

## API

Optional `source_dir` on START (and on excerpt/log query, which read artifacts).
STATUS/CANCEL/STOP take `job_id` / `process_id` only; the server remembers the
canonical checkout in `.artifacts/mcp-source-index/` under the MCP server tree
and in `job.json` / `request.json` / `process.json` of the selected checkout.

Omitted `source_dir` keeps the previous default: the checkout that launched the
server. Never cwd.

Invalid / missing / non-App-Traverse paths return `failure_kind=invalid_source_dir`
and `state=failed` (no Python exception). Relative paths are rejected.

Tools with `source_dir`:

- `apptraverse_build_start` / `status` / `cancel` / `failure_excerpt`
- `apptraverse_platform_start` / `status` / `cancel` / `failure_excerpt`
- `apptraverse_process_start` / `status` / `stop`
- `apptraverse_chat_headless_test_start`
- `apptraverse_chat_p2p_headless_test_start`
- `apptraverse_runtime_log_query`

## Commit SHA

`5699c613e55f66ccea6eece1f2009f28d8dd6f26` — Make App Traverse MCP worktree-aware

## Unit tests

`python -m unittest tools.mcp.test_apptraverse_mcp tools.runners.test_run_apptraverse_job tools.runners.test_run_apptraverse_platform_job`

- omitted `source_dir` → `start_job(repo_root(), …)`
- explicit fixture `source_dir` → runner receives that path, not MCP `repo_root()`
- two roots: status/excerpt do not mix jobs or same-run-id artifacts
- missing path / non-App-Traverse dir / relative path → `invalid_source_dir`
- existing MCP tests remain green

## Real MCP jobs against this worktree

`source_dir=C:\Users\nickc\Projects\apptraverse-prep-deps-assert`
profile `win64-ninja-msvc-debug`

The attached Cursor `user-apptraverse` process is still the original checkout
binary and ignores `source_dir`. Proof of the new server used this worktree's
`apptraverse_mcp.py` over MCP stdio (cwd `C:\Temp`) and the same tool functions.

| target | job ID | artifact ID | nested build artifact | status |
| --- | --- | --- | --- | --- |
| `apptraverse_main_window_headless_check` | `20260908-195713-0fef9b` | `apptraverse-jobs/20260908-195713-0fef9b` | `apptraverse-build/20260908-195714-45078a` | ok |
| `apptraverse_main_window_lifecycle_test` | `20260908-195737-53326d` | `apptraverse-jobs/20260908-195737-53326d` | `apptraverse-build/20260908-195739-d8ba7b` | ok |
| `apptraverse_main_window_win32_smoke_check` | `20260908-195739-1c8eb3` | `apptraverse-jobs/20260908-195739-1c8eb3` | `apptraverse-build/20260908-195741-ce8814` | ok |
| stdio preflight (cwd Temp) | `20260908-195928-053c6d` | `apptraverse-jobs/20260908-195928-053c6d` | — | ok |

Canonical `source_dir` in job metadata and public payloads:
`C:\Users\nickc\Projects\apptraverse-prep-deps-assert`

The headless-check target exists only in this worktree; the stale original MCP
job failed with unknown target. After `source_dir`, ninja found the target here.

Assert/crash path was not regressed: these jobs completed `ok` through the
existing noninteractive worker (no assert dialog).

# Main-window skeleton — progress

## Identity

- Base SHA: `24daa80dfb39edfcc1261cd15df2d62d1ce32fa9`
- Branch: `prep/deps-objects-assert-mcp-v1`
- Worktree: `C:\Users\nickc\Projects\apptraverse-prep-deps-assert`

## Commits

1. `e9bca9adfeba9b0c111486276646b1265a97452e` — Fix PublicationChannel TakePublished so a consumer buffer cannot be cleared.
2. `6a58e2074a108ca85599b6cd191ef9982f8011ea` — Add serialized initial publication without GUI-thread model graph copies.
3. `0f2540bc1d55e8853a0851376ba31e34fb7c9c20` — Add Loading/main-window example with a dedicated model thread.
4. `57f4fc30db7b75354d82085e81924f3ef3b520fc` — Add headless lifecycle and Win32 smoke tests for the main window.
5. `1a078f9f9a6b1f907749d1a8d5f6fbca7da3c0da` — Record the main-window skeleton plan and progress.
6. `75a1af396b86237bf6c6b770a8db50e4ef2a159f` — Name the documentation commit in Progress.md.
7. `4135ca5afa46b7f731afa3ba295ff1baf63aab27` — Remove premature DPI state from main window skeleton.

## DPI cleanup

DPI was removed from the new skeleton because `plan.md` defers DPI/screen system events to a later stage. This slice only needs Loading → model thread → serialized mirror → Main → shutdown.

Removed from the example and its tests:

- `MainWindow::dpi`
- reflection / Load / Save of DPI
- `kDefaultDpi`
- `model_window_dpi`
- DPI assertions

`MainWindow` now has only `x`, `y`, `width`, `height`. Schema version is 2; v0 and v1 Load throw (no conversion). Tests use clean temp state directories. `plan.md` later-stage DPI/screen architecture is unchanged.

## App Traverse library changes

- `PublicationChannel::TakePublished` now CAS-marks `in_ui_` before clearing `published_`, so `AcquireProducer` cannot wipe the buffer the consumer is reading. Added `TakePublishedCopy()`.
- `SerializeInitialPublication` / `LoadInitialPublication`: root ObjId + existing graph-fragment bytes. GUI Domain creates shells from the buffer; it does not read model objects.
- `EnableNoninteractiveCrt()` hooked from `EnsureObjectRegistration()` so Debug asserts abort to stderr instead of a modal dialog.
- `apptraverse` PUBLIC-links `aether::objects` + `aether::miscpp` only. Full `aether` is linked by chat/presence/model-ui targets that include `aether/clock.h` / `aether/all.h`. This slice does not compile the network client.

## Example-only changes

`examples/main_window_runtime_demo/`:

- `Application` → `MainWindow` (Node) with `x, y, width, height`
- `ModelSession::Run` on the model thread
- Win32 `WinApp`: Loading HWND, notify HWND, presenter, empty Main HWND

## Model lifecycle

Fresh state (model thread only):

1. create storage + Domain + graph
2. distill (`FinalizeDistilledGraph` + `SaveDistilledRoot`)
3. drop Application + Domain + storage
4. new storage + Domain in the same thread
5. `LoadApplication`
6. serialize into `PublicationChannel<3>` and notify GUI

Existing state: skip 1–3; load and publish.

Stop (`RequestStop` + `cv.notify_all`) is checked at each stage, including mid-serialize before `PublishProducer`. Cleanup always runs on the model thread: Application, Domain, storage, then `SetEvent(done_event)`.

## Initial publication format

`uint32 root_id` followed by `SerializeObjectGraphToBuffer` (layer count, per-layer obj/class/version/bytes, then Node generation table). GUI `LoadInitialPublication` injects layers into RAM storage, creates shells using a registered factory (skips `ae::Obj` base layers), loads, and `FinalizeUiNodeState` (clears `base` and `journal`).

## Domain ownership

- Model Domain + `DirectoryDomainStorage`: model thread only. Never passed to GUI.
- GUI Domain + `RamDomainStorage`: GUI thread (or the test consumer thread) after `TakePublishedCopy`.
- Same ObjIds, different C++ addresses, different Domains. Observation atomics store integer addresses for tests; GUI must not dereference them.

## MCP / artifacts

User-level `user-apptraverse` MCP is bound to the original App Traverse checkout, not this worktree.

- MCP job `20260908-193409-2f3650` / artifact `apptraverse-jobs/20260908-193409-2f3650` → failed: `unknown target 'apptraverse_main_window_headless_check'`
- Nested MCP build artifact `apptraverse-build/20260908-193411-af0768`

Local runner in this worktree (authoritative for this slice):

- DPI-cleanup build `apptraverse-build/20260908-193412-68f37e` status=ok

## Headless tests

Profile `win64-ninja-msvc-debug`.

- `apptraverse_main_window_lifecycle_test` — OK after DPI removal
  - fresh: distill + destroy Domain + reload + initial publication
  - existing: no second distill
  - initial mirror: same ObjIds, different addresses/Domains, same MainWindow bounds, UI `base` invalid and journal empty
  - thread ownership: create/destroy on model thread; GUI copy on consumer thread
  - stop while Distilling: join completes, `published` stays false
  - stop after Ready: join completes
- Synchronization is `condition_variable` waits, not `sleep()` as proof.

## Windows smoke

`apptraverse_main_window_win32_smoke_test` — OK after DPI removal (`main_window_win32_smoke_test OK`, exit 0)

- in-process: Loading appears, Main appears, Loading gone, close Main, thread exits
- in-process close during Loading (`hold_stage=Distilling`): join, no Main
- child `win32_main_window_runtime_demo`: same Loading → Main → close → process exit 0
- child close during Loading (`--hold-stage 2`): process exit 0

Search after cleanup: `dpi`, `DPI`, `WM_DPICHANGED`, `GetDpiForWindow` are absent from `examples/main_window_runtime_demo` and `tests/main_window_*.cpp`.

## Shutdown

- During Loading: GUI `WM_CLOSE` → `RequestStop` + wake; message loop until `done_event`; join; destroy Loading/notify/GUI Domain on GUI thread.
- After Ready: same stop path; presenter/Main HWND destroyed on GUI thread after join.
- No `TerminateThread`. Model objects are not freed on the GUI thread.

## git status --short

After recording the implementation SHA (documentation-only follow-up):

```
 M Progress.md
?? tools/mcp/__pycache__/
?? tools/runners/__pycache__/
?? tools/runtime/__pycache__/
```

# Main-window skeleton — presenter WndProc, wakeup, fail-fast

Status: implemented, verified locally. Not accepted.

## Identity

- Base SHA: `644729c1f69c97055afd90ae88fcda336b8d692e`
- Branch: `prep/deps-objects-assert-mcp-v1`
- Worktree / source: `C:\Users\nickc\Projects\apptraverse-prep-deps-assert`
- `CMAKE_HOME_DIRECTORY`: `C:/Users/nickc/Projects/apptraverse-prep-deps-assert`
- Build dir: `build/win64-ninja-msvc-debug`
- Profile: `win64-ninja-msvc-debug` (incremental; no clean)

## Commits

1. `af70e9c` — Native Main `RegisterClassW` / `WndProc` owned by `Win32MainWindowPresenter`. `WinApp` owns Loading/notify only. `lpParam` is the presenter. `WM_CLOSE` posts `WM_APPTRAVERSE_STOP` to the notify HWND.
2. `158682f` — Lost wakeup: `stop` is a plain `bool` under `mu`; `RequestStop` sets it then `notify_all`. Publication `NotePublished`/`PublishProducer` under the same mutex waiters use. Model graph/Domain/storage leave an inner scope before `SetEvent(done_event)`.
3. (this file) Fail-fast: `LoadApplication` uses `WriteFatalStderr` + `abort`, not `assert`. `fflush`/`FlushFileBuffers` before abort. Regression tests for missing Application. Coding-agent rules: Æther vs `WNDCLASS`, CV mutex, `done` after model scope, GUI `STD_ERROR_HANDLE`.

## Where Main lives

- `Win32MainWindowPresenter::OnLoad`: `RegisterClassW` then `CreateWindowExW(..., this)`
- `Win32MainWindowPresenter::WndProc` in `win_presenters.cpp`
- `OnUnload`: `DestroyWindow` then `UnregisterClassW`

## Fail-fast notes

- Missing Application: `if (!root) { WriteFatalStderr(...); abort(); }`
- Do not compile registrar TUs with `NDEBUG` against Debug aether-objects (`Factory` layout is `#ifndef NDEBUG`). The fatal-ndebug *test* proves abort+diagnostic; it is a Debug binary.
- GUI-subsystem load-only: diagnostic goes to `GetStdHandle(STD_ERROR_HANDLE)` as well as CRT stderr.

## Compile (this tree)

`win_app.cpp` / `win_presenters.cpp` (demo target):

```
cl.exe /TP -DAE_DISTILLATION=1 -DAE_FILTRATION=1 -DAPPTRAVERSE_ENABLE_DISTILLATION -DNOMINMAX -DWIN32_LEAN_AND_MEAN ... /std:c++20 -MDd /utf-8 /Zc:preprocessor -c .../windows/win_app.cpp
cl.exe /TP ... -c .../windows/win_presenters.cpp
```

Includes resolve to `C:\Users\nickc\Projects\apptraverse-prep-deps-assert\include` and this worktree's `_deps`.

## Tests actually run (this checkout)

| target | artifact | status |
| --- | --- | --- |
| `apptraverse_main_window_headless_check` | `apptraverse-build/20260909-003112-345383` | ok (`publication_channel_test`, `main_window_lifecycle_test`, `main_window_lifecycle_load_only_test`, `main_window_fatal_ndebug_test`) |
| `apptraverse_main_window_win32_smoke_check` | `apptraverse-build/20260909-003159-203e12` | ok (`main_window_win32_smoke_test OK`) |
| `win32_main_window_runtime_demo` + `_load_only` | `apptraverse-build/20260909-003136-993f0c` | ok (up_to_date; linked in `20260909-002902-203659`; exercised as smoke children) |

Exes:

- `build/win64-ninja-msvc-debug/tests/apptraverse_main_window_fatal_ndebug_test.exe`
- `build/win64-ninja-msvc-debug/tests/apptraverse_main_window_win32_smoke_test.exe`
- `build/win64-ninja-msvc-debug/examples/main_window_runtime_demo/windows/win32_main_window_runtime_demo.exe`
- `build/win64-ninja-msvc-debug/examples/main_window_runtime_demo/windows/win32_main_window_runtime_demo_load_only.exe`

Cursor `user-apptraverse` MCP still has no `source_dir` (**BLOCKED**). Local runner only.

## Limitations / TODO

- `LoadStoredAncestorLayers` still in App Traverse (`plan.md`)
- Do not restore close-during-Loading
- Not accepted-by-user


# WINDOWS CURSOR — finalize Windows + Android + WASM → surfaces-demo

Status: implemented, verified on integration SHA. Not accepted.

## Source SHAs (remote at finalize)

| branch | SHA |
| --- | --- |
| `origin/prep/deps-objects-assert-mcp-v1` | `7e86814869384e3d052c452306237ec00f0c6b4a` |
| `origin/feature/surfaces-android-v1` | `eee06cfb6b997bbb7a946664b0bad4a5cfe4f046` |
| `origin/feature/surfaces-wasm-v1` | `70f4d27dc6b0ed7418f3fea516fe7f9cf1b6d516` |
| `origin/integration/surfaces-windows-android-wasm-v1` (pre-fix) | `63039c8be00d77541e35bc55513ead41646b1df3` |

All four are ancestors of the final `surfaces-demo` SHA (merge-base `--is-ancestor` exit 0).

## Fixes after merge (on integration)

1. Structural keepalive: keep `Domain::Find` `Ptr` as Obj ownership; do not
   re-wrap via `ae::Ptr<Presenter>{presenter_held}`; clear
   `active_presenters` before `live_objects` after apply.
2. Web host: defer `PageShown` until after structural apply; delay
   `session_.cv.notify_all()` until after DOM/keepalive apply so rapid Add
   cannot publish the next mutation while the previous apply is still on the
   browser main thread.
3. MinGW smoke: `#if defined(_MSC_VER)` around `WaitForSingleObject(gui.native_handle())`
   (`dynamic_objects` / `main_window` win32 smoke).
4. Playwright smoke: `tools/wasm_surfaces_browser_smoke.py`.

## Common

- Canonical persisted current: `Surfaces::mobile_current` (Surface identity).
- `SetCurrentSurfaceEvent` / `Surface::MakeCurrent` / `SurfacePresenter::PageShown` present once.
- Web checkpoint policy preserved (publication → Save → IDBFS); not on Windows/Android session.
- Compiler RTTI usage: none in production; intentional `dynamic_cast` string check in `dynamic_objects_add_test.cpp` only.
- CMake isolates `WIN32` / `ANDROID` / `EMSCRIPTEN`.

## Windows (verified)

Incremental tree: `build/win64-ninja-msvc-debug` (MinGW/`-fno-rtti` despite folder name).

PASS: `apptraverse_surfaces_model_test`, `apptraverse_surfaces_win32_smoke_test`,
`apptraverse_dynamic_objects_add_test`, `apptraverse_dynamic_objects_win32_smoke_test`,
`apptraverse_presenter_load_order_test`, `apptraverse_publication_channel_test`.

Exes:

- `build/win64-ninja-msvc-debug/tests/apptraverse_surfaces_model_test.exe`
- `build/win64-ninja-msvc-debug/tests/apptraverse_surfaces_win32_smoke_test.exe`
- `build/win64-ninja-msvc-debug/examples/surfaces_demo/windows/win32_surfaces_demo.exe`
- `build/win64-ninja-msvc-debug/examples/surfaces_demo/windows/win32_surfaces_demo_load_only.exe`

Desktop does not use `mobile_current` for focus/activation.

## Android (verified)

- Script: `tools/android/run_surfaces_smoke.ps1` — PASS on `emulator-5554` (AVD API 34).
- APK: `examples/surfaces_demo/android/app/build/outputs/apk/debug/app-debug.apk`
- Incremental Gradle (no clean / no `.cxx` wipe).
- `-fno-rtti` on Android `jni_bridge.cpp` compile line.
- Restart restores selected Surface; remove-non-current keeps identity current.

## WASM (verified)

- Tree: `build/wasm-ninja-debug` (`-fno-rtti`, pthreads, COOP/COEP).
- Artifacts: `build/wasm-ninja-debug/examples/surfaces_demo/web/web_surfaces_demo.{html,js,wasm}`
- Serve: `tools/serve_wasm.py` → `http://127.0.0.1:8765/web_surfaces_demo.html`
- Smoke: `tools/wasm_surfaces_browser_smoke.py` — PASS
  - `crossOriginIsolated=true`, SharedArrayBuffer available
  - Add ×2, select Surface 2, real browser reload → Surface 2 current
  - Remove current → topology `[1,3]`
  - Rapid back-to-back Add ×2 → three tabs (notify-after-apply fix)

## Canonical branch

- Canonical tip: `git rev-parse origin/surfaces-demo` (this finalize series; do not treat older integration SHAs as current).
- Intermediate remotes deleted only after ancestry proof:
  - `feature/surfaces-android-v1`
  - `feature/surfaces-wasm-v1`
  - `integration/surfaces-windows-android-wasm-v1`
  - `prep/deps-objects-assert-mcp-v1` (ancestor of `surfaces-demo`)

## Intentionally preserved remotes

- `origin/feature/surfaces-linux-v1`
- `origin/feature/surfaces-macos-v1`
- `origin/feature/surfaces-ios-v1`

## Not done (out of Windows Cursor zone)

- Linux GTK3 / macOS / iOS merge
- SharedNode, chat, AeroAdmin-X, dependency refresh


# WINDOWS CURSOR — pre-shared runtime hardening

Status: implemented, verified on Windows + Android Emulator + WASM. Not accepted.

## Starting SHA

`c00750386619d2d015d30b269a441dc463d7f4bc` (`origin/surfaces-demo`)

## 1. Node materialized-change notifier

Old: process-global `Node::SetMaterializedChangeNotifier(std::function<...>)`.

New: per-Node runtime-only fields
`materialized_change_ctx_` + `MaterializedChangeFn` (function pointer).
`BindMaterializedChangeNotifier` / `ClearMaterializedChangeNotifier` /
`CopyMaterializedChangeNotifierFrom`.

`BindReachableNodesMaterializedChangeNotifier` walks model graph after Load.
`InitializeRuntimeNode(node, runtime_source)` copies notifier onto new Node and
its base so dynamic Surfaces inherit the same runtime without Session-specific
Surface branching.

GUI mirror Nodes are not bound (sessions bind only the model Application root).

`ModelRuntime` and `SurfacesModelSession` / `DynamicModelSession` all use the
instance-scoped path. No `thread_local`, global map, or singleton registry.

## 2. Two concurrent runtime proof

`TestTwoIndependentSessionsIsolation`: Sessions A and B in one process.
A Add / B bounds publish independently; stop A; B still Add + Event on dynamic
Surface2 with notifier intact.

## 3. Runtime Node base ownership

Old unsafe: `Node::ptr::MakeFromThis(static_cast<Node*>(raw.get()))`.

New: `Domain::AddObject` then `Domain::Find` → `Node::ptr{domain, id, {}, held}`.

`TestRuntimeNodeBaseUsesCanonicalOwnership` checks Surface and Surfaces bases
keep most-derived class ids and distinct ObjIds.

## 4. GUI backpressure decoupling

Model wait: work OR (pending dirty && !`is_publication_busy()`) OR stop.
`is_publication_busy()` = unread published **or** consumer `in_ui` slot held.

ModelWork always runs even when a GUI snapshot is unread/held.
Pending dirty uses `PendingDirtyNodes` (vector + set) for first-dirty order and
coalesce.

Web holds `TakePublished()` until apply finishes then `ReleaseConsumer()` so the
next publish cannot interrupt keepalive/DOM apply (rapid Add).

Tests: `TestModelWorkRunsWhilePublicationUnread`,
`TestShutdownDrainsWorkWithUnreadPublication`.

## 5. Runtime storage-write characterization

`TestInitializeRuntimeNodeStorageWrites`: `CaptureBaseState` causes **2**
`IDomainStorage::Store` calls during `InitializeRuntimeNode` before
`Application::Save`. Deferred (not fixed): would need Overlay flush protocol.

## 6. Windows regressions

PASS: `apptraverse_surfaces_model_test` (incl. new cases),
`apptraverse_surfaces_win32_smoke_test`,
`apptraverse_presenter_load_order_test`,
`apptraverse_publication_channel_test`,
`apptraverse_event_sourced_core_test`,
`apptraverse_journal_retention_test`,
`apptraverse_dynamic_objects_add_test`,
`apptraverse_dynamic_objects_win32_smoke_test`.

## 7. Android

`tools/android/run_surfaces_smoke.ps1` PASS on `emulator-5554` (incremental Gradle).

## 8. WASM

`tools/wasm_surfaces_browser_smoke.py` PASS (rapid Add, reload, remove).

## 9. RTTI

No production `dynamic_cast`/`typeid`; intentional string check in
`dynamic_objects_add_test` only. Global notifier symbols removed.

## Final SHA

`4811f09113483e797abe67cdf5075c197e58c26c`


## Join / Send / status-row fix (2026-09-16)

Starting SHA: `0ca85c440ea97ee9f67b1ca2bf825721e075fbd2`

### Landed
- Retryable JoinRequest (1s / 30s Joining deadline; frozen bytes; Accepted keeps Accept-resend path)
- Host Accept-then-snapshot order; JoinRejected to Client; no waiting-map mutation on failed CanApply
- ExpectInitialNodeFromEndpoint(node_id) + ForgetInitialNodeFromEndpoint; snapshot gated on Accepted
- Model-loop `project_demo_status`; Accepted projects Syncing not Joined
- Win32: bottom status/presence labels removed; Send from selected binding; inline join error under top row
- Fake drop-first control; bootstrap regressions; live pair uses `--client` + JOIN + reader threads
- Initial NodeState retry without Online gate (control/data reorder vs presence)

### Verified
- `apptraverse_chat_session_bootstrap_test` PASS (incl. lost first JoinRequest, Joined phase, wrong-source)
- `apptraverse_chat_session_integration_test` PASS
- `apptraverse_shared_sync_protocol_test` PASS
- `apptraverse_chat_demo_model_test` PASS
- `apptraverse_chat_windows_smoke_test` PASS
- Other listed unit targets built

### Live Aether
- `run_chat_session_live_pair.py`: Host obtains ROOM after Client JOIN; Client ROOM timed out in owned runs (first missing stage: Client-visible bind after Host accept/snapshot). Deterministic fake path PASS. Package still ships current EXE for manual GUI check.

### Package
- `dist/chat-demo-host-client-join-fix/` + `start-host.cmd` / `start-client.cmd`
- EXE SHA256: `5B95FE6389D2CAF8DC57F4C6618FC0D8EBE80928743EF49A0FF6BBAE2816A3EA`

Status: implemented/verified (deterministic). Not accepted-by-user. Live cold join still open.


## Windows launcher --state-dir quoting (2026-09-16)

Starting SHA: `574035d3ec458146da9b21a24f93559062d5f270`

### Landed
- Generator `tools/windows_chat_launchers.py` emits
  `--state-dir "%LOCALAPPDATA%\App Traverse\ChatExample\host|client"` (one argv)
- Package `tools/package_host_client_chat_demo.py` regenerates dist launchers (not hand-edited)
- `--dump-parsed-launch <file>` (consumed before Parse; parser unchanged) for app-side proof
- `tools/test_windows_chat_launcher_quoting.py` launches real EXE via generated .cmd + multi-space path

### Verified
- `python tools/test_windows_chat_launcher_quoting.py` PASS
- Manual `start-host.cmd` / `start-client.cmd` (no Argument Error; Host UID; Client UID entry)

Status: implemented/verified. Not accepted-by-user.


---
Status: partial on main. Live Host/Client text convergence NOT FIXED.

# Native transport path rewrite (2026-09-17)

Starting SHA: `954114e`. Final remote: `eed9e02`.

## Landed
1. `apptraverse_aether_p2p_safe_stream_duplex_test` � real `P2pSafeStream` over MockWriteStream; sequential duplex, drop recover, fragment, delay>3s, reentrancy depth (bad>=1, outer=0).
2. `ChatAetherRuntime` outer-loop write pump; deleted size half-duplex / post-Join reset / 3s hang rebuild; restored heartbeat ping/pong coalesce.
3. Pins unchanged: aether-client-cpp `0b0e3b54`, objects `1d302647`, miscpp `f8b2e1c6`.

## Live gate (still FAIL)
- Join + NodeState Host>Client works (`APP_RX` 830).
- First missing stage: Client Join-ACK `APP_TX` (24B, token=4) never gets `WRITE_OK` while Host may `APP_RX` that ACK ~50s later.
- Local messages exist on both sides; SharedEvent text does not converge (`LIVE_FAIL`).
- Logs: `%TEMP%\chat_live_transport_fix2_*`.

## Not packaged
No promoted Host/Client package while live convergence FAILS.
