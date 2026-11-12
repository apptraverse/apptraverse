# SharedNode: native synchronization

Scope: native serialization, indexed reliable live delivery, confirmed
checkpoint compaction, an optional Aether byte transport and host admission
into a three-participant room on
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
  There is no peer-count restriction. Uncontrolled history compaction stays
  blocked; the explicit confirmed-checkpoint operation selects its safe prefix.
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
`SharedSyncMessage` carries InitialState, Event, ACK, CoverageReceipt or Progress, target
Node, packet/Event identity, timestamp and a typed `SharedGraphSnapshot`.
New messages use version 1.3 with progress reports and checkpoint boundaries,
typed author stamps, initial stream receipts and continuous ACK cursors.
Native 1.0/1.1/1.2 layouts remain readable/writable and
retain their frozen bytes; their replies use the same minor version. Indexed
delivery requires 1.2; the older admission layout requires at least 1.1. Lossy
downgrading of newer fields is rejected. Other header versions are rejected
without successful ACK. This is not full schema/capability negotiation between
different client builds.

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
Each origin frame is retained in the local root independently of per-peer
pending until confirmed compaction, so a later participant can receive the
original frame even after an older destination acknowledged it.

Authoring/bootstrap methods queue and persist without sending. `Service(now_us)` sends pending
bytes and repeats them after the configured interval. Offline suppresses sends;
Unknown permits a paced attempt. Deadlines are runtime-only, so a restored
pending packet may be retried immediately, with the same ID and bytes. Saved
delivery confirmations prevent historical Events from being queued again.

An Event receives a success ACK only after insertion and persistence. Duplicates
are checked against journal identity, timestamp and native Event-layer data,
persisted and re-ACKed without a second insertion. ACK source, target Node,
packet kind and Event identity are validated against the recipient and retained
outgoing identity. Legacy Event ACKs complete only unstamped pending packets;
indexed ACKs follow the prefix contract below. ACKs themselves are not
acknowledged. If an ACK cannot be sent, sender retry elicits it again. Unknown
roots/missing dependencies receive no success ACK; the sender retains the
durable packet and retries after the prerequisite is available.

Initial ACK covers only the frozen history. Later Events are independent
packets and can overtake initial state; retry delivers them after import.
Received initial receipts are persistent. A repeated initial packet never
overwrites the live root, even after local edits and restart. A second distinct
initial snapshot for an established root is rejected rather than merged.

Before remote insertion, the runtime tries it on a disposable native copy.
`Node::TryInsertJournalEvent` dispatches through the most-derived `NodeFor<T>`
serializer, preserving application fields during timestamp-ordered replay.
Failed speculative replay cannot partially change the live replica.

## Author indexes and continuous acknowledgements

Each new Event, including the host's Join, receives a native
`SharedEventStamp { author_uid, index, event_id, timestamp_us }`. Indexes start
at 1 for each author within each SharedNode. Event identity remains native;
the index is delivery metadata and does not replace timestamp journal ordering.

`SharedNodeLocalState::stream_events` retains immutable stamps for authored and
received Events. Reusing an Event ID with a different stamp, reusing an author's
index, or contradicting timestamp order between known indexes is rejected
before live insertion. Out-of-order reception is valid: index 3 can be stored
before index 2 without claiming that 2 was received. Stamps and authored frozen
frames are persisted through local Events before any corresponding ACK/send.

`ReceivedPrefix(author)` returns the largest contiguous sequence from index 1,
with the timestamp at that boundary. A 1.2 Event ACK echoes the triggering
Event's stamp and carries this prefix. A prefix ending before the trigger is
valid; it leaves all pending packets above the gap intact. When the missing
Event arrives, one ACK can confirm several already received Events. A zero
prefix confirms no indexed Event. Lost ACKs and duplicate Events elicit the
current saved prefix rather than inventing progress.

The sender validates the trigger against its retained original frame, or exact
retired stamp after confirmed compaction, and the prefix boundary against its
own saved author index/timestamp. It completes
only ordinary indexed pending packets from that author up to the prefix, for
that ACK's source peer. The peer's `acknowledged_streams` cursor and packet
completions are saved together. Another recipient's pending is untouched.
Delayed lower prefixes cannot downgrade saved progress. A legacy individual
ACK cannot complete indexed pending.

Initial messages explicitly carry the known native stamp ledger because
`LocalPtr` metadata is excluded from the graph snapshot. Every supplied stamp
must name either a snapshot journal Event with the same timestamp or a retired
Event at/below the checkpoint boundary, and a known author Link; Event IDs and
author indexes must be unique. A snapshot can contain
a sparse stream and preserves its gap. Imported stamps are saved before the
initial ACK and do not become locally authored Events. Exact admission coverage
and a continuous stream prefix remain different facts: a snapshot can deliver
index 3 while index 2 is still missing.

Chronological generation is enforced across restart. The next local index and
last author timestamp come from saved stamps; upgraded unstamped local origins
still contribute their timestamps. Explicit time must exceed that author's
last time, published progress promise, closed checkpoint and bootstrap snapshot
floor, otherwise CommitLocal/AdmitPeer
returns false before changing the journal or saving. Automatic time is at least
the current clock and strictly exceeds saved author/floor time and the current
journal tail, with overflow rejected. New/imported local roots capture the
journal-tail floor before their
base state is initialized. A checkpoint snapshot raises the new author's floor
to at least its closed boundary. Legacy metadata has no invented snapshot floor.

Old history remains unstamped: loading it does not assign guessed authors or
indexes. Its frozen 1.0/1.1 packets retain individual ACK behavior. New indexed
events begin at 1 for an author with no saved indexed stream. This is format
compatibility, not mixed-build indexed synchronization.

## Progress promises and confirmed checkpoints

The application explicitly calls `PublishProgress(node_id, through_us)`; zero
selects an automatic time. A native `SharedProgressMarker` binds one author,
its promise that no later Event will have timestamp at/below that time, the
exact sorted room membership and the author's durable continuous receipt
cursors for every participant. A silent participant can publish a promise
without creating a business Event. Publication saves the promise and frozen
delivery obligations before Service can transmit them. It is not a heartbeat.

Reports go directly to all other participants through paced retry. A receiver
requires every reported receipt prefix and its timestamp boundary to be locally
present before saving the report and sending its terminal Progress ACK. This
includes the report author's own prefix. A report that overtakes the needed
Events remains unacknowledged until retry after those Events arrive. A newer
coordinatewise stronger report can replace the saved report; dominated repeats
are re-ACKed without downgrading progress. Incomparable promises/receipts are
rejected. Refreshing reports after more Events arrive supplies newer durable
receipt evidence even when the author has created no new business Event.

`CompactConfirmed(node_id)` considers reports from every current participant.
Their memberships must exactly match the current room. It takes the minimum
promise, requires local continuous receipt through every author's declared own
prefix, and requires every participant's report to confirm each author's
Event prefix through that candidate time. This combines two separate facts:
no unknown earlier Event can still be generated, and all known Events through
the boundary have been durably received by everyone. Offline or silent peers
with insufficient reports stop advancement. No peer is implicitly removed.

The selected timestamp prefix is folded through the ordinary most-derived
`NodeFor` compaction path, preserving current application fields and this
replica's local subtree. Existing native schema/compaction guards still apply;
only the actually collapsed prefix can retire its frozen authored payloads.
The closed boundary and local cleanup are saved together. Failure to persist
latches the runtime rather than allowing further sends from unsaved progress.

The immutable stamp/ID ledger remains. It preserves exact duplicate identity,
author indexes and ACK validation after payload retirement; this first policy
does not compress that ledger. A retired Event with its exact stamp is re-ACKed
without reinsertion or dependency import. Conflicting stamps or an unknown
Event at/below the closed boundary cannot reopen historical state.
The retired payload is no longer available for byte comparison; replay handling
uses its retained immutable author/index/Event ID/timestamp identity.

Initial state carries the closed checkpoint, full stamp ledger and materialized
base plus remaining journal. Its exact covered IDs include retired Events as
well as the remaining journal. Therefore an existing peer that has not yet
compacted does not send the old covered payloads to a new participant. The
joining participant starts after the adopted checkpoint. Current-roster
reports must be refreshed before further compaction after admission; an old
two-participant report is not evidence for a three-participant room.

This is logical journal and origin-payload compaction. Native storage may still
contain unreachable old object layers; storage garbage collection and physical
file-size reduction are separate work. Retained identity metadata is also not
bounded by this operation.

## Host admission and direct delivery

The application calls `AdmitPeer(node_id, join_event, joining_uid, timestamp)`
on H. The ordinary application Join Event references the new participant's
native graph (for example Bob's name and Link descriptor). Its Apply must add
that descriptor to the shared Node's links. The runtime does not decode chat
classes or create application profiles by UID.

H freezes one admission plan binding Join ID, joining UID, host UID, initial
packet ID and exact snapshot-covered Event IDs. In one model-thread operation
it applies Join, reconciles local peer metadata, retains the pre-Apply Join
frame, freezes initial state including Join, and saves all delivery obligations
before Service can send. Join goes to existing participants; initial state goes
to B. Either can arrive first.

A installs the received plan before queueing its own Events for B. Covered
history is assigned to H's initial delivery: it is suppressed in A→B scheduling
but is not yet marked delivered. A immediately sends its own uncovered Events
to both H and B using B's UID. That includes an older Event that H had not
received when freezing the snapshot; its retained origin frame is reused even
if H subsequently ACKed it. There is no A-B handshake.

When B's initial ACK arrives, H confirms only H→B coverage and queues reliable
CoverageReceipt obligations for existing peers in the same persistence step.
A accepts a receipt only for the exact installed admission, marks that coverage
delivered to B, saves, and emits a terminal ACK. H persists notice completion
separately from business Event delivery. ACKs themselves are not ACKed. A
receipt arriving before Join receives no success ACK and is retried by H.
Duplicate Join/receipt cannot downgrade confirmed coverage or create duplicate
participants. All admission and receipt progress is local-persistent.

Coverage remains directional. B's snapshot receipt does not confirm H→A
delivery, so H continues any outstanding sends to A. B knows the initial source
H has the snapshot, but does not infer that A has all of it. Imported history
does not become B-authored history. After admission, each participant directly
sends its own new Events to every other participant; H does not relay A's or
B's ordinary Events. Established A↔B delivery continues when H is Offline.

An Event overtaking the recipient's initial state or necessary Join receives no
success ACK. Its origin retains the durable packet until retry can apply it.
No agreement exchange or fabricated participant is needed. Suppressing covered
incremental history does not forbid repeating the one frozen initial packet
after its ACK is lost; the receiver re-ACKs it without replacing live state.

Local delivery schemas are S4, with S0/S1/S2/S3 metadata loaders and the older
QueueSharedPacketEvent/RememberSharedLocalFrameEvent inline-layout loaders.
Existing frozen pending bytes are not rewritten or attributed guessed indexes.
S1 origin IDs that never retained a pre-Apply frame cannot supply uncovered old
history to a newly added peer; the runtime refuses to invent that frame from
newer dependency state.

## Memory stand

`IByteLink` accepts destination UID and bytes, and receives source UID and bytes.
`MemoryLink` registers runtime callbacks only; it has no model pointers or wire
serialization. `MemoryLinkNetwork` exposes queued packet identities and allows
explicit delivery order, drop, duplicate, delay and manual logical time.

The stand supplies availability observations. Unknown permits sending; Offline
suppresses enqueue/delivery. Recreating an endpoint does not inherit stale
availability. Retry pacing and persistence belong to `SharedSyncRuntime`.
Heartbeat generation and timeout detection are not implemented by this stand.

## Optional Aether transport

`APPTRAVERSE_BUILD_AETHER_LINK=ON` adds `apptraverse_shared_aether` and
`AetherByteLink`; the option defaults OFF. Ordinary model/UI builds and the
existing memory check do not acquire the SDK. The separate
`APPTRAVERSE_BUILD_AETHER_CLOUD_WORKER` option also defaults OFF and adds only
the owned headless cloud worker, not a GUI example or an automatic network test.

The public SDK base is `5ddf5eaaac5bd154ec384ff8c74f247a0ab80fb7`, the working
push SDK used by App. Frozen consumer patches add the streaming fixes and
native Client presence API without changing ordinary push registration/Ping
or P2P message formats. GetClientTiming uses its defined correlated RPC.
The native source/test postimages are checked by manifest; an isolated source
override can also use equivalent integration `423ab1e1`. Foreign live worktrees
and benchmark reports are not consumed by the build. The Debug registration
capture patch only makes its callback fit SmallFunction capacity. The common object
dependency and its 27-patch chain remain unchanged. The earlier transport
pin and probe-based verification are historical and recorded separately in
[the original validation](../validation/shared-node-aether-2026-11-11.md).

`AetherByteLink` implements `IByteLink` over SDK `P2pStream` and
`P2pSafeStream`. It sends the frozen native SharedSyncMessage bytes unchanged
without an application transport envelope. SDK `SizedPacketGate` supplies
whole-message framing; bounded writes through P2pSafeStream fragment messages
larger than its circular buffer. No application offset/length protocol or new
SharedNode field codec is added. The business-message limit is 1 MiB, excluding
SDK framing; larger messages are refused explicitly.

`WatchPeer(uid)` prepares a local peer session; Send can discover a valid
new UID, and incoming SDK ports create or replace the corresponding local peer
session. Endpoint strings are validated and normalized Aether UIDs. Application
membership still comes from SharedNode Join/snapshot, rather than from merely
seeing transport traffic. The adapter and SharedSyncRuntime run on one model
thread. The context, client and AetherApp outlive the adapter, whose subscriptions
are released before its streams and SDK owners are destroyed.
One adapter owns the Client's peer-port subscription; multiple SharedNodes use
that adapter. Bootstrap snapshots take priority over Event retries so a bounded
transport cannot repeatedly deliver an early Event while starving its initial
state.

Presence is disabled by default (`AetherLinkOptions::use_presence = false`),
as requested on 2026-11-12. Valid destinations report Unknown without querying
the SDK or inferring Online from traffic. Send and chunk writes proceed as
transport capacity permits. SharedSync retains every durable packet until its
application ACK and retries it at the configured interval (one second by
default), including while the recipient is absent. A successful SDK write does
not complete the SharedSync obligation. ACK loss causes an identical retry;
the receiver deduplicates the Event and acknowledges its persisted result.

The explicit opt-in presence mode uses `Client::IsLocallyOnline()` and `Client::QueryPeerPresence(uid)`.
The SDK queries the peer's Personal Cloud authoritative servers. Only a fresh
native remote Online result together with local Online permits business
sends. Offline, Unknown, errors and expired results retain the durable retry
obligation without sending business bytes. There is no application probe/echo
fallback. Query start time bounds result freshness; delayed answers cannot
extend it by their arrival time. Queries default to one-second pacing and a
five-second TTL/deadline. The SDK owns one query action per Client, so the
adapter serializes queries round-robin rather than replacing concurrent
queries for different peers. SDK action subscriptions are runtime-only.

At most one business message per peer is queued/in flight, with one native
write at a time. Busy/oversized sends return false. An unwritable SDK stream
keeps one frame queued so stall detection can recreate the session; it must
not refuse every retry with an empty queue forever. SharedSyncRuntime keeps
its durable obligation for retry. With presence
enabled, an accepted frame waits for Online before each further native chunk.
Successful chunk progress does not establish Online. Failed writes and stalled
queued writes (without presence, or with observed Offline) permit bounded
session recreation, resetting
the local SDK stream and packet collector. Inbound replacement uses generation
checks to ignore old callbacks. Recovery properties are limited to executed
scenarios in the corresponding validation record.

## Minimal Windows chat

The chat uses the default ACK/retry mode without availability queries. The
previous GetClientTiming blocker applies only to the opt-in presence mode;
its historical evidence is in [native-presence validation](../validation/shared-node-presence-streaming-2026-11-11.md).
The current publication/storage contract is recorded in
[event-driven RAM validation](../validation/shared-node-chat-event-driven-2026-11-12.md).
Earlier saved-profile checks are in
[historical ACK/retry validation](../validation/shared-node-ack-retry-2026-11-12.md).

`APPTRAVERSE_BUILD_SHARED_NODE_CHAT=ON` requires the optional Aether link and
builds `shared_node_chat`. The window contains the local UID, Host room,
invitation/Join, participant names, message history and
input/Send. Fixed English labels follow the explicitly requested no-localization
scope. Each running participant needs a distinct `--profile`; `--name` supplies
the display name. `examples/shared_node_chat/run_demo.ps1` opens three windows.

The native model contains an Application, a SharedNode Room, Participants with
serializable link descriptors, Join Events and Text Events. A host UID plus
native room ID is the invitation. A small reflected BinaryArchive application
frame routes either a JoinRequest or unchanged SharedSync bytes. The host
admits each UID once; repeated requests retain the original frozen admission.
After admission all participants send their own Events directly to every peer.
The runtime handles snapshot coverage, ACKs, retry, ordering and duplicate
suppression. The chat adds no separate journal or message index.

The SDK, Domain and sync runtime live on a model thread. GUI commands enter
through `ModelObjectProxy`; the UI consumes immutable value publications.
ChatView is published only after a materialized Node change or a status
transition, through the existing Node notifier. A callback posts a Windows
message; the UI consumes the view when handling it. There is no regular
publication/refresh timer, and no presentation work for ACKs/retries/idle.

The demonstration owns chat objects and receipt metadata in RAM; its
SharedSync persistence callback accepts that in-memory transition. Chat graph
Save/Load, disk checkpoints and automatic shutdown saves are not used. Closing
the process loses its room/history/pending state. An embedding application
chooses its own object-storage policy. Only SDK identity uses the private
profile; it does not restore chat state. Previous persistent-demo validation
is historical and is not a RAM-chat restart guarantee. Runtime
transport objects never enter the shared graph. The example implements one
room and no room merging, moderation, attachments or unrelated controls.

## Next slices

1. Compact identity metadata and native storage reclamation only after a
   separate delayed-packet/identity contract; current checkpoints retain the
   stamp ledger and do not perform global object-store garbage collection.
2. Different-build schema/capability compatibility and broader network fault
   characterization. Dynamic topology and arbitrary
   mutable dependency graphs need their own application conflict/replay tests.

The current admission contract is a fixed three-participant scenario; memory
and optional real-cloud execution evidence have separate validation records.
Removal/rejoin, admission by multiple coordinators, host failover during
admission and generic merging of independently formed rooms are not implemented.

Ordering remains by `timestamp_us` within the agreed single-computer scope.
Event identity does not become a hidden ordering key. Equal-timestamp behavior
must be settled before claiming general convergence. Cross-machine clock
ordering remains outside the demonstrated scope.

## Headless entry point

In a Debug build configured with `BUILD_TESTING=ON`:

```sh
cmake --build build/shared-node-debug --target apptraverse_shared_node_headless_check
```

This builds and executes seven native/memory test executables, including live
delivery/restart, three-participant admission and new-only graph import, without
historical GUI examples.
Validation: [foundation](../validation/shared-node-native-memory-2026-11-11.md),
[live delivery](../validation/shared-node-live-memory-2026-11-11.md),
[three participants](../validation/shared-node-three-peer-memory-2026-11-11.md),
[indexed delivery](../validation/shared-node-indexed-memory-2026-11-11.md),
[confirmed checkpoints](../validation/shared-node-checkpoint-memory-2026-11-11.md),
[Aether transport](../validation/shared-node-aether-2026-11-11.md).
