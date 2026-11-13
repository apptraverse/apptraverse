# SharedNode: two live memory replicas — 2026-11-11

Status: implemented / verified on Windows MSVC x64 Debug.
Branch: `feature/shared-node-native-memory`; starting commit: `7d1732b`.
This extends the [native foundation](shared-node-native-memory-2026-11-11.md).

## Implemented behavior

Native reflected InitialState/Event/ACK messages, independently persisted
per-peer pending packets, byte-identical retries, exact frozen snapshot
coverage, save-before-ACK and persistent delivery confirmations now operate
between two live Applications over `IByteLink` / MemoryLink.

New-only native import reuses known canonical object IDs and does not load stale
packet layers over known live or cold stored dependencies. Class collisions
are checked before writes. Incoming Events are tried against a disposable
native graph copy before live insertion. A small Node/NodeFor virtual hook
dispatches insertion through the most-derived serializer for historical replay.

The source captures the Event graph before Apply and persists its frozen message
bytes before Send. The receiver persists the applied shared graph and parent
attachment before ACK. The source completes pending and saves the confirmation
before stopping retry. Duplicate Events compare native Event-layer data and
timestamp, then re-ACK without insertion. Initial duplicates use persisted
receipts and never replace the live root.

SharedPeerState and SharedNodeLocalState local schemas are now S1 with explicit
S0 loaders for the previous foundation's metadata. The wire header accepts
exactly 1.0; this is not full client schema/capability negotiation.

## Executed scope

Same incremental `build/shared-node-debug`, MSVC 19.50 x64, `/GR- /EHs-c-`,
`_HAS_EXCEPTIONS=0`; unchanged pinned object dependency and main's exact 27-patch
chain. No new populate, Release build or real network client.

```sh
cmake --build build/shared-node-debug --target apptraverse_shared_node_headless_check
```

**6/6 CTest entries passed**, including all four foundation entries and:

- `apptraverse_shared_sync_test` — eight scenario groups below.
- `apptraverse_shared_graph_import_test` — native new-only graph import,
  aliases, source destruction, local exclusion, known live/cold dependency
  preservation, repeated canonical import, and loaded/cold class collisions
  with zero destination writes.

| Live scenario | Evidence |
| --- | --- |
| Initial loss, lost initial ACK and Event overtaking initial | Snapshot retries preserve ID/bytes and cover only frozen history; early Event gets no success ACK and later retry converges |
| Initial-state restart boundaries | H restarts after pending save/before first Send; B restarts before initial arrival from persisted bootstrap config; B restarts after import with lost ACK; one Room and the same receipt survive |
| Bidirectional Event loss, duplicate and reverse ordering | Exact retry bytes, original distinct timestamps, equal journal IDs/order/payload and materialized text |
| Lost ACK and receiver restart | Applied/saved Event survives; duplicate after restart re-ACKs without a second journal entry; saved sender ACK survives restart and does not regenerate historical pending even on a new local commit |
| Pending before Send and both replicas restarting | Pending bytes survive complete runtime/App/Domain/endpoint destruction and are sent unchanged after restore |
| Wrong ACK, two Nodes and Offline pacing | Wrong source/Node/packet cannot clear pending; ACK for Node A leaves B alone; prolonged Offline sends nothing; Online resumes once and retry honors the resumed-send deadline |
| Duplicate initial after local edits | Receiver-created Event and local origin metadata survive repeated old snapshot and restart |
| Unsupported major version | No persistence callback, model mutation or success ACK; valid original retry still succeeds |

Both Applications own independent storage/Domain graphs. Persistence copies the
native Application graph into separate durable RAM snapshots. Restart discards
live storage, runtime, transport, endpoint, App and Domain; no teardown Save is
allowed. Test-only constructor/destructor address tracking proves the old
canonical Room/Application objects were actually destroyed, including the
Event-to-Room cycle stress case. Restore reads only the saved native layers.

At each outgoing ACK a frame ledger identifies exactly which native data the
ACK answers. An independent cold reader reaches the shared Room through the
persisted Application reference and checks that data and its materialization.
The oracle does not compare merely live state to saved state.

### Mutation sensitivity

A temporary, reverted production mutation emitted the incremental Event ACK
before `PersistState()`. The test failed inside the ACK hook with
`CHECK failed: found != actual.end()` because the acknowledged Event was absent
from the durable journal. The corrected code was rebuilt and all six entries
passed again. No mutation is part of the committed implementation.

### Regression

**9/9 existing targeted headless tests passed** after rebuilding consumers of
the additive Node/NodeFor insertion hook:

```sh
ctest --test-dir build/shared-node-debug --output-on-failure -R '^apptraverse_(event_sourced_core|object_clone|historical_base_materialization|model_persistence|single_file_user_storage|object_scope_storage|application_contract_headless|window_catalog_headless|minimal_app_headless)_test$'
```

`git diff --check` passed. Existing MSVC option-override warnings and the
unrelated long-target configure warning remain unchanged.

## Contract boundaries

- The application saves its bootstrap configuration and rebinds runtime-only
  initial callbacks after restart. The fixture demonstrates this with ordinary
  native Application Events. The runtime does not serialize callbacks.
- Persistent success is the boundary: no arbitrary torn-write/power-failure
  atomicity or disk-error recovery is claimed. The live tests use native RAM
  persistence snapshots, not a restarted OS process or real filesystem outage.
- Native IDs represent the same logical objects across these replicas. General
  cross-client namespace remapping and unrelated same-class ID collisions are
  not solved here.
- Events are authored with `SharedSyncRuntime::CommitLocal`; arbitrary ordinary
  Node commits are not automatically observed. This is a model-thread runtime.
- Timestamps are distinct in these tests, on one computer. No tie-break,
  equal-timestamp convergence or cross-machine clock claim is introduced.
- A second distinct initial snapshot for a live root is rejected. General late
  snapshot merging, dynamic participant changes, arbitrary mutable dependency
  semantics and third-participant coverage propagation remain later slices.
- Memory availability is supplied by the stand. Heartbeat timeout, real Aether,
  GUI, other-platform runtime runs and full version negotiation were not run.
  Shared-history compaction remains blocked.
