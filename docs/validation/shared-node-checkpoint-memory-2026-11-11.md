# SharedNode: progress promises and confirmed checkpoints — 2026-11-11

Status: **PASS**, Windows MSVC x64 Debug; memory transport only.
Branch: `feature/shared-node-native-memory`; starting commit: `18c0881`.
Extends [indexed memory delivery](shared-node-indexed-memory-2026-11-11.md).

## Contract

`PublishProgress(node_id, through_us)` persists a native progress report and
its frozen outgoing obligations. The report binds its author, a promise never
to generate another Event at/below the promised time, exact sorted membership
and durable receipt cursors for all participants. Publication is explicit and
does not create a business Event or heartbeat. New authoring must exceed the
saved promise even after restart.

A receiver requires every claimed receipt prefix and timestamp boundary,
including the report author's own prefix, before saving the report and sending
a terminal ACK. Overtaking reports wait on retry.
Stronger reports replace weaker ones; dominated repeats cannot downgrade saved
progress. Incomparable promise/receipt coordinates are rejected.

`CompactConfirmed(node_id)` requires a report from each current member, exact
membership matches, local receipt through every author's declared own prefix,
and every member's report confirming each author's prefix through the minimum
promise. Missing/offline members stop progress. Membership changes require
fresh reports; there is no implicit eviction.

The ordinary native `NodeFor` path folds the selected journal prefix into the
base while preserving materialized application state and replica-local data.
Only actually collapsed Events retire their frozen authored payloads. Closed
time and local cleanup persist together. Existing compaction/schema guards
remain authoritative.

Stamp/ID metadata is retained for exact stamp identity checks, ACK validation,
author counters and later admission coverage. Retired exact duplicates are
re-ACKed without reapplying or importing their dependency graph. Unknown old
Events cannot reopen the closed prefix.
Retired payload bytes are no longer compared; the immutable stamp identifies
that already folded Event.

Wire 1.3 adds progress/checkpoint fields through native reflected serialization.
Exact previous tuple layouts remain supported without silent field dropping.
Initial state transfers the materialized base, remaining journal, full stamp
ledger and closed checkpoint. Coverage includes retired IDs so an existing
uncompacted peer does not replay old payloads to the joining participant.
The new author's timestamp floor includes the adopted checkpoint.

## Execution evidence

Same incremental `build/shared-node-debug`, Windows MSVC x64 Debug, no C++ RTTI
or exceptions. Dependency pins and main's 27-patch chain are unchanged; no
fresh dependency population or Release build.

```sh
cmake --build build/shared-node-debug --target apptraverse_shared_node_headless_check
```

Profile result: **7/7 PASS** (11.60 s), including the three-peer executable
(10.89 s) with **22 scenario groups**. Existing MSVC `/EHsc` override warnings
are unchanged.

Required memory checks cover:

| Case | Required outcome |
| --- | --- |
| Progress before Events | No success ACK or checkpoint before the author's declared prefix arrives |
| Loss, duplicate and restart | Saved promises and frozen report obligations retry without downgrade |
| Silent/offline participant | No unproven advancement; explicit silent report can permit progress |
| Confirmed compaction | Model state and local identity survive while journal/origin payloads shrink |
| Partial prefix | Later Events remain available for retry and timestamp-ordered replay |
| Retired duplicate | No reinsertion or dependency import, including after restart |
| Late old/conflicting data | Closed history and immutable stamp identity cannot be changed |
| Admission after checkpoint | Base and tail import correctly; already covered history is not resent |
| Changed membership | Older reports cannot authorize a checkpoint for the new roster |
| Delayed coverage receipt | Join can be compacted and A restarted before the original receipt retry; exact retained identity still allows durable confirmation and terminal ACK |

Cold readers load through the saved Application edge and verify progress
promises, every claimed continuous receipt boundary, checkpoint, application
state and the snapshot's complete stamp ledger. The admission-after-checkpoint
case checks that the network graph contains neither old H10 nor A20 Event
objects; retired IDs are coverage metadata. Native 1.0/1.1 data retain individual
ACKs, and a native 1.2 indexed Event gets its matching cumulative 1.2 ACK.
This run does not execute an old-build producer or an old on-disk profile.

The first restart run caught a fixture-owned rejected Event keeping its Room
reference alive. Resetting that test reference before restart restored the
strict object-destruction check; no production lifetime workaround was added.

Targeted regressions:

```sh
ctest --test-dir build/shared-node-debug --output-on-failure -R '^apptraverse_(event_sourced_core|object_clone|historical_base_materialization|model_persistence|single_file_user_storage|object_scope_storage|application_contract_headless|window_catalog_headless|minimal_app_headless)_test$'
```

Regression result: **9/9 PASS** (29.25 s), with all nine targets built
incrementally (`ninja: no work to do`). Final `git diff --check`: **PASS**.

A controlled mutation removed the requirement that every report acknowledge
each required author prefix. After building it, the three-peer test failed at
`!before_data`: compaction advanced while B still lacked an Event. The mutation
was reverted and rebuilt. The complete final profile, including the delayed
coverage-receipt regression, passed.

## Limits

This is logical journal and frozen-origin-payload compaction. Identity metadata
remains proportional to history. Native storage can retain unreachable old
object layers; no global object-store GC or physical file-size reduction is
claimed. Legacy unstamped history cannot be assigned guessed author evidence.

Three independent replicas on one computer and memory loss/reorder/availability
remain the runtime scope. Removal, rejoin, multiple admission coordinators,
room merge, admission failover, real Aether transport, GUI, full mixed-build
negotiation and cross-machine clocks are separate work. Save success is the
durability boundary; this stand proves graph destruction/restore rather than
arbitrary torn filesystem writes.
