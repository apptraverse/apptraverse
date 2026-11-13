# Cross-platform USER state — plan and progress

Branch: `feature/cross-platform-user-state`, based on `main` at `8b592f9`.
Scope: WindowCatalog on Windows, Linux, Android and Web. Preserve Apple source
compatibility; Apple execution requires its own host. No SharedNode work.

Result: implementation and acceptance complete. See the
[final validation record](validation/window-catalog-portable-user-2026-11-11.md)
for exact scope, commands, artifacts, preservation checks and limitations.

## Unified-order follow-up (2026-11-11)

WindowCatalog now shares one order and active-window mechanism across hosts.
Prototype S0 changed; the new matching supplies/USER passed the same 52-case
matrix. See [unified-order validation](validation/window-catalog-unified-window-order-2026-11-11.md)
for runtime coverage and preserved older profiles. Earlier mode-specific
implementation notes below describe the preceding revision.

## Acceptance

The same `user.bin` must load against each platform's independently built,
matching-revision embedded supply. Preserve object identity, rows, open/closed
windows, language, selection and desktop restore geometry. Reopen lazy windows,
edit the graph, save and transfer it again. Native handles are never persisted.
Normal Debug remains fresh; explicit load-only consumers exercise transfers.
Existing experimental profiles are preserved, not migrated or silently reset.

## Findings before implementation

- Native presenter layers have platform-specific class identities. Unknown
  stored classes correctly reject the graph. This check must remain strict.
- Android's button presenter duplicates inherited serialized fields, unlike
  the other native implementations. Equal class names alone are insufficient.
- Initial localization, chrome presenter and historical-base IDs vary per
  distillation. USER is an overlay, so omitted BACKING objects must retain IDs.
- Desktop and paged prefabs differ in their Close subgraph. The supplied graph
  must be complete on every consumer, without a runtime repair path.
- The saved pager flag describes the previous environment. The current host
  must report its presentation mode through the model before UI publication.

## Plan

1. Establish baseline transfer failures with real platform registries.
2. Unify persistent presenter identities and payloads; retain native C++
   implementations and strict object loading.
3. Produce a deterministic common initial graph, including lazy/history IDs,
   with domain-scoped allocation confined to distillation.
4. Bind presentation mode through a model event; keep desktop restore geometry
   separate from current mobile/browser viewport dimensions.
5. Run the four-source/four-consumer transfer matrix and a mutation round trip.
6. Build incrementally and exercise available native/browser/emulator consumers
   using isolated profiles. Run affected existing regression coverage.
7. Document the resulting window hierarchy, exact evidence and limitations;
   commit and push this branch. Do not merge back to main in this task.

## Progress

- Branch created; tracked source clean at start.
- Baseline source audit completed; implementation and transfer fixture underway.
- Independent Windows, Linux and Android supplies now have identical layer
  payloads, constants and scope metadata (header guards excluded):
  `b4dd34544028fa73e3c0f67ad2ff27de7b627d9fb9ba055f29c0041a29f2dfea`.
- Windows / actual NDK Android / Wasm interchange: 30 processes PASS, including
  nine ordered transfers, mutations and return transfers. Linux is joining the
  final matrix after its incremental build completes.
- Actual browser imported unchanged Windows USER, preserved Russian labels,
  object identities and closed rows, reopened the middle child, saved/restarted.
  Windows then verified the browser output against the expected deliberate edit.
- Actual Windows GUI imported both NDK Android and Wasm USER, reopened a closed
  child and cold-restarted successfully.
- Web functional and full lifecycle regressions PASS; Web embedded session PASS.
- Windows native acceptance PASS. Initial 24-test regression: 22 PASS / 2 FAIL.
  Allocator test used exceptions despite the project's no-exceptions policy;
  replaced with an early-return RAII test and focused PASS. The remaining Close
  lifetime failure is being investigated in the object pointer graph algorithm.
- Android historical batch: 6/7 PASS; final swipe after Activity remount failed
  again in a focused repeat. Superseded by the focused diagnosis and final clean
  7/7 PASS recorded below.
- Four-platform matrix subsequently passed all 52 processes: four seeds plus
  16 ordered transfers, each with mutation/save and verification on return.
  This includes actual Android NDK x86-64 and Wasm32, not only host registries.
- The Close lifetime defect was reproduced independently of AppTraverse and
  fixed in a narrow object-system patch. See
  [the releasing-edge regression](validation/aether-ptr-releasing-edge-2026-11-11.md).
  The previously failing Windows model suite then passed, including 100 holders.
- Review found another cross-mode ownership issue: a previously active mobile
  page and its selection history retained an owning window link after desktop
  Close. Active selection/history now keep lazy identity links. Closing or
  removing a window updates both presentation modes' references regardless of
  the current host. New headless tests cover desktop Close/Delete followed by
  a cold paged restart.
- Native GTK acceptance exposed a queued frame event after the child had been
  freed. Frame, screen and viewport commands now address the catalog and validate
  the current window's membership, open state and loaded state. Tests additionally
  cover an externally retained or deliberately materialized closed graph.
- A standalone actual-presenter probe reproduced two queued Close clicks: after
  the first click the button no longer existed, so the second strict proxy call
  asserted. Runtime click dispatch now lets the Close action target the catalog;
  ordinary buttons retain strict self dispatch. The identical probe passes after
  the fix. No new persisted fields/classes or generic missing-target fallback.
- Android's Activity-remount swipe failed because injected DOWN waited for a
  window that was visible to accessibility but not focused for input; heavy
  parallel builds could further delay delivery into long-press classification.
  The test now waits for InputDispatcher's actual focused window. An idle trace
  delivered DOWN in 11 ms and passed the unchanged physical gesture in 8.627 s.
  Production gestures were unchanged and all temporary logs were removed.
- Final frozen-source Web embedded, functional, headed lifecycle and imported
  Windows USER tests PASS. Lifecycle covers tab visibility, navigation, same-turn
  shutdown and a full Chrome process restart (13.907 s).
- Final Android instrumentation: 7/7 PASS, 60.226 s. Fresh Windows USER imported
  in the actual Android UI, reopened/closed a child, added Window 4, saved and
  cold-restarted. Windows verified the returned file's preserved original IDs,
  localization and negative restore geometry plus the expected edits. Original
  APK restored without launching the default profile; original USER unchanged.
- Final Linux: 16/16 affected headless/GTK tests PASS, 57.27 s. Actual Windows
  USER import, GTK presentation, save and cold GTK restart PASS.
- Final frozen-source four-platform matrix: 52/52 PASS. All 32 read-only
  verifications preserve file hashes; Android's 8 checks read actual remote bytes.
- Final Windows: 40/40 affected Debug tests PASS (39-test batch 282.33 s, native
  acceptance 13.12 s), including all 13 additional ownership/history/serialization
  suites and the new queued-input regressions. The native harness now verifies
  parent activation before a synthetic click; final execution established the
  foreground prerequisite with a PID-checked click on its own titlebar. No
  child-focus assertions or production focus behavior were weakened.
- Final actual Android and Wasm USER imports into Windows GUI both PASS, including
  reopening a cold child, saving and restarting. Real browser/Android UI return
  files were checked with the native Windows registry.
- All seven implementation/acceptance steps are complete. The feature branch
  contains the tested code, regression harnesses and this evidence; `main` and
  the separate Æther repository are not published by this task.

## Window and presenter structure

The logical window remains `UiWindow -> UiPresentedWindow -> UiDesktopWindow
-> CatalogWindow`. `UiDesktopWindow` is the optional restoration capability of
the same logical window: its coordinates and restore dimensions remain stored
while a mobile/browser host displays the window as a page. Current viewport
measurements do not overwrite those desktop restore dimensions.

Platform presenters remain separate C++ implementations. Their persisted
native role layers use the same names and payloads: `NativeWindowPresenter`,
`NativeListContainerPresenter`, `NativePushButtonPresenter`. The executable
registers its own implementation for those roles. Both main and child catalog
windows use the same application presenter schema and catalog link.

Different persisted mobile/web window subclasses are not required by the
current behavior. The presentation environment is reported as a model event
before publication; changing it does not reconstruct the graph or discard the
other platform's user state. Any future subclass must represent actual distinct
model semantics, not merely choose a native widget toolkit.
