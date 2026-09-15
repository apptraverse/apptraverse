Status: active
Progress: Progress.md

# App Traverse — next application

This plan sequences the next App Traverse work after surfaces.
Architecture of later stages is recorded here and is not redesigned
in a slice that does not own that stage.

Status vocabulary: implemented / verified / accepted-by-user are distinct.
Do not mark accepted.

Coding-agent rules (incremental build, fail-fast, no extra entities, no RTTI,
Event-only Node mutation, commit/push):
`.cursor/rules/apptraverse-coding-agent.mdc`.

## Current canonical application branch

**`surfaces-demo`** — always resolve the live SHA with
`git rev-parse origin/surfaces-demo` (do not treat a SHA in this file as
authoritative without fetch).

Observed at surfaces freeze (2026-09-12), after Linux + Apple adaptive merges:

`677a5f7adc5e401fb796a492f0fe7abe588a41b4`

### surfaces_demo — FEATURE COMPLETE / FROZEN FOR NOW

`surfaces_demo` is **feature-complete and frozen** for architectural work:

- do **not** add SharedNode / chat architecture into this demo
- performance optimizations and known platform limitations stay deferred
- regression fixes may land separately
- next architectural demo is generic **shared_node_demo** (headless first)

### Platforms on the final surfaces line (source-verified on canonical)

| Platform | Host | Adaptive orientation (model presentation size) |
|---|---|---|
| Windows | Win32 multi-window | **DONE** — `WM_SIZE` → Event → publication → `IsWide` |
| Android | pager + `mobile_current` | **DONE** — host size Event → LinearLayout from `IsWide` (+ rotation) |
| Web / WASM | tabs + IndexedDB checkpoint | **DONE** — resize → Event → flex from `IsWide` |
| Linux | **GTK3** (`LinuxSurfacePresenter`) | **DONE** — size-allocate → Event → `GtkBox` from `IsWide` |
| macOS | SwiftUI content in AppKit window | **DONE** — size report → Event → HStack/VStack from `IsWide` |
| iOS | SwiftUI content in UIKit pager | **DONE** — size report → Event → HStack/VStack from `IsWide`; portrait/landscape allowed |

Status vocabulary: **implemented** on canonical source. Platform runtime verification
was reported by the owning Cursors; this freeze task does **not** re-run those
tests and does **not** mark **accepted-by-user**.

### Linux GTK3 adaptive (landed on canonical)

- Was developed on `feature/surfaces-linux-adaptive-final-v1` @
  `d7b47ac60c6fd94a8b96933e01d38e007fbf7f10`
- Merged into `surfaces-demo` (see `Merge Linux adaptive presentation…`)
- Path: size-allocate on stable content host → `PresentationSizeChanged` →
  model Event → publication → `IsWide` → `GtkBox` orientation
- **Raw X11/Xlib is historical.** It is not the intended current Linux path.

### Apple adaptive (landed on canonical)

- Was developed on `feature/surfaces-apple-adaptive-final-v1` @
  `ef01ed8e3c22cf5fd8807507eebce49bd5572afc`
- Merged into `surfaces-demo` (see `Merge Apple adaptive presentation…`)
- macOS: SwiftUI HStack/VStack from published `IsWide`
- iOS: same; `UISupportedInterfaceOrientations` includes landscape;
  orientation enum is not the source of truth for controls

### Model-driven adaptive orientation contract (common)

On the full canonical surfaces line:

- `Surface::presentation_width` / `presentation_height` (persisted; not desktop placement)
- `SurfacePresentationSizeChangedEvent`
- `Surface::SetPresentationSize` / `SurfacePresenter::PresentationSizeChanged`
- `SurfacePresenter::IsWide()` ⇔ `presentation_width >= presentation_height`
- Native resize only **reports** size; native UI must not decide orientation
  outside the published GUI mirror

### Historical note (superseded)

Older docs referred to “Linux X11/Xlib” and “Linux host remains X11/Xlib”.
Those statements described an earlier port and are **not** the intended final
Linux surfaces path.

## Surfaces roadmap (status)

1. main_window_runtime_demo — lifecycle/mirror foundation [done]
2. journal retention/compaction [landed]
3. dynamic_objects_demo — Add / Remove Item [done]
4. foundation hardening + UI ownership / ObjId proxy [done]
5. dynamic_objects cleanup (invariants / Win32 routing) [done]
6. Disable RTTI + invariant-driven coding policy [done]
7. surfaces_demo — common model + headless [done]
8. surfaces_demo — Windows multi-window + persisted geometry [done]
9. surfaces_demo — Android pager + `mobile_current` [done]
10. surfaces_demo — Web/WASM tabs + IDBFS checkpoint [done]
11. surfaces_demo — Linux desktop host [done as **GTK3**; X11 was historical]
12. surfaces_demo — macOS desktop port [done]
13. surfaces_demo — iOS / iPhone Simulator [done]
14. pre-shared runtime hardening [done]
15. model-driven adaptive presentation size — Windows / Android / Web [done]
16. model-driven adaptive — Linux GTK3 [done on canonical]
17. model-driven adaptive — macOS / iOS [done on canonical]
18. surfaces_demo — **FEATURE COMPLETE / FROZEN** (architectural work stops here)

Deferred relative to surfaces / SharedNode:

- Node execution / marquee demo
- Resource / version / cache
- DPI / screen system events
- Full desktop Z-order stack (only active Surface restored on desktop;
  Windows + Linux + macOS restore via `mobile_current`)

## Canonical current Surface

Persisted identity: `Surfaces::mobile_current` (Surface reference / ObjId),
not a positional numeric index. UI may keep a runtime page index.

Required APIs: `SetCurrentSurfaceEvent`, `Surface::MakeCurrent()`,
`SurfacePresenter::PageShown()`.

Mobile/Web report the visible page through `PageShown`.

Windows, Linux, and macOS desktop also use `mobile_current` for active-window /
Z-order restore: focus → `PageShown`; on startup raise + focus the persisted
current Surface.

Web checkpoints `Application::Save` after each model publication, then IDBFS
sync — browser reload/tab close is not a reliable graceful shutdown. This is
**Web-host policy only**, not common `SurfacesModelSession`, and must not be
copied onto Windows/Android/Linux/macOS/iOS.

## Old shared-chat experiment — retired as architecture, removed from the tree

`feature/shared-chat-headless-v1` @ known
`63abddeeb57b78fdb7cfa4dc2a459785fa6566b8`
is an **experiment / reference only**.

It is **not** the basis of the new SharedNode architecture.

Its runtime is **deleted**, not deprecated: `SharedInstance<TNode>`,
`SharedRuntime`, `PeerDeliveryState`, `PeerInFlightEntry`,
`DeferredIncomingEvent`, `SharedWriteState`, the
`shared_room_id` / `peers[]` / `pending[]` / `in_flight[]` / `channel_ready`
delivery state, `ISharedTransport` with its `SharedEventFrame` /
`SharedAckFrame` codec, and the chat binding and Æther Win32 adapter built on
them are gone from the repository. There is one sharing architecture:

```
SharedNode + Share/share_id + Link + LinkSyncState + SharedSyncRuntime
           + IByteTransport (MemoryTransport / MemoryNetwork)
```

`LinkSyncState` is the only persistent per-Share delivery state, and
incremental Event replication (milestone 08) extends that architecture rather
than reviving a second one.

Useful concepts to retain from experiments (including older sync sessions):

- independent Domains / Storages per replica
- serialized in-memory transport tests
- duplicate suppression
- repeated ACK after duplicate
- lost ACK / retry scenarios
- restart persistence concepts

**Do not carry forward** as the final design:

- Chat-specific Event remapping as the shared core
- `SharedInstance<Chat>` as the final SharedNode model
- room-id coupling as universal topology
- special Join / presence-controller architecture
- `ISharedTransport::SendEvent` / `SendAck` as the universal Link API

## NEXT direction — generic sharing first

`surfaces_demo` is frozen. Next architectural work:

```
sharing (generic SharedNode, headless)
  → memory chat in one process / two windows
  → host + multiple participants
  → Æther Link transport
  → separate processes
  → AeroAdmin-X product chat
```

**No GUI** in the first SharedNode milestones.
Do **not** implement SharedNode inside `surfaces_demo`.

---

# SharedNode / Link — current working design

The following is **working design**, not implemented fact, unless a later
slice marks a milestone implemented.

## Link

Link is a **persistent object** in the application graph.

Persistent transport descriptor / config may include:

- transport kind / type
- UID / address / broker topic / subscription identifiers as needed
- heartbeat / availability configuration
- other transport-specific serializable parameters

Runtime-only (not serialized as durable Link state):

- sockets, subscriptions, callbacks, native handles
- active operations
- current observed availability

Do **not** serialize a universal `is_local`.
Each runtime decides which Link is itself from its local transport identity.

One Link may be referenced by:

- multiple SharedNode objects
- ChatClient / participant business model
- file-transfer peer model
- other business Nodes

**Presence belongs to Link**, not to every SharedNode.

## SharedNode topology

SharedNode derives from the existing Node architecture.

Shared part includes roughly:

- `shares[]`: Link reference + access for this SharedNode (initially RW / RO)
- business state + shared journal

The Link topology itself is shared.

Example chat room:

```
ChatRoom
  AliceLink  RW
  BobLink    RW
  NickLink   RW
```

A newly attached participant receives the room and Link transport descriptors,
so it knows participants / connectivity endpoints.
Which Link is local differs by runtime.

## Share relationship identity

A Link is a long-lived connectivity endpoint. A **Share** is one lifetime of
`SharedNode <-> Link`. Remove + re-add over the same Link is a **new**
relationship and must not inherit the previous one's delivery state.

`Share::share_id` is that identity: the `ObjId` of the `AddShareEvent` that
opened the relationship. It is shared topology state (it crosses a network
graph copy), it survives Save/Load and journal replay, and `RemoveShareEvent` /
`ChangeShareAccessEvent` name it instead of the Link.

`LinkSyncState` carries the same `share_id`, so replaying a historical
`AddShare` / `RemoveShare` of the same Link cannot consume the local sync state
of the currently active relationship. No post-replay repair pass exists.

## Per-Share delivery state is local-persistent

Critical distinction:

- SharedNode **sharing topology** is shared.
- Delivery progress for Node N through one Share relationship is
  **local persistent state** of that replica.

Examples of local-persistent metadata:

- what the remote side already ACKed
- pending packets
- exact serialized retry bytes
- initial-sync progress
- received / dedup information as required

A’s knowledge “B ACKed E17” is not B’s shared state.

It **must** survive restart.
It **must not** be overwritten by incoming shared snapshot / replay.

**Settled for initial state:** a network snapshot carries no `LocalPtr`
referent, so an imported SharedNode arrives with one empty local slot per
Share. The rebuild stash carries only entries that hold state, and replaying
the imported shared journal creates the receiver's own `LinkSyncState` for
every Share, keyed by the `share_id` that travelled with the topology. No
field repair, no class switch, no patched topology.

## ACK / durability contract (first sharing guarantee)

Intended first path:

1. sender: create / freeze packet → persist pending exact bytes / identity → send
2. receiver: decode / validate → apply shared state / Event → persist accepted /
   dedup state → send AppTraverse ACK
3. sender: receive ACK → persist delivered state → remove pending

Lost ACK: duplicate accepted as duplicate → no second Apply → ACK again.

Do not claim arbitrary torn-write atomicity.
Storage I/O failure handling remains out of scope unless separately assigned.

Restart tests must really destroy and recreate Application / Domain / runtime.

## Transport / Link contract

One Link may carry multiple SharedNode protocols.

Incoming transport callback provides:

- source Link / endpoint identity
- opaque bytes

Shared sync frame must contain **target SharedNode identity**, because source
alone cannot identify the target Node.

Transport examples:

- Æther: sender UID can come from the receive callback
- Broker: subscription / topic mapping may identify source / destination

Transport does **not** understand ChatMessage, EditorProfile, or business
Event types.

## Memory transport first

Headless first transport (Memory Link) must support:

- opaque message delivery
- source identity
- disconnect / reconnect
- directional loss, drop, duplicate, reorder
- deterministic / fake clock
- heartbeat
- local / remote availability

Initial scenarios assume continuously connected clients.
Sleeping / Poll / Push windows are later; the abstract Link contract must not
make Æther capabilities impossible.

## Presence

Presence / availability is a **Link** capability.

Business model may reference Link for online status.
Shared sync scheduler reads the same Link availability.

No special `if local participant … else remote …` branches for sync core.

Old observed Online is not trustworthy immediately after process restart;
runtime re-establishes observation.

Presence is **not**: Event ACK, membership, synchronized state, or permission.

## RW / RO and visibility are orthogonal

**RO does not mean “do not send Links”.**

A read-only replica may need Link descriptors to:

- subscribe to broker topics
- receive traffic
- send ACK
- identify connectivity endpoints

Example: Alice and Bob edit a shared table; Viewer is RO and must not see
editor profiles.

Viewer **may** receive: AliceLink, BobLink, ViewerLink, shared document content.  
Viewer **may not** receive: AliceEditorProfile, BobEditorProfile.

Therefore:

```
transport Link visibility
  ≠ business object visibility
  ≠ RW / RO modification rights
```

Do **not** reintroduce rejected `anonymous_read` as the answer to this problem.

## Recipient-scoped graph edges (planned)

Need a mechanism for graph edges / fields conceptually:

- shared to all recipients
- writers only / recipient filtered
- local persistent only

External / shared-reference semantics also remain relevant (e.g. a document
may contain a TableSnapshot bytes while a reference identifies another
shared resource without shipping the full editor topology immediately).

Do not finalize API names until implementation proves them.

## Localization / remote resource note

Localization strings can technically be SharedNode state changed through Events.
Immutable released resources may instead use a resource reference / version /
hash with a separate resource subsystem for bytes.

These are distinct use cases — do not force either into the first SharedNode
implementation.

## Event order (explicit decision) — implemented

Shared Event **order is `timestamp_us` only**. `SharedEventOrder` holds that one
field, `SharedEventOrderLess` compares that one field, and journal position
follows from nothing else.

Shared Event **identity is separate**: `SharedEventId` (`origin_uid`,
`origin_sequence`) is the only cross-replica logical identity and is what
deduplication works on. `Event::ObjId` is replica-local object identity: two
Domains may give the same Event different ObjIds, and a receiver must allocate
its own. Identity never enters ordering, and no secondary sort key exists —
not origin, sequence, ObjId, endpoint, Share id, or insertion index.

A replica's own consecutive commits still get strictly increasing timestamps
(`Node::CommitInto`). That is a wall-clock
adjustment over one replica's own sequence, not a logical clock: remote Events
never advance it and keep the timestamp they were sent with.

Equal-timestamp behavior across independent sources remains an open edge case.
Two different Events may carry the same timestamp; where they land relative to
each other is `std::lower_bound` behavior on that replica, not agreed order,
and convergence for that case is not claimed. Do not silently solve it by
adding a second sort key.

---

# Planned implementation ladder

**Milestones 01–04 foundation hardened on `feature/shared-node-foundation-v1`
(v1.1 corrections). Initial-state synchronization (05–07, 09 initial subset),
standalone incremental Event replication (08, 09 Event subset, 12 source
access), and closed Event graph serialization slice are on `main`. Dynamic
object graphs, topology Events, multi-hop, presence, and Æther are not started.**

### SharedNode headless (01–16)

01. **Introduce persistent Link descriptors** — **implemented/verified** (`Link` / `MemoryLink`; config set before `InitializeRuntimeNode`).  
02. **Add SharedNode share topology** — **implemented/verified** (`shares[]` + Add/Remove/ChangeShareAccess Events).  
03. **Separate shared and local-persistent graph edges** — **implemented/verified** (generic `LocalPtr` + `GraphCopyPolicy::NetworkShared`; no SharedNode sanitization; rebuild stash).  
04. **Persist per-Share SharedNode sync state** — **foundation implemented/verified** (`LinkSyncState` Event-sourced Node + `InitialSyncPhase` only; keyed by Share relationship identity so RemoveShare+AddShare starts a new relationship at NotStarted and a forced `RebuildFromBaseAndReplay` keeps the current relationship's progress; no ACK/pending bytes yet).  
05. **Add generic shared sync framing and routing** — **implemented/verified for NodeState, Ack, and standalone Event** (protocol v1 frames routed by `target_node_id` and named by `destination_share_id`; canonical frame length and non-zero ids required; every frame bound to the transport `source_endpoint`; `SharedSyncRuntime` per replica).  
06. **Add deterministic Memory Link transport** — **message delivery subset implemented/verified** (opaque bytes, endpoint identity, deliver / drop / duplicate / disconnect / reconnect, no threads or sleeps; no heartbeat, presence, reorder, or fake clock yet).  
07. **Synchronize a SharedNode to a newly attached Link** — **implemented/verified** (freeze + persist + send, admission of the snapshot in a scratch Domain before any write to real storage, import into the receiver Domain, receiver-local sync state by journal replay, persist before ACK, duplicate acknowledged without re-apply).  
08. **Replicate incremental SharedNode Events** — **standalone scalar subset implemented/verified** (`EventFrame` + generic `Ack`; one pending Event packet per Share; `SharedEventId` is the only cross-replica identity; receiver allocates a fresh local Event ObjId; Event graphs that reach a second object are refused; pre-LoadRoot class chain validation and scratch preflight replay guarantee safe admission). **Closed Event graph serialization slice implemented/verified** (Freeze, Parse, Validate, and Import for closed Event graphs referencing Nodes and Objs with aliases, remapped `ObjIds` avoiding receiver collisions, remapped `Node::base`, excluded `LocalPtr`s, invariant checks in disposable scratch, zero receiver mutation on failure, and explicit export boundary; wire integration into `EventFrame`/ACK deferred). Dynamic child-object graphs, topology Events, and multi-hop are not started.  
09. **Make shared delivery restart-safe** — **implemented/verified for initial state and standalone Events** (sender restart while pending resends the same packet id and bytes, receiver restart after apply still recognizes the duplicate from the journal, sender restart after ACK keeps the identity delivered and does not resend).  
10. **Replicate dynamic SharedNode graphs** — topology changes as shared Events.  
11. **Share multiple Nodes over one Link** — multiplexing proof.  
12. **Enforce RW and RO sharing rights** — **source access implemented/verified for incremental Events** (ReadWrite source required to mutate; ReadOnly destination may still receive). Recipient-filtered graphs and writer-vs-reader edge visibility are later.  
13. **Add recipient-scoped object references** — filtered graph edges for writers vs readers.  
14. **Prove full share topology with three replicas** — A/B/C memory convergence.  
15. **Integrate Link presence with SharedNode delivery** — scheduler observes Link availability.  
16. **Freeze shared_node_demo headless contract** — documented PASS criteria for headless sharing.

### Chat ladder on SharedNode (17–21)

17. **Build ChatRoom on SharedNode** — product chat model uses generic sharing, not a special Chat sync core.  
18. **Add in-process two-window memory chat** — one process, two windows, Memory Link.  
19. **Add multi-participant host chat** — host + several participants on memory transport.  
20. **Add Æther Link transport** — replace Memory Link with Æther without redesigning SharedNode.  
21. **Run chat replicas in separate processes** — then AeroAdmin-X product chat.

---

# Open questions

Do not resolve casually in documentation:

- exact public vs local/private Link fields
- transport credentials serialization boundary
- who is authorized to modify shares / access topology
- direct peer mesh vs host relay
- exact equal-`timestamp_us` behavior
- recipient-filtered Event dependency semantics
- deletion + delayed Events
- exact class / layout of local-persistent per-Share sync metadata
- ~~transport-stable derivation of `Share::share_id`~~ — **closed**: the
  initial-state snapshot carries the Share topology and its `AddShareEvent`
  identities, so the receiver imports the same `share_id` instead of
  re-deriving one from a receiver-side Event.
- durability beyond explicitly tested Save boundaries
- future compaction / frontier
- external resource / reference semantics

---

## SwiftUI view layer (Apple platforms)

*(Host architecture — unchanged; adaptive orientation status is separate above.)*

SwiftUI owns window/page **content**. It does not own the window set, window
lifecycle, frames, or Z-order: those follow from the model graph
(`Surfaces::surfaces`, `desktop_*`, `mobile_current`) and stay with the
presenter. `WindowGroup` is deliberately not used — it would move the window
set into SwiftUI scene storage and break both the geometry contract and
`RestoreActiveSurfaceZOrder`.

Boundary (macOS):

```
SwiftUI view (SurfaceContentView.swift)
  → id<MacSurfaceActions>   (pure Objective-C protocol; SurfaceWindowDelegate)
  → MacSurfacePresenter     (AddClick / RemoveClick / PageShown)
  → ModelObjectProxy → model event
```

Boundary (iOS), same shape through the single mobile host:

```
SwiftUI view (SurfaceContentView.swift)
  → id<IOSSurfaceActions>   (pure ObjC protocol; SurfacesRootViewController)
  → IOSApp                  (AddCurrentClick / RemoveCurrentClick)
  → current IOSSurfacePresenter → ModelObjectProxy → model event
```

Two constraints fix this shape:

- Swift's clang importer is Xcode's (Apple Clang 15) and cannot parse pinned
  `aether-miscpp` C++, while the C++ needs `clang++-mp-20`. So the
  Swift-visible header is pure Objective-C and all C++ stays in `.mm`.
- The single ObjC++ → Swift call is `@_cdecl`
  (`ApptraverseInstallMacSurfaceContent`): it installs an `NSHostingView` into
  the presenter's window. No generated `-Swift.h` (that header needs clang
  modules, unavailable under `clang++-mp-20`) and no object ownership crosses
  the boundary.
- Swift targets link with the ObjC++ driver (`LINKER_LANGUAGE OBJCXX`); the
  Swift driver rejects the C++ policy flags such as `-fno-rtti`.

iOS adds three constraints of its own:

- The pager stays UIKit. `TabView(.page)` or a SwiftUI app lifecycle would move
  page order and the current page into SwiftUI state, breaking `mobile_current`
  identity and the desired-id reconcile that `IOSApp` uses for swipe races.
  ObjC++ keeps every container `UIView` and its frame (`RelayoutPages`,
  `viewDidLayoutSubviews`); SwiftUI only draws inside a container.
- iOS has no public `UIHostingView`, so content is a `UIHostingController`,
  which its own view does not retain. It is attached with
  `objc_setAssociatedObject` to the container UIKit already owns: that ties its
  lifetime to the container and lets the stateless update entry point
  (`ApptraverseUpdateIOSSurfaceBar`) find it again without global Swift state.
- Model-derived control state is re-supplied, never mirrored. Every publication
  passes `RemovableFromPager()` back in, so `Remove current` has no Swift state.

SwiftUI macOS buttons are private `NSControl` subclasses with no title and no
`accessibilityIdentifier` on the `NSView`, and SwiftUI builds its accessibility
tree only for an attached AX client. Native smoke tests therefore drive
`MacSurfaceActions` (the production boundary SwiftUI calls) and keep native
assertions for window geometry, key window, and Z-order.

## Presenter hierarchy

```
SurfacePresenter
  ↓
DesktopSurfacePresenter
  ├─ Win32SurfacePresenter
  ├─ MacSurfacePresenter
  └─ LinuxSurfacePresenter   — GTK3 (intended); X11 was historical

SurfacePresenter
  ↓
MobileSurfacePresenter
  ├─ AndroidSurfacePresenter
  └─ IOSSurfacePresenter

SurfacePresenter
  └─ WebSurfacePresenter     — not under Mobile
```

## Known follow-ups (not this slice)

- Runtime base snapshot (`CaptureBaseState` / `DomainGraph::Save`) may write
  storage before explicit `Application::Save` (characterized: 2 `Store` calls
  per `InitializeRuntimeNode`). Defer Overlay flushing / persistence redesign.
- SharedNode replay allocates a transient `LinkSyncState` for every historical
  relationship it re-applies (erased again by the matching `RemoveShare`, never
  saved). Revisit only if replay allocation cost shows up.
- A NodeState frame is the state at freeze time. Changes the sender makes
  afterwards are not covered by its ACK and are resent incrementally via
  milestone 08 (incremental Event replication). FrozenNodeState produces both
  payload and covered_event_ids atomically in one pass.
- A second, different initial snapshot for an already imported relationship is
  rejected instead of applied: protocol v1 has one initial packet per Share.
- Snapshot admission validates structure, class inheritance chains, relationship
  identity, endpoints, and journal historical replay validity before
  persisting or mutating. Incremental events undergo preflight historical
  replay in a scratch node copy before admission.
- Frames are bound to the endpoint identity the transport reports. Whether that
  identity is authentic is the transport's problem (Æther, milestone 20); the
  protocol runtime adds no signatures or crypto of its own.
- The network-shared graph payload reuses the storage encoding of each object
  layer, which is not a portable wire format. Fine for a memory transport in
  one process; revisit before a cross-machine transport.
- Two shared Events with the same `timestamp_us` have no defined order between
  them, and replicas may disagree. Deliberately unresolved: picking a
  tie-break would put identity back into ordering.
- A Node's journal bytes are written into the storage layer of the most derived
  class, so `Node`'s own version does not key any concrete Node's storage.
  [RESOLVED in FIX 01]: Native class-layer serialization restored; each class in
  the inheritance hierarchy now serializes its direct registered base via
  `dnv(base_, ...)`. Node bumped to v4 (Load/Save base_, base, journal); derived
  classes bumped by 1 with explicit rejection of former flattened layouts.
  `kNodeJournalFormat` and format-word markers removed.
- [CHAT DEMO 01]: Common `ChatRoom` (derived from `SharedNode`), `ChatEntry` (Node),
  and `ChatWorkspace` (Node root) implemented with pure projection, native serialization,
  scalar `MessageValue` events, command layer (`OpenOrSelectChat`, `BindChat`,
  `SubmitDraft`, etc.), launch option parser, and 15 comprehensive model regression scenarios.
- [CHAT DEMO 02]: Two-way scalar chat synchronization over `MemoryNetwork`:
  - Sequence overflow protection in `SubmitDraft` and `MessageSequenceReservedEvent::CanApply`.
  - Chat selection membership validation in `SelectChat` and `ChatSelectedEvent::CanApply`.
  - Message payload metadata consistency verification via `Event::MatchesSharedMetadata` and `Node::TryCommitSharedInto`.
  - Standalone-scalar Event allow-list in `SharedSyncRuntime` (`AllowStandaloneEventClass`).
  - Standalone Event graph validation and import (`ValidateStandaloneEventGraph`, `ImportStandaloneEventGraph`, `CopyStoredObjectAs`) bypassing `OperationStorage`.
  - Snapshot local object collision rejection in `ImportValidatedNode` before storage writes or ACK.
  - Two-way initial sync completion: `CompleteFromReceivedSnapshotEvent` on `LinkSyncState` marking source Share complete on first snapshot import.
  - End-to-end product test `apptraverse_chat_demo_sync_test` proving two-way messaging, replies, duplicate ACK, restart persistence, and metadata mismatch rejection.
- [CHAT DEMO 03]: Real Aether transport and heartbeat presence:
  - Fixed snapshot admission bug: source-share uniqueness validation occurs in scratch candidate before `CommitObjectGraph` and collision check; returns `ImportedNode` struct; zero storage modifications on failure.
  - Dedicated `IByteTransport` header in `include/apptraverse/byte_transport.h`.
  - Runtime-only `PeerPresence` enum (`examples/chat_demo/common/chat_presence.h`).
  - Native `ChatAetherRuntime` (`examples/chat_demo/aether/chat_aether_runtime.{h,cpp}`) managing `AetherApp`, `Client`, dedicated thread, `P2pStream` peer management, and deterministic stream binding.
  - 14-byte heartbeat protocol with steady-clock Ping/Pong scheduling and timeout-based presence (`Unknown`, `Connecting`, `Online`, `Offline`).
  - `AetherByteTransport` adapter mapping `IByteTransport` to `ChatAetherRuntime`.
  - Headless `apptraverse_chat_aether_probe` for command-line validation and interactive testing.
  - Multi-process test `apptraverse_chat_aether_p2p_test` proving two-process presence detection, application byte delivery, timeout-based offline detection, and end-to-end two-way chat synchronization over real Aether.
- [CHAT DEMO 04]: Model-thread delivery, safe Aether stream multiplexer, and cross-platform build guards:
  - Strict thread isolation: Aether thread owns `ChatAetherRuntime`, client, stream, and frame codec; never calls `SharedSyncRuntime` or mutates model/storage directly.
  - `AetherByteTransport` receives frames via `IAetherFrameEndpoint`, captures only `std::weak_ptr<ReceiveBinding>`, and dispatches incoming frames onto caller-supplied `ModelDispatch` queue (`ModelTask`).
  - Safe transport destruction: unbinds frame callback, clears `active`, nulls `receive_ctx`/`receive_fn`, and resets binding without touching or waiting on model queue.
  - Frame multiplexing envelope (`aether_stream_frame.{h,cpp}`): `'A' 'T' 'R' 'N'` magic, version 1, 10-byte header, with kinds `kApplication` (up to 16 MiB), `kHeartbeatPing` (8-byte nonce), `kHeartbeatPong` (8-byte nonce); complete rejection of malformed frames.
  - Transport-local duplicate suppression on pending outbound frames prior to P2P stream link.
  - Presence: runtime-only, transitions `Offline -> Connecting` on stream re-link, drops malformed frames without updating application state.
  - Model-only build support: `chat_demo_model` builds when `APPTRAVERSE_BUILD_AETHER_DEMOS=OFF` without linking Aether.
  - Cross-platform build guards: POSIX process test `chat_aether_p2p_test` guarded by `if(UNIX AND NOT EMSCRIPTEN AND TARGET chat_demo_aether)`; unit tests `aether_stream_frame_test` and `aether_byte_transport_dispatch_test` compile on all platforms.
- [CHAT DEMO 05]: First Windows chat and reusable chat session:
  - Common `ChatSession` (`examples/chat_demo/runtime/chat_session.{h,cpp}`): manages lifecycle (`kStarting`, `kReady`, `kFailed`, `kStopped`), local workspace graph persistence in `state_dir/model`, separate Aether client state in `state_dir/aether`, and publication channel for GUI mirrors.
  - Strict thread isolation: GUI thread interacts with `ChatSession` exclusively via value-copied commands (`OpenPeer`, `SelectChat`, `EditDraft`, `SendDraft`, `SaveScroll`, `SaveBounds`); model thread executes domain mutations; incoming transport frames dispatched from Aether worker thread onto the model loop.
  - Unknown-node bootstrap extension in `SharedSyncRuntime`: added `ExpectInitialNodeFromEndpoint` and `SetInitialNodeImportedCallback`, admitting `ChatRoom` without prior `ObjId` knowledge from explicitly authorized endpoints; validates root most-derived class before writes and binds entry before sending ACK.
  - Rejection of unknown sources, incompatible root classes, and second/duplicate initial snapshots for already existing nodes.
  - Aether link descriptor: `AetherLink` (`examples/chat_demo/aether/aether_link.{h,cpp}`) reflecting and persisting `endpoint_uid` with registered class ID.
  - Deterministic room creator election: `canonical local UID < canonical remote UID` creates `ChatRoom` and shares; waiting peer expects room from authorized peer endpoint.
  - Win32 host `apptraverse_chat` (`examples/chat_demo/windows/`): system controls (ListBox, Msftedit RichEdit, multiline draft Edit, Send button, connection inputs, status/presence labels), UTF-8/UTF-16 conversion, exclusive `profile.lock` file locking, normal/maximized window placement restoration, and non-tail scroll anchor restoration.
  - Headless integration test `tests/chat_session_integration_test.cpp`: endpoint-authorized room discovery, single creator election, rejection of unauthorized sources and invalid root classes, pre-ACK binding verification, restart persistence of room and queued messages, and private field isolation.
- Publication scaling / full-graph cost.
- Android presenter ownership / UI weaknesses.
- Mobile lifecycle persistence limitations beyond current checkpoints.
- Merge of historical adaptive feature branches is complete; further surfaces
  feature work is frozen (see freeze note above).
- The iOS bundle has no launch storyboard, so iOS runs it scaled from a 320×480
  logical screen and in the light appearance. Layout and the SwiftUI content are
  correct inside that box; native full-screen geometry is a separate slice.
- iOS has no user-driven application close, and `simctl terminate` does not
  deliver `applicationWillTerminate`, so `Application::Save` does not run on a
  simulator kill: the newest topology change reloads from the earlier runtime
  write. Pre-existing, identical before the SwiftUI port.

## Cross-platform invariant (foundation)

```
native input
  → platform presenter
  → common presenter method
  → ModelObjectProxy(ObjId)
  → model object method
  → Event / Commit
```

Session/runtime transports work and publications; it contains no application
event semantics.

ModelWork execution is independent of GUI publication consumption.
`PublicationChannel` backpressure delays only the next GUI snapshot.

## Foundation still in force

Independent Model/GUI Domains, Event-only Node mutation, presentation_load_order,
structural keepalive (`Domain::Find`), instance-scoped Node materialized-change
notifier (no process-global callback), native-X app STOP + Close-button Remove,
shutdown geometry snapshot before RequestStop, distill separation, no RTTI,
invariant-driven checks.
