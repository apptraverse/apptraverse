# Node history template sharing — 2026-11-10

Status: implemented and validated. Initially kept uncommitted as requested;
the user subsequently authorized committing and publishing this work.
Base: `39a06607ef9cc0dd988d4a1e91d191f1dbba463b` on
`feature/domain-hierarchy-cloning`, in `apptraverse-wt-domain-hierarchy`.

## Implementation and preserved contracts

- Moved the large bodies of historical leaf remapping, base capture and replay
  preparation from templates into `src/node_history.cpp`. Shared code takes
  `Node&`; typed wrappers remain at the existing call sites.
- A small serializer descriptor carries the **static** `ConcreteNode::kClassId`
  and a typed `DomainGraph::Save<ConcreteNode>` adapter. Using a dynamic class ID
  or saving an `Obj&` would change layer selection when `NodeFor<T>` describes
  a base of the most-derived object; neither substitution was made.
- `ConcreteNode::RebuildOperationScratch` remains typed and stack-owned. Existing
  virtual stash/restore hooks preserve localization's live/lazy language state.
- Existing graph ownership, historical snapshot scope, leaf isolation, capture,
  journal replay, error paths and serialized fields are retained. The descriptor
  introduces no heap allocation or new owning object reference. This is not a
  conversion of persisted typed model pointers to untyped pointers.
- Added the new source to the common static library. No dependency patches,
  storage format changes or platform-specific code were introduced.

## MinSizeRel measurements

Both applications use the existing static MSVC runtime, `/O1`, `/GL` and LTCG.
Baseline EXEs, MAP files and Debug-produced supply headers were copied before
building. The first comparison reused those exact supply headers, isolating
the code change from regenerated object identities. After the incremental Debug
build regenerated supply, both applications and dependent tests were rebuilt;
the final EXE sizes remained identical to the controlled comparison.

| Application | Before, bytes | After, bytes | Saving | Saving, % |
|---|---:|---:|---:|---:|
| WindowCatalog | 681,984 (666 KiB) | 647,168 (632 KiB) | 34,816 (34 KiB) | 5.11% |
| MinimalApp | 622,592 (608 KiB) | 569,856 (556.5 KiB) | 52,736 (51.5 KiB) | 8.47% |

Catalog `.text` file bytes fall from 534,016 to 499,200; its other section sizes
are unchanged. MinimalApp `.text` falls from 483,328 to 431,104, and `.rdata`
falls by another 512 bytes. Both final EXEs import only KERNEL32/USER32 and pass
the existing binary contracts for static runtime, forbidden distillation
objects, broad registration modules and unused concrete factories. These gates
check their named exclusions; they do not prove globally minimal code.

The MAP files confirm shared implementations rather than only a change in
embedded data. Before, each of the three large bodies had nine code addresses
in Catalog and thirteen in MinimalApp. Afterwards, remap and capture each have
one; replay has one shared body plus the small localization scratch wrapper.

| Approximate MAP span | Catalog before → after | MinimalApp before → after |
|---|---:|---:|
| Remap | 15,084 → 1,680 B | 21,788 → 1,680 B |
| Capture | 13,032 → 1,492 B | 18,824 → 1,492 B |
| Replay, including retained typed wrapper | 15,208 → 1,872 B | 21,928 → 1,872 B |
| New typed descriptor/getter adapters | 0 → 504 B | 0 → 728 B |

MAP spans use unique symbol addresses and the interval to the next symbol;
padding, folding and linker attribution make them estimates, not exact
standalone function sizes. Whole-file byte counts above are exact.

## Validation and test synchronization correction

All builds were incremental Windows builds. `AE_NO_DEBUG_LOG=ON`; Debug asserts
remain enabled. Existing test coverage exercises historical leaves, clones,
localization, application contracts, USER boundaries and native save/restart.

| Check | Result |
|---|---|
| Focused Debug history/clone/localization/application contract | 4/4 PASS, 3.32 s |
| Full Debug CTest after production changes | 82/82 PASS, 174.49 s |
| Debug Catalog acceptance after test synchronization correction | 1/1 PASS, 9.52 s |
| Final targeted MinSizeRel suite, including four binary contracts | 17/17 PASS, 34.82 s |
| Additional consecutive MinSizeRel Catalog acceptance runs | 3/3 PASS, 8.17 s total |

The initial MinSizeRel suite and a later complete attempt failed while waiting
for a minimized child. The test sent a second asynchronous Open request and
accepted an already-true foreground condition as its completion. It could then
minimize the window before Open's pending focus publication restored it. The
existing app callback explicitly restores an iconic window before giving it
focus. One unchanged-binary retry passed, demonstrating that retry alone was
not adequate acceptance.

The test now first brings the main window to the foreground, then sends Open
and observes the child regaining focus. This fixes both equivalent second-Open
checks without sleeps, reduced assertions or a production UI change. The
successful full Debug run preceded this test-only fix; its affected test was
rebuilt and passed afterwards. No production code changed after the full run.

An intermediate acceptance run also mixed a newly embedded application with an
older test executable containing the previous supply header and failed with
`LoadApplication failed id=1`. Rebuilding all dependent MinSizeRel tests restored
matching supply. The final results above use the consistent build; neither that
intermediate run nor the earlier focus failures are counted as PASS.

## Evidence and limitations

Local artifacts are under `.artifacts/template-sharing/`:

- Frozen controls: `before/`, `after-fixed-supply/`, `size-comparison.json`.
- Binary inspection: `catalog-before.json`, `minimal-before.json`,
  `catalog-fixed-supply.json`, `minimal-fixed-supply.json`,
  `catalog-final.json`, `minimal-final.json`.
- Debug: `debug-focused-build.log`, `debug-focused-test.log`,
  `debug-full-build.log`, `debug-ctest.log`, `debug-acceptance-build.log`,
  `debug-acceptance-final.log`.
- MinSizeRel: `min-build-fixed-supply.log`, `min-build-fresh-supply.log`,
  `min-build-final.log`, `min-acceptance-build.log`,
  `min-ctest-accepted.log`, `min-acceptance-repeated.log`.
- Preliminary failures remain in `min-ctest.log`, `min-ctest-final.log` and
  `min-final-acceptance.log`; the unchanged-binary retry is
  `min-acceptance-recheck.log`.

No controlled latency benchmark was performed, so no Close or replay speedup
is claimed. The full Debug suite includes the existing Close diagnostic.
No other platform/device build, clean rebuild or real user profile was used.
Unrelated pre-existing untracked caches remain untouched. Publication includes
only the reviewed source, test and documentation changes; build artifacts and
local caches are excluded.
