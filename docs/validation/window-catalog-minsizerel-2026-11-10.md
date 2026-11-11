# WindowCatalog MinSizeRel validation — 2026-11-10

Tested source: `0417ea63d13fc83ea5db9aa5efdfea5c56fb79ce` on
`feature/domain-hierarchy-cloning`. No application/library/test source changes
were needed. This follow-up closes the MinSizeRel verification gap recorded in
[the five canon fixes](window-catalog-canon-five-fixes-2026-11-10.md).

## Build and provenance

Incremental existing `build/win64-ninja-msvc-minsizerel`: PASS. No clean,
cache deletion, dependency download or other-platform build. The existing
verified Æther source override and patch chain were unchanged. `AE_NO_DEBUG_LOG=ON`,
`/DNDEBUG`, static runtime and the existing MinSizeRel optimization policy.

MinSizeRel embedded the supply prepared by the current Debug application.
The Debug and MinSizeRel generated supply headers have identical SHA-256:
`CD23E3F989CF20769C49B54ECCECAEDF0A0F627DE514537BE16E175551200704`.

The ordinary and explicit load-only executables were both freshly linked.
The build emitted the existing exception-option override warnings and an
object-path-length warning for an unrelated, unselected test target; neither
prevented the selected build. No CI or legacy-example repair was made.

## Executed tests

**24/24 PASS**, **58.02 seconds**, serial CTest with configuration MinSizeRel.
Checks in the relevant suites use CHECK and remain active under NDEBUG.

- Normal and load-only binary contracts (2).
- Desktop geometry and control materialization (2).
- Native frame sync, interactive drag and model/UI publication (3).
- Clone, domain hierarchy, history-base materialization and object scopes (4).
- Single-file USER and Catalog persistence boundary (2).
- Catalog headless/model, embedded overlay and close-stage controls (4).
- Application model runtime/UI session (2).
- Catalog native acceptance, chrome adaptation, main-frame coalescing,
  native command routing and Close timing (5).

The headless/coalescing suites include the newly added holder replay, valid
lazy-list and mixed restore/live geometry regressions. The native acceptance
suite exercises the ordinary MinSizeRel EXE on fresh USER and after shutdown,
with an intermediate explicit load-only restore step. It checks window
creation/close/open, identity, localization, geometry, selection and deletion.
This is execution of actual Windows processes, not only a graph round-trip.

Logs: `.artifacts/canon-minsizerel-build.log`,
`.artifacts/canon-minsizerel-tests.log`,
`.artifacts/canon-minsizerel-tests.xml`,
`.artifacts/canon-minsizerel-detail.log` (full test output; JUnit truncates
individual successful-test output).

## Binary composition

Both normal and load-only EXEs are **645,120 bytes (630 KiB)** and import only
**KERNEL32.dll and USER32.dll**. The PE import/map contract passed: no dynamic
CRT/STL/AppTraverse/Æther runtime; no listed distillation/builders, directory
storage, superseded registration objects or named unused concrete factories.
This verifies the explicit exclusions, not globally minimal machine code.

The previous local normal EXE was 647,168 bytes; the fresh artifact is 2,048
bytes smaller. This comparison spans the source changes since that older
artifact and is not an attribution to one particular fix.

## Close observations

The timing harness explicitly launches `win32_window_catalog_demo_load_only.exe`.
The following 5×4 observations must not be described as a timing measurement
of the normal executable, whose functional restart was checked separately.

| Scenario | Child HWND destruction median | Main process exit median |
| --- | ---: | ---: |
| Cold Add/Close | 21 ms | 31 ms |
| Resize burst, immediate Close | 45 ms | 31 ms |
| Resize burst, stable native size | 15 ms | 28 ms |
| Saved profile | 18 ms | 30 ms |

All 20 runs completed successfully with process exit code 0. The Main
measurement follows the child-close observation; it is not a many-open-window
shutdown benchmark. Stable native size does not prove the model queue drained.

The RAM/disk stage diagnostic separately reported zero disk bytes for its RAM
controls. Its explicit directory-storage control is not the application's
single-file USER path.

## Scope

This verifies the current Windows Catalog/headless changes in MinSizeRel, in
addition to the preceding 22/22 Debug PASS. No merge to main, standalone Æther
publication, fresh upstream patch-chain run, complete repository CI, other
example or other-platform acceptance was performed. This commit records
validation only; tested source and binaries were not modified afterward.
