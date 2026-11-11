# WindowCatalog: five canon fixes — 2026-11-10

Base: `a2a3352962b029c37608c43200ae2b431f136381`.
Branch: `feature/domain-hierarchy-cloning`.
Scope: Windows WindowCatalog, its headless tests and the shared Windows
presenter code it uses. CI, other examples and other-platform acceptance are
excluded. Existing untracked caches and the adaptive worktree were preserved.

## Reproduced baseline failures

A separate diagnostic linked against the existing Debug libraries reproduced
three failures before these changes (`.artifacts/canon-five-before.log`):

- A restore report followed by a live-only report for the same frame retained
  restore geometry `0,0,640,480` instead of `-231,-143,701,503`. The live client
  size changed, so checking client size alone missed the loss. The control with
  only a restore report passed.
- After Add, a holder had a valid `primary_text`, but its base had ID 0.
  `TryReplayFromBase()` returned true and erased the live required title link.
- A loaded list produced a two-window mobile ring; resetting that still-valid
  reference produced a one-window ring, despite another owner retaining it.

The earlier 18/18 PASS and source review did not cover these cases. This report
supersedes the earlier conclusion that no further concrete violations remained.

## Changes and regression coverage

1. **Preserve restore geometry independently of live geometry.**
   `WindowCatalogMainNativeFrameQueue` retains the latest live frame and the
   latest pending restore frame separately. Drain snapshots both under the
   mutex, consumes the restore report once, and applies ordinary model Events.
   A later Close/maximized live report cannot erase or replace the restore
   coordinates. Reports arriving during Apply are drained by the same accepted
   model task, including during shutdown. No file writes or mirror mutations
   were added.

   `window_catalog_main_native_frame_coalesce_test` checks true-only,
   true→false with identical coordinates, distinct restore/live coordinates,
   multiple restore reports, reports posted during Apply, and a subsequent
   live-only batch. The shutdown test now uses actual RAM USER → `user.bin` →
   a fresh Domain, verifying signed restore coordinates and a different live
   client size/viewport. The during-Apply test uses deterministic notifier
   interleaving; it is not a general thread-stress claim.

2. **Capture a complete initial holder base.**
   `AddWindow` fills required/optional holder fields and window links before
   `InitializeRuntimeNode`. `TestAddedHolderReplayPreservesInitialLinks`
   performs real replay with an empty journal and verifies title identity/text,
   row text, window/presenter identity and window number. This fixes history
   semantics even though ordinary Catalog UI does not currently replay holders.

3. **Load a valid lazy list; retain real closed/stale conditions.**
   `BuildMobileRing` loads the mandatory list before traversing it. Redundant
   mandatory-link checks were removed from ring/predecessor/swipe/z-order;
   closed-window and stale-target filtering remains.
   `TestMobileRingLoadsLazyListAndSkipsClosedWindows` checks list Reset→Load,
   unchanged order, predecessor/swipe behavior and exclusion of closed windows
   without loading them. This mobile-model path belongs to the existing
   headless suite; no mobile platform build is claimed.

4. **Remove production presenter test hooks.**
   Removed `APPTRAVERSE_WIN_DESKTOP_FRAME_SYNC_TEST` from the library and all
   test definitions, and removed its production methods. Frame-sync tests now
   use test-local `SetWindowPos` and real `WM_ENTERSIZEMOVE`/`WM_EXITSIZEMOVE`
   messages. They verify 500 movement-only steps produce no intermediate
   reports and exactly one final report. The separate model/UI drag test also
   verifies queue counts after draining startup/initial-size work. Source and
   generated Debug build rules contain none of the removed hooks/define.

5. **Make the declared Visual Studio Debug preset Debug-only.**
   The preset sets both `CMAKE_CONFIGURATION_TYPES=Debug` and
   `CMAKE_BUILD_TYPE=Debug`, keeping its only build configuration consistent
   with the current application's configure-time startup/supply selection.
   `window_catalog_debug_preset_test.py` reads those actual preset settings and
   generates a small Ninja Multi-Config project including the real Windows
   application CMakeLists. It verifies normal Debug bootstrap/distillation and
   the separate explicit load-only consumer. Probe reuse clears the two
   selection cache entries before applying current preset settings, so old
   cache values cannot hide a regression. It reuses the existing compiler and
   Debug flags; it does not populate dependencies or build another app tree.

## Executed validation

- Incremental existing `build/win64-ninja-msvc-debug`: PASS for the demo,
  load-only demo and all selected test targets. `AE_NO_DEBUG_LOG=ON`, normal
  Debug assertions, existing `/Z7` compiler flags. No clean or cache deletion.
- Initial focused run: all five behavior tests PASS. The new configure probe
  failed because its default `/Zi` compiler check hit this machine's MSVC C1902
  PDB-manager mismatch. The probe was changed to inherit the already-working
  main build's Debug flags; no application/compiler checks were disabled.
- Final suite: **22/22 PASS**, **104.10 s**. This includes headless/model,
  native acceptance/chrome adaptation, geometry queue, frame sync/drag/
  publication, native command routing, USER boundary/single-file storage,
  Clone/domain hierarchy/history/scope, application model/UI sessions and
  the **5×4 Close diagnostic**.
- Logs: `.artifacts/canon-five-focused-build.log`,
  `.artifacts/canon-five-focused.log`, `.artifacts/canon-five-final-build.log`,
  `.artifacts/canon-five-final.log`, `.artifacts/canon-five-final.xml`.
- `git diff --check`: PASS. Independent source review of the queue/ownership
  and native-test conversion found no further concrete issue in these edits.

No production/test changes followed the final passing run; subsequent changes
only recorded this evidence. MinSizeRel, a full Visual Studio 2022 solution,
other apps/platforms and a new upstream Æther patch-chain run were NOT RUN.
The preset regression verifies multi-config target selection, not VS2022
installation/device acceptance. No new release size or performance-improvement
claim is made.
