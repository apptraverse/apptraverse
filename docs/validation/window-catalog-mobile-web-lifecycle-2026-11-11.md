# WindowCatalog Android/Web lifecycle persistence — 2026-11-11

Base: `3d020bf6dbbcf2acb88e3b2f1bb23368216f9be5`, branch
`feature/domain-hierarchy-cloning`. Scope: ordinary Android task close/relaunch,
browser tab/window close/relaunch, and the existing shared USER codec. No Apple
branch, other example, Release configuration or maximization work was included.

## Causes and implementation

The Android APK installed after the previous acceptance was **normal Debug**.
Its installed SHA matched the normal-Debug artifact, and saved USER files were
already present. `Activity.onStop` was saving correctly; normal Debug deliberately
starts a new graph and ignores previous USER on the next process launch.
The persistent **debugLoadOnly** variant is now installed for manual use.
It reads `files/window_catalog_state/user.bin` through the ordinary launcher;
old debug-run profiles remain untouched. Android production lifecycle code did
not change. Instrumentation now checks actual Back/Home and Recents dismissal,
in addition to the existing model/viewport/activity-replacement cases.

The previous Web acceptance covered only the explicit **Close application**
button. Before this fix, a real tab close followed by reopening the same Chrome
profile lost the child and RU language. Browser visibility/pagehide had no save
handler, and asynchronously starting an IDBFS flush after teardown would be too
late for a departing page.

The shell now handles hidden `visibilitychange`, with `pagehide` as fallback.
The shared Web host puts a synchronous barrier after accepted model work. The
worker captures the model into its existing RAM USER and serializes the complete
USER bytes; this callback performs no filesystem, browser or UI operations.
On the browser main thread, the backend writes the same `user.bin` into MEMFS,
queues its bytes and parent directory metadata in the already-open IDBFS
database, and calls `IDBTransaction.commit()` before the lifecycle event returns.
No asynchronous open/enumeration/request callback is needed to start the commit.
The file bytes are copied out of Wasm's SharedArrayBuffer before IndexedDB use.

Repeated hidden/pagehide notifications for one departure share a single save;
visible/pageshow resets that guard. Hiding the page does not stop the live model.
The explicit Close button also commits this snapshot before requesting stop:
otherwise Close followed immediately by reload could skip saving in `kStopping`.
Its existing asynchronous shutdown/save path still completes and acknowledges
IDBFS while the page remains alive; this intentionally retains an additional
final save at explicit shutdown, not a foreground-action write.

`SerializeUserStorage` and `DeserializeUserStorage` expose the existing binary
representation without filesystem access. File helpers delegate to this codec.
The encoding is unchanged: layer table followed by scope metadata. There is no
new persistent store, localStorage copy, schema, sidecar or per-object file.
Foreground Add, child Close, reopen, Delete, locale changes and resizing remain
RAM-only. Both platforms retain ordinary Debug's fresh-start policy.

## Browser acceptance

Windows host; Emscripten 3.1.64, SDK Node 24.19.0, installed Chrome
157.0.8103.39 and Playwright 1.62. Incremental Debug builds reused existing
dependencies, with assertions enabled and `AE_NO_DEBUG_LOG=ON`.

The new lifecycle runner uses an isolated persistent Chrome user-data directory.
It reads the actual IndexedDB `FILE_DATA` store independently of MEMFS, never
imports/exports Playwright storage state, and never dispatches fake lifecycle
events. The final headed run passed in **10.937 s**:

- Ordinary tab close/reopen restores the active child, stable ID and language.
- Reload in the same JavaScript turn as Add preserves the last queued command.
- Navigation away preserves a closed child's lazy state; reopen retains its ID.
- Real tab hide/return persists one USER file and retains the live page/model.
- Native Chrome window Close, process exit and relaunch with the same browser
  profile restore the latest changed language and open children.
- Close application followed by reload in the same turn preserves the new child.
- Working actions and child Close leave both MEMFS and IndexedDB unchanged.

The ordinary functional browser runner also passed in **8.005 s**: Add, touch swipes, paging,
Close/reopen/Delete, RU/EN, model viewport layout, explicit shutdown, two cold
restarts and fresh Debug policy. The Node embedded-session test passed on the
final build (`window_catalog_web_embedded_session_test OK`).

The genuine visibility test attaches to its own Chrome with
`connect_over_cdp(no_defaults=True)`; Playwright's default focus emulation would
otherwise keep tabs visible. Offscreen occlusion throttling is disabled for this
test, but real tab visibility still changes. CDP `Browser.close` skipped page
lifecycle notifications in this Chrome build; it is not used as evidence for an
ordinary window close. The native test sends WM_CLOSE only to the unique Chrome
window owned by its own subprocess PID. No user Chrome window/profile is used.

Observed lifecycle callback samples were **70.330 / 45.910 / 15.185 ms** in the
final Debug run. These are individual samples, not a performance benchmark.
Emscripten reports its known main-thread blocking warning for the synchronous
model barrier. The test allows that exact SDK warning and continues to reject
other script/runtime errors. There is no global diagnostic suppression. The
barrier must remain RAM-only to avoid filesystem/UI proxy deadlocks.

## Android acceptance method

The existing Android14/API34 x86_64 emulator remains in use. The final consumer
APK is explicitly assembled and installed after rebuilding the shared codec;
building only the instrumentation APK does not prove that the application APK
contains the newly built native library.

Final repackaged **debugLoadOnly: 7/7 PASS, 65.636 s**. APK SHA256:
`03318756f9b4cff0193971e6e820782aa9795338a4203c0773bc93772cc929ba`.

Final repackaged **normal Debug: 6/7 in the 94.173 s batch, then the unchanged
localization test PASS 1/1 in 10.096 s**. Both new strict task-dismissal cases
passed in that batch. The one failure did not reach the child after the existing
raw horizontal swipe: its page-shown trace stopped at the localized catalog.
The focused rerun showed child/catalog/child and passed without code changes.
This is full seven-case coverage across the batch and focused rerun, not a clean
7/7 batch claim. The transient navigation/input test limitation is retained;
production locale/model code was not changed on this evidence.

The final manual launcher check also caught an important supply constraint.
The host-export header had been regenerated after the earlier manual profile was
saved. That old 6,931-byte USER decoded successfully (65 objects and 65 scopes),
but its eight immutable text references were absent from both USER and the new
BACKING: for example `label_add=447870356` and `label_open=328494180`.
The regenerated supply assigned different constant IDs. This was a mismatched
USER/BACKING pair, not a byte-codec format regression. Cross-supply updates remain
explicitly unsupported by the prototype. The agent-created manual test file is
archived byte-for-byte before preparing a matching profile for this APK;
pre-existing normal-Debug profile directories are not modified. No migration,
runtime graph repair or silent fresh-state fallback was added.
The archived bytes are in app-private
`files/window_catalog_test_archive/pre-codec-rebuild-agent-created-user.bin`
and local `.artifacts/android-default-before-codec-rebuild-user.bin`.

The added scenarios use Back and Home, verify the single saved USER file, enter
the actual launcher Recents screen, select the catalog card by its app label and
wait for stable bounds. An object-bound `UiObject2.swipe(Direction.UP, 1.0f)`
dismisses that card. The test requires the captured task ID to disappear from
Recents before proceeding. Raw coordinate swipes sometimes left the card
unchanged; those failed checks were test-input failures, not evidence of failed
USER persistence, and the task-removal assertion was retained.
In the final controlled manual comparison, ending the raw drag at screen y=0
left the card in place; the same start/duration ending inside the card removed
it. The committed test uses the object-bound gesture, not a screen-edge fling.

Task dismissal is distinct from process lifetime: Android can cache a process
after its task disappears. Only after checking real task removal, the test
terminates any retained demo process and relaunches in a different PID. The
persistent variant must restore its child and RU language; normal Debug must
start empty and preserve the earlier saved file. The original default-profile
manual reproduction also observed a natural process exit after task dismissal
(PID10884 to PID11181), restoring one open and one closed child plus RU.

The final ordinary-launcher/default-profile check on the current APK also
**PASSed**: Back saved USER, real Recents removal naturally ended PID21102, and
cold launcher PID22112 restored active Window1, RU and closed Window2. The
persistent application remains open on its restored catalog. The emulator is
running, both pre-existing debug-run files are unchanged, and the older
agent-created manual USER is archived separately.

The existing five scenarios still cover native controls, Add/Close/reopen/Delete,
localization, rotation, Activity remounting, zero foreground file writes and
ordinary background/cold restore. No production lifecycle change was needed.

## Shared regression

- Windows **25/25 PASS**: 24-test batch 120.39 s plus native acceptance 10.52 s
  (10.59 s CTest total). The native test received one initial main-window focus
  activation; the child-focus assertion was not assisted.
- Linux **15/15 PASS**, 46.94 s, existing WSL/private Xvfb environment. The WSLg
  position limitation documented on 2026-11-10 remains unchanged.
- Codec tests check byte/file interchange, scopes/tombstones, replacement of
  output buffers, no filesystem operations in the codec and preservation of
  existing decoded state on incomplete input.
- Windows Close 5-by-4 diagnostic completed: HWND medians
  **218 / 232 / 187 / 170 ms** (cold/immediate/settled/saved). Linux compilation
  ran concurrently, so these are functional regression observations, not an A/B
  performance claim.

## Contract and scope

A lifecycle commit request is not a synchronous durability acknowledgement.
The lifecycle tests prove actual cold restoration for the named Chrome version;
they do not promise recovery after process crash, power loss or termination that
delivers no lifecycle notification. Other browsers, mobile-browser devices and
Android ARM64 remain outside this executed scope.

The browser approach follows the documented hidden/freeze transaction boundary:
[Chrome Page Lifecycle](https://developer.chrome.com/docs/web-platform/page-lifecycle-api).
`pagehide` is a fallback rather than the only notification:
[MDN pagehide](https://developer.mozilla.org/en-US/docs/Web/API/Window/pagehide_event).
The worker callback constraint follows Emscripten's main-thread/proxy caveats:
[Emscripten pthreads](https://emscripten.org/docs/porting/pthreads.html).

## Reproduction and evidence

See the Android and Web example READMEs for build/install/server/test commands.
For manual Web persistence use `web_window_catalog_demo_load_only.html`, not the
normal fresh Debug URL. The local server remains on port 8785.

Local evidence under `.artifacts/`:

- `android-close-reopen-verification.md`, `android-persistent-manual-result.json`
- `android-codec-final-loadonly-tests.txt`, `android-gesture-uia-home-focused.txt`
- `android-codec-final-debug-tests.txt`, `android-codec-debug-localization-focused.txt`
- `android-default-supply-mismatch.json`, `android-default-before-codec-rebuild-user.bin`
- `android-codec-final-restored.json`, `android-codec-final-restored.png`
- `wasm-browser-lifecycle-before.log`, `wasm-browser-lifecycle-final.log`
- `wasm-browser-functional-lifecycle.log`, `wasm-lifecycle-node-test.log`
- `wasm-browser-lifecycle/` (isolated Chrome profiles and screenshots)
- `web-user-codec-windows-tests.log`, `web-user-codec-windows-native-tests.log`
- `web-user-codec-windows-detail.log`, `web-user-codec-linux-tests.log`

The earlier port validation documents remain historical evidence for their
original, narrower scopes; this follow-up extends the lifecycle contract.
