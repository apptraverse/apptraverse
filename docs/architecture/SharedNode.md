# SharedNode: native memory synchronization

Scope: serialization foundation and two-live-replica delivery on
`feature/shared-node-native-memory`, based on main
`8b592f994c2ad2931bcef22a044a6a21dbe05c4e`. The user's November 11 request
explicitly starts new SharedNode work; the older removal/deferment of network
replication in the WindowCatalog plan does not govern this separate extension.

## Implemented boundary

Link the `apptraverse_shared_node` target and explicitly call
`EnsureSharedNodeRegistration()` before loading its types. Existing application
registration entry points do not acquire the extension's factories.

- `SharedLinkDescriptor` is a separately identified Node containing an endpoint
  UID and a local-only transport configuration reference. Configure its initial
  data before `InitializeRuntimeNode`. Its Node identity survives another Node's
  historical replay, so multiple SharedNodes can refer to the same descriptor.
- `SharedNode` has shared link topology, changed through `AddSharedLinkEvent`.
  There is no peer-count restriction. History compaction starts blocked.
- `SharedNodeLocalState` and its per-peer `SharedPeerState` Nodes are reachable
  through one `LocalPtr`. The entire collection, including its size and local
  delivery Events, belongs to this replica. Local metadata changes use Events.
  Pending packets store their exact native message bytes. Local delivery Events
  record queueing, received initial receipts and completion. Completion removes
  pending and records delivery in one local Event.
- `SharedNode` preserves this local root across historical replay using the
  existing operation-scratch hooks. Shared topology replay does not create local
  peers or open transports.

The graph is serialized by `DomainGraph(NetworkShared)`. A reflected
`SharedGraphSnapshot { root_id, layers }` uses the ordinary `BinaryArchive` and
native `RamDomainStorage::ObjClassData`. Object fields, references and class
layers are encoded by the existing object system. There is no separate field
format, manual length/offset framing, application-specific remap or GUI mirror
serialization in this path. Export freezes bytes independently of the sender.

`LocalPtr` continues to save/load normally under `LocalPersistent`. A network
save writes an empty pointer and omits its referent. A network load consumes
that empty slot while preserving an existing destination-local pointer. New
shells keep empty local slots. Collections that are entirely local must be
behind a local pointer, rather than exposing a shared vector of local slots.

`CaptureSharedGraph` / `SerializeSharedGraph` / `DeserializeSharedGraph` provide
native export/parsing. `ValidateSharedGraphSnapshot` validates the closed graph
in a disposable Domain. `ImportSharedGraphNewObjects` preflights loaded and cold
class definitions, installs only missing native objects and reuses existing
canonical instances. It never hydrates known dependencies from stale packet
state. Different-class ID collisions are rejected before writes. Native IDs
must identify the same logical object across these replicas; unrelated
same-class collisions are not resolved by this slice.

## Two-live-replica delivery

`SharedSyncRuntime` is model-thread code over `IByteLink`. Its native reflected
`SharedSyncMessage` carries InitialState, Event or ACK, target Node, packet/Event
identity, timestamp and a typed `SharedGraphSnapshot`. Version 1.0 is accepted;
other header versions are rejected without successful ACK. Full schema/capability
negotiation between different client builds remains separate work.

The application owns persistence. Its `Persist` callback must save the whole
local Application graph, including the shared root and local delivery subtree,
before returning success. The runtime latches a failure if persistence fails;
there is no success ACK or further transmission from that failed runtime.

Bootstrap is explicit: initialize a source Node's local subtree once with
`InitializeLocalState`, then `RegisterNode`. A receiver calls
`ExpectInitialState(node_id, source_uid, class_id, attach_callback)` for a new
root. The application persists its bootstrap configuration and rebinds callbacks
after restart; callbacks themselves are runtime-only. A successful incoming
initial state is attached to the local Application before persistence and ACK.
No A-B handshake or reciprocal snapshot is required for subsequent Events.

`QueueInitialState` freezes the snapshot, exact covered Event IDs and packet
bytes, then persists the outgoing obligation. `CommitLocal` validates and inserts
a local Event, records its origin locally, queues independent outgoing packets
for its peers and persists them. This explicit authoring entry point is required
for this runtime; arbitrary calls to ordinary `Node::Commit` are not automatically
classified or transmitted. Outgoing Event closure is captured before Apply.

Both methods queue and persist without sending. `Service(now_us)` sends pending
bytes and repeats them after the configured interval. Offline suppresses sends;
Unknown permits a paced attempt. Deadlines are runtime-only, so a restored
pending packet may be retried immediately, with the same ID and bytes. Saved
delivery confirmations prevent historical Events from being queued again.

An Event receives a success ACK only after insertion and persistence. Duplicates
are checked against journal identity, timestamp and native Event-layer data,
persisted and re-ACKed without a second insertion. ACK source, target Node,
packet kind and Event identity must match that peer's pending record. ACKs
themselves are not acknowledged. If an ACK cannot be sent, sender retry elicits
it again. Unknown roots/missing dependencies receive no success ACK; the sender
retains the durable packet and retries after the prerequisite is available.

Initial ACK covers only the frozen history. Later Events are independent
packets and can overtake initial state; retry delivers them after import.
Received initial receipts are persistent. A repeated initial packet never
overwrites the live root, even after local edits and restart. A second distinct
initial snapshot for an established root is rejected rather than merged.

Before remote insertion, the runtime tries it on a disposable native copy.
`Node::TryInsertJournalEvent` dispatches through the most-derived `NodeFor<T>`
serializer, preserving application fields during timestamp-ordered replay.
Failed speculative replay cannot partially change the live replica.

## Memory stand

`IByteLink` accepts destination UID and bytes, and receives source UID and bytes.
`MemoryLink` registers runtime callbacks only; it has no model pointers or wire
serialization. `MemoryLinkNetwork` exposes queued packet identities and allows
explicit delivery order, drop, duplicate, delay and manual logical time.

The stand supplies availability observations. Unknown permits sending; Offline
suppresses enqueue/delivery. Recreating an endpoint does not inherit stale
availability. Retry pacing and persistence belong to `SharedSyncRuntime`.
Heartbeat generation and timeout detection are not implemented by this stand.

## Next slices

1. H accepts Join B and sends its native participant graph to A. H supplies B's
   initial state; subsequent events originate directly to all peers. A can send
   to B as soon as it knows B's UID; no A-B handshake is required.
2. Extend exact initial-state coverage and reliable confirmation to all peers,
   and test every
   ordering of Join, initial state, Event and ACK. Pending dependency handling
   must not fabricate participants or overwrite receiver-local state.
3. Node-owned safe checkpoint/compaction policy, tested with late Events,
   offline peers and late attachment. Keep history until that contract is proven.
4. Different-build schema/capability compatibility, transport heartbeat and real
   Aether adaptation after the memory contract. Dynamic topology and arbitrary
   mutable dependency graphs need their own application conflict/replay tests.

Ordering remains by `timestamp_us` within the agreed single-computer scope.
Event identity does not become a hidden ordering key. Equal-timestamp behavior
must be settled before claiming general convergence. GUI, real Aether transport
and cross-machine clocks are outside this slice.

## Headless entry point

In a Debug build configured with `BUILD_TESTING=ON`:

```sh
cmake --build build/shared-node-debug --target apptraverse_shared_node_headless_check
```

This builds and executes six native/memory test executables, including live
delivery/restart and new-only graph import, without historical GUI examples.
Validation: [foundation](../validation/shared-node-native-memory-2026-11-11.md),
[live delivery](../validation/shared-node-live-memory-2026-11-11.md).
