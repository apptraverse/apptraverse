# WindowCatalog portable USER — 2026-11-11

Base: `main` at `8b592f994c2ad2931bcef22a044a6a21dbe05c4e`.
Work branch: `feature/cross-platform-user-state`.

## Contract

Transfer the identical `user.bin` between independently built Windows, Linux,
Android and Web consumers of this schema/supply revision. There is no export
conversion, platform-specific USER envelope or runtime graph repair. Native
resources are reconstructed by the destination's registered presenter factories.
Constants remain in BACKING, not copied into USER.

The normal Debug fresh policy is unchanged. All import tests use load-only
consumers and isolated profiles. Previous experimental profiles are preserved;
this work does not introduce schema migration or supply upgrades.

All changes remain in the AppTraverse feature branch. The upstream Æther pin is
unchanged; the scoped allocator and releasing-edge fix are reproducible patches
28/29 in this repository. No separate Æther repository was published.

## Implementation and failure boundaries

1. Native window, list and button implementations now have common persistent
   role identities and base-only layer schemas. Android's duplicate button
   fields and Android/Web main-only presenter layers were removed. Strict
   unknown-class rejection remains enabled.
2. A domain-owned, scoped ID sequence covers initial graph construction, child
   prefab domains and historical bases. Explicit IDs no longer accidentally
   consume generated IDs. The policy ends at the builder boundary; ordinary
   runtime creation/cloning retains its existing allocation policy.
3. All platform builders supply the same complete Close graph. Four independent
   generated supplies have identical layer bytes, constants and object scopes;
   header wrapper/guard differences are not persisted data.
4. A model event reports the current host's presentation mode before the first
   publication. Active-page selection and desktop restoration remain distinct
   state. Page-selection history holds lazy identity links; Close/Delete update
   both presentation modes' references, including when invoked on a desktop.
5. Late native frame, viewport, screen and Close inputs address the surviving
   catalog, then validate current membership, logical open state and loaded state
   before accessing the window. A closed window may still exist because another
   owner retains or explicitly materializes it; `Domain::Find` alone is therefore
   insufficient. A standalone two-click probe reproduced the second queued
   click targeting a freed button. `UiPushButton::QueueClickFromUi` is a runtime
   dispatch hook: its default retains strict self-target dispatch, while the
   catalog Close action targets the catalog and calls the live button with
   scoped ownership. No serialized class or field was added for this hook.
   Queued-input tests cover live, closed-but-retained, closed-but-materialized,
   freed and deleted windows, plus disabled buttons and duplicate Close.
   `ModelObjectProxy` keeps its strict target assertion. These are real lifecycle
   races, not permission to silently accept a malformed distilled graph.
6. A cyclic ownership bug exposed by the complete Close graph was reproduced
   with pure `ae::Ptr`, failing before and passing after the narrow releasing-edge
   patch. See [the isolated proof](aether-ptr-releasing-edge-2026-11-11.md).
7. The Windows release binary checker now explicitly requires the portable
   catalog's Close, active-page and presentation-mode factories. It still rejects
   distillation, dynamic runtime libraries and unrelated concrete factories;
   MinimalApp retains its narrower contract. A focused Python policy regression
   passes. This is not a new Release/MinSizeRel binary verification.

The original platform schema and ID mismatches were found by source inspection;
the original four-platform matrix was not executed before implementation began.
Do not reinterpret the later successful matrix as a measured baseline comparison.

The final independently generated headers each contain 247 object layers and
60 object scopes. Their normalized content SHA-256 is
`1a1ff30bffde0a2b008d17166fef7ed544e8b29f18f1e8ef1ca980fca9b4402e`.
Normalization reads UTF-8 with `splitlines()`, removes whitespace-only lines and
lines starting with `#ifndef`, `#define`, `#endif`, `namespace ` or
`}  // namespace`, preserves all remaining characters, then joins with LF and
one final LF. This compares generated data/metadata without platform header
guards or namespace wrappers. Earlier progress hashes used different whitespace
normalizations; they are not a change of the supplied state.

## Hierarchy decision

Keep `UiWindow -> UiPresentedWindow -> UiDesktopWindow -> CatalogWindow` as the
shared logical model. The desktop layer is a restoration capability retained
while Android/Web show that same window as a page. Current client viewport
measurements do not overwrite desktop restore position or dimensions.

Native presenters are different C++ descendants implementing the same persistent
roles. The registry of each executable selects its backend implementation. Main
and child windows have the same catalog presenter schema. Separate persisted
mobile/web model siblings would not represent a distinct current business
meaning and would prevent lossless return transfer if desktop fields were lost.

The older generic headless builder is not a native-profile importer. Interchange
fixtures execute the actual platform registries without opening native widgets;
the separate GUI tests cover real presentation and lifecycle.

## Verification protocol

`tests/window_catalog_portable_user_fixture.cpp` is compiled separately for all
four backends. Windows/Linux/Android execute native 64-bit code; the Web fixture
executes actual Wasm32 under Node. The matrix runner records ABI widths and every
process result in JSON.

Each source creates three rows, two open windows and one closed lazy window,
Russian localization, selection and negative desktop coordinates. Every consumer
checks object/presenter IDs, row order, active page, desktop order, localized
texts and restore geometry. It explicitly loads the closed window and presenter.
The mutation stage closes a previously active page, checks actual graph release,
reopens/changes/closes a lazy child, adds a new clone, changes language and saves.
The original source then cold-loads and verifies the returned file.

Four seeds plus 16 ordered pairs with verification, mutation/save and return
verification produce **52 process results**. The runner hashes input USER before
and after read-only verification to ensure it was transferred unchanged. For
Android the latter hash is read back from the emulator, rather than merely
rehashing the local file that was pushed.

Reusable tools:

- `tools/window_catalog_portable_user_matrix.py`: JSON-configured native, WSL,
  emulator and Node runners; writes a report and isolated files.
- `tools/window_catalog_portable_browser_test.py`: import unchanged desktop USER
  through the browser's own filesystem, reopen a child and cold restart.
- `tools/window_catalog_portable_windows_test.py`: import foreign USER into the
  real Windows application, operate native controls and restart.

The matrix configuration uses per-platform command arrays (WSL runners set
`path_style: "wsl"`; emulator runners set `kind: "adb"`, executable and isolated
remote directory). Build the fixture targets alongside each backend's Debug
producer so each receives its own freshly generated embedded header. Run:

```text
python tools/window_catalog_portable_user_matrix.py --config <runners.json> --output <new-artifact-directory>
```

Use a fresh isolated `remote_root` for each emulator matrix run as well.

Fixture targets:

| Backend | Target |
|---|---|
| Windows / Linux | `apptraverse_window_catalog_portable_user_fixture` |
| Android NDK | `apptraverse_window_catalog_android_portable_user_fixture` |
| Web / Node Wasm32 | `apptraverse_window_catalog_web_portable_user_fixture` |

The similarly named host Android fixture is supplementary coverage; it is not
substituted for the NDK executable running in the emulator.

## Final acceptance

Final frozen-source acceptance is complete. Counts below describe the affected
regression scope, not every test in the repository.

| Scope | Result | Evidence |
|---|---|---|
| Windows affected Debug regression scope | 40/40 PASS: 39-test batch 282.33 s, native acceptance 13.12 s | `.artifacts/portable-user-windows-accepted-tests.log`, `.artifacts/portable-user-windows-accepted-native.log` |
| Four-platform seed/transfer/edit/return matrix | 52/52 PASS; all 32 read-only hashes unchanged, including 8 actual Android readbacks | `.artifacts/portable-user-accepted-dispatch/report.json` |
| Linux affected headless/GTK regression scope | 16/16 PASS, 57.27 s; native acceptance 51.86 s | `.artifacts/portable-linux-tests.log`, `.artifacts/portable-linux-tests.xml`, `.artifacts/portable-linux-x11.log` |
| Web embedded session, actual Wasm32 under Node | PASS | `.artifacts/portable-web-accepted-embedded.log` |
| Web functional UI, fresh Debug, child Close and two cold restarts | PASS | `.artifacts/portable-web-accepted-functional.log` |
| Web headed lifecycle, tab visibility/navigation and full Chrome restart | PASS, 13.907 s | `.artifacts/portable-web-accepted-lifecycle.log` |
| Windows USER → browser UI → Windows verification | PASS | `.artifacts/portable-browser-accepted-transfer.log`, `.artifacts/portable-browser-return-windows-accepted.log` |
| Android actual emulator instrumentation | 7/7 PASS, 60.226 s | `.artifacts/portable-android-frozen-final-seven.log` |
| Windows USER → Android UI → Windows verification | PASS, exact expected Add/Close/selection changes | `.artifacts/portable-android-final-evidence.json`, `.artifacts/portable-android-windows-return-final/verification.json` |
| Windows USER → Linux GTK UI → cold Linux restart | PASS, main plus Windows 1/3 restored, Window 2 remained closed; both exits 0 | `.artifacts/portable-linux-win-gui-result.json`, `.artifacts/portable-linux-win-gui-restart-result.json` |
| Android / Wasm USER → Windows native GUI, reopen/save/restart | Both PASS | `.artifacts/portable-user-windows-accepted-from-android/native-report.json`, `.artifacts/portable-user-windows-accepted-from-web/native-report.json` |

The Android import used the fresh Windows seed with SHA-256
`892c0c9bfcec067ab1b20d7d46a5f51a0ad6b9c12fb62033b9589f9d473547eb`.
The real UI restored Russian rows, reopened and closed a cold child, navigated
with the original physical swipe, added Window 4, saved on backgrounding and
restored Window 4 after process restart. The default Android USER was not used
or changed. All temporary input diagnostics were removed.
The original installed APK was restored without launching its default profile;
the original 7,964-byte USER retained SHA-256
`db911cab158e3d50dd8f44de72cac49d492f8aba152680f9af9c0509a5547689`.
The tested new APK remains in `examples/surfaces_demo/android/app/build/outputs/apk/debugLoadOnly/app-debugLoadOnly.apk` (11,850,714 bytes, SHA-256
`0d700d63c451632a8f30267be39bd86fe0e2a7e8d356a9f6c9b6809bb77b3f48`).
The restored original APK has SHA-256
`03318756f9b4cff0193971e6e820782aa9795338a4203c0773bc93772cc929ba`.

The Android rotation-test failure was localized with InputDispatcher and touch
timestamps: accessibility exposed the Activity before its window could accept
input, and a delayed DOWN became a long press. The test now waits for the actual
focused input window; the physical swipe and all model assertions are unchanged.
GUI acceptance ran without concurrent heavy builds. Production gesture handling
was not changed to accommodate the test.

The Windows native harness's synthetic button command also required an explicit
foreground precondition. An initial run restored the child and its geometry but
failed the subsequent focus assertion. `ClickControl` now activates and verifies
its own parent before sending `WM_COMMAND`, matching a real user click and the
existing explicit Open helper. The child-focus assertions remain intact; no
production focus behavior was changed for this test.
Windows blocked background activation while another application held foreground.
The final run established the interactive precondition with one neutral titlebar
click on the test-owned main window, after checking its PID and `WindowFromPoint`.
The recorded observer then confirmed foreground transfer and all native assertions
passed. Initial failed attempts remain in the local diagnostic artifacts.

The Windows scope includes 13 additional ownership, serialization, origin,
historical-base and compaction regressions, the isolated releasing-edge test,
queued native input/duplicate Close tests and the binary-contract policy test.
The initial failed attempts and earlier passes remain distinguished in the
[progress document](../cross-platform-user-state-plan-progress.md). Close timings
observed during parallel builds are not an A/B performance result.

## Limits

This is same-schema, matching-supply interoperability, not an upgrade mechanism
for older experimental files. Android device verification uses the x86-64
emulator; ARM hardware is not claimed. Web native UI verification uses Chrome;
Wasm32 serialization is additionally executed under Node. Apple source schemas
are kept consistent, but no new macOS/iOS runtime verification was performed on
this Windows host. MinSizeRel and unrelated example applications were not rebuilt.
Native placement remains subject to the host window manager, including the
previously documented WSLg positioning limitation. Maximization remains deferred.
