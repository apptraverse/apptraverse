# SharedNode: host admission and three memory participants — 2026-11-11

Status: **PASS**, Windows MSVC x64 Debug; memory transport only.
Branch: `feature/shared-node-native-memory`; starting commit: `08e951c`.
Extends [two live replicas](shared-node-live-memory-2026-11-11.md).

## Behavior

H admits B with an ordinary application Join Event containing Bob's native
participant graph and Link UID. `AdmitPeer` freezes Join, one initial state and
an admission plan with exact coverage, then persists the obligations before
transmission. H sends Join to A and initial state to B. Each participant sends
its own later Events directly to both other endpoints; H is not an Event relay.

A installs coverage suppression before creating outgoing work for B. Assigned
history is not yet a remote ACK. B's saved initial receipt lets H queue reliable
CoverageReceipt delivery to A in the same persistence step as initial
completion. A persists its confirmation before ACKing the receipt. Notice
completion has separate metadata and never becomes a business Event ID.
Receipt-before-Join is retried; duplicate Join/receipt is idempotent.

Frozen authored frames are retained separately from per-peer pending. If H
freezes a snapshot, later accepts and ACKs A's older Event, and only then A
receives Join, A can still deliver the original pre-Apply bytes to B. Events
already covered by the initial state produce no incremental A→B replay.

Coverage is directional: confirmation for B does not clear H→A pending.
Snapshots with a lost ACK may repeat unchanged and are re-ACKed without a
second import or replacement of the live room. Offline suppresses sends to
that destination while the other two participants keep exchanging directly.

New native messages use wire 1.1. The exact 1.0 reflected layout remains
readable/writable for frozen data packets and matching ACKs. Admission needs
1.1; a lossy downgrade of admission/control packets to 1.0 is rejected. There
is no claim of negotiated mixed-build admission. Local schemas are
S2 with S0/S1 metadata and old QueueSharedPacketEvent loaders. Frozen pending
bytes remain unchanged. Old S1 origin IDs without retained pre-Apply frames
cannot recreate uncovered history for a new peer.

## Validation commands

Same incremental `build/shared-node-debug`, Windows MSVC x64 Debug, no C++ RTTI
or exceptions, unchanged pinned object dependency and main's 27-patch chain.
No fresh dependency population, Release or real network client.

```sh
cmake --build build/shared-node-debug --target apptraverse_shared_node_headless_check
```

Final result: **7/7 PASS** (3.78 s), including
`apptraverse_shared_three_peer_test` (2.91 s) and the six existing native/memory
entries. The regression targets were also built incrementally (`ninja: no work
to do`). Existing MSVC `/EHsc` override warnings are unchanged.

The new executable contains eight scenario groups:

| Scenario | Required outcome |
| --- | --- |
| Join before snapshot | A immediately sends uncovered Events directly to B; covered A history remains suppressed, not falsely confirmed |
| Snapshot and receipt before Join | Early B Event and receipt get no success ACK; retry converges after Join |
| A history missing from snapshot | An Event ACKed by H after freeze still reaches B using its original retained frame, including after A restart |
| Directional ACK | B's snapshot receipt cannot clear an H-origin Event still pending for A |
| Lost control packets and restart phases | Join, snapshot, initial ACK, coverage receipt and receipt ACK survive losses and restart with unchanged identity/bytes |
| Offline and all three restarting | One offline destination does not block the other peers; direct pending survives complete destruction/restore and reverse-order delivery |
| Host offline after admission | A and B exchange directly while their H-directed obligations remain pending |
| Legacy native Event after admission | Retained wire 1.0 Event bytes receive a matching 1.0 ACK and complete delivery |

The fixture uses independent Applications/Domain/storage snapshots. Test-only
address tracking verifies actual canonical App/Room destruction and forbids a
teardown Save. Each outgoing data/initial ACK is checked by a cold reader
through the saved Application edge; receipt ACK checks persisted coverage
confirmation. Exact per-origin/destination send counts detect retransmission
of covered incremental history and accidental host relaying.

Targeted regressions:

```sh
ctest --test-dir build/shared-node-debug --output-on-failure -R '^apptraverse_(event_sourced_core|object_clone|historical_base_materialization|model_persistence|single_file_user_storage|object_scope_storage|application_contract_headless|window_catalog_headless|minimal_app_headless)_test$'
```

Final regression result: **9/9 PASS** (26.87 s).

A mutation removed `IsCoveredByAdmission` from outgoing scheduling while
retaining ordinary delivered-ID checks. The three-peer test failed at
`Count("A", "B", a20) == 0`: A retransmitted history already assigned to B's
snapshot. The mutation was reverted, rebuilt, and all seven final headless
entries passed. This checks the no-history-resend oracle independently of the
happy-path convergence assertions.

`git diff --check`: **PASS**.

## Limits

Fixed H/A/B admission, source-authored fanout and memory transport are the tested
scope. Removal/rejoin, another coordinator, host failover during admission,
independently formed room merge and automatic journal compaction remain
separate work. The host may restart and resume saved obligations; another
participant does not take over those obligations.

Native IDs must represent the same logical objects. General same-class ID
collision remapping and arbitrary mutable dependency semantics are not proven.
Timestamps are distinct on one computer; no hidden tie-break is introduced.
Persistence success remains the boundary: durable RAM graph snapshots prove
destruction/restore, not arbitrary torn filesystem writes or OS process failure.
Heartbeat detection, GUI, real Aether, other-platform runtime and full client
schema negotiation are outside this slice.
