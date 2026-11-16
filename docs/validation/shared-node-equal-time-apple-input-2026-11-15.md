# SharedNode equal timestamps and Apple chat input — 2026-11-15

Audited against AppTraverse main `7fed1d64`. Both findings were confirmed.

## Changes

- Journal order remains ascending by `timestamp_us`. Equal timestamps now use
  the serialized Event ID in ascending order on every replica. The rule is
  explicit in `EventRecordOrderLess` and the SharedNode contract. Per-author
  indexes remain delivery cursors; no clock synchronization or wire field was
  added. This closes the previously documented equal-time convergence gap for
  replicas containing the same distinct Events.
- Apple `ChatBridge` exposes the canonical edit revision. `ChatPresenter`
  retains a newer local draft or invite until the mirror acknowledges its edit,
  and does not reassign unchanged text, preserving the native field and caret.

## Executed checks

- Windows Debug `apptraverse_shared_node_headless_check`: 7/7 PASS. This
  includes existing memory-link, SharedNode, SharedSync and three-peer tests
  that exercise delivery loss, reordering, restart and compaction.
- `apptraverse_event_sourced_core_test`: PASS. The added test inserts two
  noncommutative Events at the same timestamp in both arrival orders, checks
  identical replay and journal order, then requires both journals to compact
  completely while preserving the same result.
- `git diff --check`: PASS.

Apple build and runtime: NOT RUN; this worker is Windows and has no Apple host.
The Swift/Objective-C++ path was source reviewed. Cross-machine wall-clock
ordering and convergence of independently conflicting application state remain
outside this rule's scope. No MinSizeRel, size, online benchmark or prior night
program was run.
