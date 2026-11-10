# Window Catalog Debug Close: persistence profiling and optimization

Date: 2026-11-10. Worktree: `apptraverse-wt-domain-hierarchy`.

This is a scoped comparison of two Close/storage changes, while the Debug
bootstrap work was also in the working tree. It is not a performance claim for
future object-scope metadata or a differently generated supply.

## Controlled comparison

Both runs used `win32_window_catalog_demo_load_only.exe`, Windows x64 Debug,
MSVC 14.50, `AE_NO_DEBUG_LOG=ON`, assertions/runtime checks enabled, and five
observations per scenario. The normal Debug application now bootstraps a fresh
graph, so it is not a valid consumer for the saved-profile scenario. The timing
test explicitly selects the load-only executable.

The embedded header was held byte-identical across the pair:
`4C8279CB8B6B9D06D120229C6C6420E69765CD4BA06C85CDB2E5066012A7BD8D` (SHA-256).
Before means the original separate Save operations and recursive directory
preparation; after means the changes described below. Every scenario used its
own USER directory; saved scenarios used copies of one runtime-prepared seed.

| Scenario | Before HWND destroy, ms | After HWND destroy, ms | Reduction | Before / after list, ms |
| --- | ---: | ---: | ---: | ---: |
| COLD_ADD_CLOSE | 696 | 416 | 40.2% | 677 / 394 |
| BURST_CLOSE_IMMEDIATE | 859 | 537 | 37.5% | 848 / 525 |
| BURST_CLOSE_AFTER_SETTLE | 1082 | 566 | 47.7% | 1065 / 553 |
| SAVED_PROFILE_CLOSE | 231 | 165 | 28.6% | 214 / 152 |

All 20 observations passed in each run. No timeout or process-exit failure was
observed. This measures actual HWND destruction, not early hiding.
`AFTER_SETTLE` means stable HWND geometry; it does not prove queue drain.

## Localized cost

Temporary timers were removed after diagnosis. They showed that Close Commit
and reference releases are small compared with persistence. A representative
model-only cold Close was 582 ms: presenter Save 468 ms, window Save 101 ms,
holder Save 12 ms, and both pointer resets about 1 ms combined. Burst was
924 ms, including presenter Save 758 ms and window Save 154 ms.

The same model-only operation with RAM as the diagnostic USER backend took
8 ms cold and 12 ms burst, versus 687/1105 ms with directory USER in that
profiling run. This diagnostic control does not change production storage.

File-write timers isolated recursive directory preparation: in one cold run,
315 ms was spent in `create_directories`, versus 48 ms opening temporary files,
21 ms writing/closing, and 114 ms in atomic rename. Burst directory preparation
was 543 ms. Native publication/apply added roughly 22-28 / 52-57 ms in selected
Win32 traces, rather than explaining the main model delay.

## Changes

1. Directory storage first attempts `create_directory` for the final layer
   directory. Missing ancestors retain the existing recursive creation fallback.
   The layer layout, bytes, error handling and temporary-file/rename commit
   mechanism are unchanged. There is no directory cache and no USER RAM store.
2. Unload retains the presenter while resetting the window's presenter link,
   then saves window and presenter roots with one `DomainGraph`. Shared layers
   are visited once and the persisted window link remains lazy. Different-domain
   presenters keep the previous separate-save behavior and original storage
   owner. All temporary references are released before return.
3. The disk-stage diagnostic now reports actual directory commit statistics
   rather than the misleading zero disk-byte count from an instrumented overlay,
   and includes an otherwise identical RAM control.

With the fixed-supply pair, model-only cold/burst times fell from 624/1003 ms
to 344/535 ms. Store calls fell from 452/638 to 235/328. Physical layer commits
fell only from 174/267 to 173/266; the main remaining cost is creation and atomic
replacement of many small files. This change does not make Close instantaneous.

## Verification

- Directory storage round-trip, unchanged/empty layers and write-failure cases: PASS.
- WindowCatalog USER persistence boundary, including cold presenter-only state: PASS.
- MinimalApp unload-only disk round-trip: PASS.
- Presenter-only disk round-trip and ancestor-presenter storage ownership: PASS.
- Fixed-supply Win32 Close diagnostic: 20/20 PASS before and after.

The ancestor test verifies that a presenter and its private string stored in
an ancestor are saved there, not copied into the child USER store, and that the
private string reloads cold. Other test matrices and any subsequent scope
metadata integration require the parent task's final regression run.

## Local evidence

- `.artifacts/close-ab-before.log`, `.artifacts/close-ab-after.log`
- `.artifacts/close-ab-stage-before.log`, `.artifacts/close-ab-stage-after.log`
- `.artifacts/close-ab-before-supply.json`, `.artifacts/close-ab-after-supply.txt`
- `.artifacts/close-ab-focused.log`
- `.artifacts/close-stage-profile.log`, `.artifacts/close-stage-ram.log`
- `.artifacts/close-stage-io.log`, `.artifacts/close-stage-dir-fast.log`

An earlier attempted A/B pair was rejected because copied source timestamps did
not trigger recompilation. Another after-run was rejected because the supply
regenerated. The final pair above verifies both expected Store counts and the
identical header hash. Earlier `close-timing-before/after.log` exploratory runs
are not the controlled comparison.
