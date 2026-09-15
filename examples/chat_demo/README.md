# Chat Demo Common Model & Local Workspace

This component implements the common chat model, local workspace persistence, commands, and AeroAdmin launch options parser for AppTraverse chat clients.

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
                  | peer_admin_id                 |
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
  - `ChatEntry`: `peer_admin_id`, `display_name`, `draft`, `scroll`, pointers to `peer_link` and `room`.
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
   - Searches existing entries by normalized `peer_admin_id`. If found, selects it via `ChatSelectedEvent`.
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
- `--state-dir <path>`: Local storage directory path for `DirectoryDomainStorage`.
- `--peer-admin-id <id>`: AeroAdmin peer ID to open or select immediately.
- `--peer-aether-uid <uid>`: Optional Aether transport UID hint for the peer (requires `--peer-admin-id`).
- `--peer-name <name>`: Optional display name for the peer (requires `--peer-admin-id`).

Repeated options, unknown options, missing values, or peer UID/name without `--peer-admin-id` produce a structured error message without exiting the process.

## Current Limitations & Explicit Out-of-Scope

- **Aether Transport**: Not yet connected. The model does not start network threads or send network packets.
- **Presence**: Not yet connected. Real connection observations will come from the Link runtime in subsequent slices; no mock `bool online` is stored in the model.
- **Peer Resolution**: AeroAdmin ID resolution to Aether UID / Link is not implemented in this slice.
- **GUI**: No UI, window handles, or presenters are included in this slice.
