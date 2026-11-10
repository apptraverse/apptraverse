# Immediate application event compaction вЂ” 2026-11-10

Base: `2583867e7d297f7e00b2db2015ebe3a8516f278c`.

## Contract and implementation

Both application runtimes enable immediate compaction on model nodes. Commit applies the event, folds its result into the historical base and releases the journal record before returning. Existing loaded journals are compacted when the runtime binds nodes; new/reopened nodes inherit the policy through notifier binding. Startup traversal holds strong references while compaction can release journal-only objects.

The policy is runtime-only: no stored schema changes. Standalone versioning tools retain explicit history support. Existing unknown-version and compaction-block guards are preserved: an unsafe-to-fold history is not silently discarded.

No USER RAM store or new disk write boundary is introduced. Historical capture uses existing scratch buffers; disk persistence remains at window unload/application shutdown. UI publication and generation tracking remain intact.

## Regression coverage

- Application session: immediate empty journal and current base, UI publication, persisted cold reload.
- New child inherits policy; twenty commits retain no journal and do not write storage.
- WindowCatalog: forty geometry commits, empty journal/current base after each, zero USER writes before Close, private presenter and geometry restored from cold disk after Close and shutdown.
- MinimalApp E2E now checks folded geometry instead of expecting a retained resize event.

## Validation

Results are recorded after execution below. Full compaction is performed per event; this change does not claim to make geometry event handling cheaper.

- Debug: all 83 non-timing entries passed across the full run (82 PASS) and the corrected MinimalApp test rerun (1 PASS). Only test expectations changed after the full run; production sources were unchanged. Close diagnostic additionally passed (20 observations, 5 per scenario).
- MinSizeRel: application runtime, persistence boundary and Win32 acceptance: 3/3 PASS. Normal executable Close probe: 20/20 observations PASS, exit codes zero, unchanged executable hash during measurement.
- Debug object logging disabled (`AE_NO_DEBUG_LOG=ON`), assertions enabled. No builds ran during timing.

| HWND destruction median, ms | Debug | MinSizeRel |
|---|---:|---:|
| Cold Add/Close | 521 | 395 |
| Main resize burst, immediate child Close | 991 | 709 |
| Main resize burst, stable geometry then child Close | 958 | 663 |
| Runtime-prepared saved profile | 543 | 369 |

Main exit after child Close: Debug 108–143 ms for fresh/burst, 485–533 ms saved; MinSizeRel 86–130 ms fresh/burst, 392–424 ms saved. These are not main-with-open-child measurements. Stable geometry is not a verified queue-drain barrier.

Earlier measurements before this change were Debug 400/505/550/171 and MinSizeRel 385/730/732/105 ms. These sequential runs are not a controlled performance isolation. There is a material observed Debug/saved-profile slowdown, not a demonstrated Close optimization. Per-event full historical capture is extra work; attribution of the slowdown needs stage profiling. Geometry handling latency itself was not measured.

Local logs: `.artifacts/immediate-compact-final-tests.log`, `immediate-compact-minimal-tests.log`, `immediate-compact-min-tests.log`, `immediate-compact-min-acceptance.log`, `immediate-compact-debug-timing.log`. MinSizeRel probe run: `standard-e07a38717bfa4806a435af37c7b26287`; executable SHA256 `AE54E320127F78BD08BEA0A4DF86740281ADBC089A6E5D9CF83B8651E4DE6A10`.
