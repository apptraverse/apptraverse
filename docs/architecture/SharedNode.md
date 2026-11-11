# SharedNode: native serialization and memory foundation

Scope: first implementation slice on `feature/shared-node-native-memory`, based
on main `8b592f994c2ad2931bcef22a044a6a21dbe05c4e`. The user's November 11 request
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
  `MarkDelivered` records local knowledge; it is not an ACK implementation.
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

`SerializeSharedGraph` / `DeserializeSharedGraph` provide export and parsing,
not an established-replica import algorithm. The latter validates registered
class chains and publishes parsed output only on success. The memory proof
loads native IDs into fresh, independent Domains. Operation-local hydration of
known native instances is tested separately for preservation of local state.
Cross-packet reconciliation, ID collisions, late snapshot merge and duplicate
packet application remain part of the synchronization stage.

## Memory stand

`IByteLink` accepts destination UID and bytes, and receives source UID and bytes.
`MemoryLink` registers runtime callbacks only; it has no model pointers or wire
serialization. `MemoryLinkNetwork` exposes queued packet identities and allows
explicit delivery order, drop, duplicate, delay and manual logical time.

The stand supplies availability observations. Unknown permits sending; Offline
suppresses enqueue/delivery. Recreating an endpoint does not inherit stale
availability. Heartbeat generation, timeout detection, retry pacing and pending
packet persistence are not implemented by this first transport primitive.

## Next slices

1. Native typed initial-state/Event/ACK messages and version compatibility,
   independent delivery state for every destination, frozen retry packets and
   save-before-ACK. Define cross-replica logical object/Event identity before
   importing into established Domains.
2. Two independent Applications with incremental delivery, real destruction and
   restoration, loss/duplicate/out-of-order scenarios and presence-driven retry.
3. H accepts Join B and sends its native participant graph to A. H supplies B's
   initial state; subsequent events originate directly to all peers. A can send
   to B as soon as it knows B's UID; no A-B handshake is required.
4. Define exact initial-state coverage and reliable confirmation, and test every
   ordering of Join, initial state, Event and ACK. Pending dependency handling
   must not fabricate participants or overwrite receiver-local state.
5. Node-owned safe checkpoint/compaction policy, tested with late Events,
   offline peers and late attachment. Keep history until that contract is proven.

Ordering remains by `timestamp_us` within the agreed single-computer scope.
Event identity does not become a hidden ordering key. Equal-timestamp behavior
must be settled before claiming general convergence. GUI, real Aether transport
and cross-machine clocks are outside this slice.

## Headless entry point

In a Debug build configured with `BUILD_TESTING=ON`:

```sh
cmake --build build/shared-node-debug --target apptraverse_shared_node_headless_check
```

This builds and executes the memory, local-pointer, SharedNode and existing
native object graph proof tests without enabling historical GUI examples.
