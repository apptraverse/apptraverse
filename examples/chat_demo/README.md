# Chat Demo Common Model & Local Workspace

This component implements the common chat model, local workspace persistence, commands, and Host/Client launch options parser for AppTraverse chat clients.

## Model Ownership & Graph Structure

```
                  +-------------------------------+
                  |         ChatWorkspace         | (Local persistence root, Node)
                  |-------------------------------|
                  | local_endpoint_uid            |
                  | next_message_sequence         |
                  | selected_chat_id              |
                  | desktop_bounds (DesktopBounds)|
                  | chats [vector<ChatEntry::ptr>]|
                  +---------------+---------------+
                                  |
                                  | 1..* (owns)
                                  v
                  +-------------------------------+
                  |           ChatEntry           | (Local device state, Node)
                  |-------------------------------|
                  | peer_uid                      |
                  | display_name                  |
                  | draft                         |
                  | scroll (ScrollAnchor)         |
                  | peer_link (Link::ptr) --------+----+
                  | room (ChatRoom::ptr) ---------+--+ |
                  +-------------------------------+  | |
                                                     | |
                 +-----------------------------------+ |
                 |                                     |
                 v                                     v
+----------------------------------+   +----------------------------------+
|             ChatRoom             |   |               Link               |
|            (SharedNode)          |   |              (Node)              |
|----------------------------------|   +----------------------------------+
| messages [vector<MessageValue>]  |   (Peer transport configuration)
| shares [vector<Share>]           |
| link_sync_states                 |
+----------------------------------+
```

### Private vs Shared Fields

- **Private / Local to Workspace & Device**:
  - `ChatWorkspace`: `local_endpoint_uid`, `next_message_sequence`, `selected_chat_id`, `desktop_bounds`, `chats`.
  - `ChatEntry`: `peer_uid`, `display_name`, `draft`, `scroll`, pointers to `peer_link` and `room`.
  - These are local `Node`s and are never replicated across the network to peers.
- **Shared / Replicated via SharedNode**:
  - `ChatRoom`: `messages` (`std::vector<MessageValue>`), `shares` (`std::vector<Share>`), `link_sync_states` (per-link synchronization metadata).
  - Messages are stored as values inside shared replication events (`MessageAddedEvent`), and ordered by event timestamp in the journal.
  - Network synchronization exports only `ChatRoom` without touching or sharing local workspace/entry objects.

## Exact Common Commands

Platform hosts (Windows, Linux, Android, Web/WASM) interact with the model via common commands running on the model thread:

1. `OpenOrSelectChat(workspace, admin_id, display_name, persist)`:
   - Trims surrounding ASCII whitespace from `admin_id`.
   - Rejects empty ID.
   - Searches existing entries by normalized `peer_uid`. If found, selects it via `ChatSelectedEvent`.
   - If absent, creates and initializes `ChatEntry` (`InitializeRuntimeNode`), commits `ChatEntryAddedEvent`, and selects it via `ChatSelectedEvent`.
   - Persists state once after the commands.

2. `BindChat(entry, link, room, persist)`:
   - Requires all nonempty references to belong to the same Domain as `entry`.
   - Requires non-empty `link` and `room`.
   - Idempotent no-op if already bound to the same link and room.
   - Conflicting non-empty binding returns explicit error (`false`).
   - Commits `ChatBindingChangedEvent` and invokes `persist`.

3. `BindLocalEndpoint(workspace, uid, persist)`:
   - Sets local transport identity UID when known.
   - Idempotent if equal; rejects if already set to a different UID.

4. `SetDraft(entry, text, persist)`:
   - Commits `DraftChangedEvent` on change and persists.

5. `SetScroll(entry, anchor, persist)`:
   - Commits `ScrollChangedEvent` on change and persists.

6. `SetDesktopBounds(workspace, bounds, persist)`:
   - Commits `DesktopBoundsChangedEvent` on change and persists.

7. `SelectChat(workspace, entry_id, persist)`:
   - Commits `ChatSelectedEvent` on change and persists.

8. `SubmitDraft(workspace, entry, now_us, persist)`:
   - Validates prerequisites: known local endpoint UID, bound room, non-empty draft, non-zero timestamp, valid sequence.
   - Reserves sequence `workspace.next_message_sequence` via `MessageSequenceReservedEvent` and persists the reservation immediately.
   - Constructs `MessageValue` with `SharedEventId{local_endpoint_uid, reserved_sequence}`, timestamp, and draft text.
   - Commits `MessageAddedEvent` to the room using `CommitShared`.
   - Persists room state.
   - Clears draft in entry via `DraftChangedEvent` and persists again.
   - Note: neither draft submission nor message receipt modifies `ScrollAnchor`.

## Supported Launch Arguments

Command-line arguments supported for desktop execution (excluding argv[0]):
- `--host` or `--client`: required role (exactly one).
- `--state-dir <path>`: Local storage directory (optional; role-specific defaults under AppTraverse / ChatExample).
- `--host-uid <uid>`: Client-only prefill of the Host UID field (does not Join).

Unknown options, both roles, no role, or `--host-uid` without `--client` produce an input error without starting the application.


## Architecture: Aether Thread Boundary & Model Thread Delivery

AppTraverse chat demo enforces strict thread ownership boundaries:

### Aether Thread (Dedicated Network Thread)
- Owns `ae::AetherApp`, `ae::Client`, `ae::P2pStreamManager`, and `ae::P2pStream`s.
- Handles steady-clock heartbeat Ping/Pong scheduling and timeout detection.
- Encodes and decodes the internal `'A' 'T' 'R' 'N'` stream framing envelope (multiplexing application bytes, heartbeat ping, and heartbeat pong).
- Performs transport-local duplicate suppression on pending outbound frames prior to stream connection.
- Posts raw application payload frames into a caller-supplied `ModelDispatch` queue (`ModelTask`).
- Emits raw application frame into dispatcher.

### Model Thread (Domain / Application Thread)
- Executes `ModelTask` dispatched from the Aether thread.
- Drives `AetherByteTransport::ReceiveFn`.
- Owns and mutates `SharedSyncRuntime`, `ae::Domain`, `DirectoryDomainStorage` / `ae::RamDomainStorage`, `ChatWorkspace`, `ChatEntry`, `ChatRoom`, and `LinkSyncState`.
- Processes user commands (`SubmitDraft`, `SetDraft`, `OpenOrSelectChat`, etc.).

**Explicit Invariant**:
"No ae::Domain, Node, SharedNode, SharedSyncRuntime or model storage mutation occurs on the Aether thread."

### Runtime-Only Presence Semantics
`PeerPresence` (`kUnknown`, `kConnecting`, `kOnline`, `kOffline`) is runtime-only and never persisted into Domain objects or storage.
- When an Aether stream is newly bound or rebound from offline, presence transitions to `kConnecting` (if not already `kOnline`).
- Receipt of any valid application frame or heartbeat ping/pong transitions presence to `kOnline`.
- If no frame is received within `offline_after_ms`, presence transitions from `kConnecting` or `kOnline` to `kOffline`.
- Malformed stream frames are dropped immediately without updating presence or forwarding application bytes.

## Runtime Architecture & Reusable ChatSession

The chat runtime provides a cross-platform `ChatSession` (`runtime/chat_session.{h,cpp}`):
- Manages lifecycle (`kStarting`, `kReady`, `kFailed`, `kStopped`).
- Isolates `state_dir/model` (chat domain and workspace graph) from `state_dir/aether` (Aether network client state).
- Drives `SharedSyncRuntime` and `AetherByteTransport` on the model thread.
- Exposes a thread-safe value-copying API for GUI hosts (`OpenPeer`, `SelectChat`, `EditDraft`, `SendDraft`, `SaveScroll`, `SaveBounds`).
- Publishes model snapshots to the GUI via `PublicationChannel<3>` alongside `ChatRuntimeStatus`.
- Automatically elects room creator (`canonical local UID < canonical remote UID`) and accepts incoming rooms from authorized endpoints via `ExpectInitialNodeFromEndpoint`.

## Windows Host (`apptraverse_chat.exe`)

Native Win32 desktop executable (`windows/`):
- System controls: ListBox for chats, Msftedit RichEdit for read-only transcript, multiline Edit for draft, Send button, peer connection inputs, status line, and presence label.
- Geometry and placement restoration via `WINDOWPLACEMENT` with off-screen monitor detection and clamping.
- File-level profile locking (`profile.lock`) preventing concurrent execution with the same state directory.
- Scroll restoration using message-based `ScrollAnchor` table.

## Current Limitations & Explicit Out-of-Scope

- **Peer Resolution**: Peer AeroAdmin ID resolution service without an Aether UID is not implemented in this slice; supplied Aether UID or existing bound link is required.
- **Running-Instance Launch Forwarding**: Not implemented; attempts to open an already-locked profile show an explicit error.
- **Other Platform GUIs**: Linux/Android/Web GUI not implemented in this slice (reusable `ChatSession` prepared for future hosts).
- **Disk Transactions**: Arbitrary disk-failure transaction rollbacks are not claimed.
