# SharedNode: author indexes and continuous ACKs — 2026-11-11

Status: **PASS**, Windows MSVC x64 Debug; memory transport only.
Branch: `feature/shared-node-native-memory`; starting commit: `6da9399`.
Extends [three memory participants](shared-node-three-peer-memory-2026-11-11.md).

## Implemented behavior

Every new local Event and host Join receives `(author UID, index, Event ID,
timestamp)` through native reflected serialization. Indexes are per author per
SharedNode and start at 1. The immutable receipt ledger, original authored
frames and per-recipient acknowledged cursors persist in the local metadata
subtree. Journal ordering remains timestamp-only.

A received Event may arrive out of order. Its stamp is checked for Event/index
reuse and strict author timestamp ordering against both earlier and later known
indexes before live insertion. An ACK reports the largest continuous prefix,
not the maximum observed index. If index 3 arrives before 2, the prefix remains
1; indexed pending 2 and 3 both survive. Receipt of 2 permits one ACK to confirm
both. Zero prefix confirms no indexed data. Source, Node, original trigger
stamp and the saved author boundary are checked before completion. Progress is
directional and monotonic; a legacy ACK cannot complete indexed pending.

Initial state transfers the known stamp ledger explicitly as typed native
message data. Each entry must match an actual journal Event/timestamp, an author
Link, and unique Event and author/index identities. Sparse coverage remains
sparse after import and restart. Snapshot-covered Event delivery does not imply
that a gap in that author's stream was received. Existing admission suppression
and reliable coverage receipts continue to prevent redundant incremental replay.

Explicit author timestamps must strictly exceed the saved author time and
bootstrap floor. Invalid times are rejected before journal mutation or Save.
Automatic time is at least the current clock and strictly exceeds author/floor
time and the journal tail; exhausted counters/timestamps are rejected. A
new/imported local root captures
its snapshot-tail floor before initializing its base. Restart restores the
counter, floor and sparse receipt ledger. Legacy metadata has no guessed floor;
retained legacy local Event timestamps still constrain new authoring.

New wire messages use **1.2**. Exact native **1.0/1.1** tuple layouts remain
supported for old frozen packets and matching ACKs. Older inline packet and
local metadata schemas load without rewriting bytes; S3 adds the stream ledger
and confirmed cursors. Old history stays unstamped. There is no automatic
mixed-build negotiation or silent downgrade of indexed packets.

## Validation

Same incremental `build/shared-node-debug`, Windows MSVC x64 Debug, no C++ RTTI
or exceptions. Unchanged pinned object dependency and main's 27-patch chain;
no fresh dependency population or Release build.

```sh
cmake --build build/shared-node-debug --target apptraverse_shared_node_headless_check
```

Profile result: **7/7 PASS** (6.40 s), including the three-peer executable
(5.50 s) with fourteen scenario groups. Existing MSVC `/EHsc` override warnings
are unchanged.

The three-peer executable retains its admission/loss/offline/restart scenarios
and adds the following indexed cases:

| Case | Required outcome |
| --- | --- |
| Gap, duplicate and sparse restart | Later index stays received but unconfirmed until the gap is filled; saved receipt survives destruction/restore |
| Lost cumulative ACK | Retry reproduces frozen bytes and elicits the saved higher prefix; one ACK completes multiple pending packets |
| Independent recipients | H's confirmation for A does not complete A→B pending |
| Sparse snapshot coverage | B imports A:3 without A:2, retains prefix 1, and receives only missing A:2 directly |
| Invalid prefix | Wrong author, index, boundary time, trigger stamp and legacy fallback cannot cancel indexed pending |
| Chronological generation/restart | Explicit equal/backwards times are rejected without Save; automatic time and indexes advance beyond a future snapshot and survive restart |
| Inbound stamp conflicts | Reused index, remapped Event and timestamp violations against both neighboring indexes get no ACK, Save, import or journal mutation |
| Malformed snapshot ledger | Duplicate Event/index and wrong journal timestamp are rejected before attachment; the original snapshot still imports successfully |

Cold ACK checks inspect the saved Application edge and indexed receipt prefix.
The fixture verifies actual canonical object destruction and forbids teardown
Save. Exact packet counts retain the covered-history and no-host-relay oracles.

Targeted regressions:

```sh
ctest --test-dir build/shared-node-debug --output-on-failure -R '^apptraverse_(event_sourced_core|object_clone|historical_base_materialization|model_persistence|single_file_user_storage|object_scope_storage|application_contract_headless|window_catalog_headless|minimal_app_headless)_test$'
```

Regression result: **9/9 PASS** (18.28 s); all nine targets also built
incrementally (`ninja: no work to do`). Final `git diff --check`: **PASS**.

A controlled mutation completed pending through the triggering Event's index
even when its continuous ACK prefix was lower. After building that mutation,
the three-peer test failed its replica journal-count assertion: premature
completion lost an undelivered Event. The mutation was reverted, rebuilt and
all seven final profile entries passed.

The malformed-ACK fixture initially attempted to decode an intentionally
invalid packet before delivering it. It now injects that packet by transport
ID, so the runtime rejection is what the test exercises. Compatibility cases
execute native 1.0 and 1.1 wire data/ACK with unstamped local pending records;
they do not run a separate old-build producer or an old on-disk profile.

## Limits

This slice prepares delivery bookkeeping for a later checkpoint policy. Journal,
origin frames and exact delivery identities are still retained; compaction is
blocked. No silent-participant progress marker or safe checkpoint boundary is
implemented. Chronological generation per author does not by itself supply that
marker.

Three independent replicas on one computer and memory loss/reorder/availability
are the runtime scope. UID ownership remains stable across restart. Removal,
rejoin, room merge, admission failover, real Aether transport, GUI, full client
schema negotiation and cross-machine clocks are separate work. Persistence
success proves destruction/restore of the saved graph, not arbitrary torn
filesystem writes or OS process failure.
