# SharedNode Chat non-Apple native migration - 17 November 2026

Implementation: GPT-6.1 Sol high in the coordinated tree based on `fd08d202`.
This report covers Linux/Web/Android adapters and RAM-only transport. The
[30-rule audit](all-applications-ui-canon-audit-2026-11-17.md) and
[Windows/common report](shared-node-chat-generic-ui-2026-11-17.md) retain their
own executed boundaries.

## Implemented contracts

GTK, Web and Android configure the reusable native window, button, text and
existing list presenters. The shared constraint engine owns layout; native
measurement and arranged rectangles stay local. Hidden role-specific controls
are omitted from the active layout. Native labels measure and wrap their text.
Android retains the original void button JNI callback used by MinimalApp.

Native edits and acceptance use proxy/model Events and publications. Pending
edit revisions protect newer input from stale acknowledgements. Presenters
preserve selection and avoid repeatedly reconfiguring the native editing mode.
The transcript is a read-only multiline control; participants use UiList.
Send returns focus to the composer. Semantic accessible labels are model data:
GTK uses ATK names, Web uses aria-label and Android uses contentDescription.
Static GTK labels expose their visible text. Android text uses themed SP units.
Clipboard work runs after a published request and reports its result through
the proxy. A fresh Android Session resets its host copy counter; remount does
not. No presentation polling timer was introduced.

Chat business/history/drafts/names/pending receipts stay in ordinary RAM.
`ChatSession` no longer loads or saves a business USER file. Logical checkpoint,
ACK, retry and journal compaction retain native graph snapshots in RAM. SDK
identity remains separately persistent. UI lifecycle saves capture device/window
LOCAL in the normal native format. Web saves the actual active profile path,
including Debug run suffixes; it does not invent a business USER file.

A fresh RAM replica reusing its SDK identity can explicitly rejoin a live host.
Join v2 carries a fresh native request identity. Known members receive a new
initial snapshot without duplicate membership Events; retries of that request
retain their frozen packet. The deterministic RAM test covers rejoin and sends
in both directions. Existing shared synchronization behavior is unchanged by
default; completed-peer refresh is explicit.

Activity recreation retains the Android Session and remounts its native graph.
A finishing Activity stops that Session. A replacement attached while the old
owner is finishing first tears down the old Session/UI against its old host
references, then creates a fresh Session. Java teardown and native stop/checkpoint
callbacks are owner guarded, so a delayed obsolete callback cannot destroy or
checkpoint the replacement's Session. The final source has no input trace,
publication-hold switch or per-publication profiler. The common publication
implementation now shares one hydration context and hydrates materialized IDs
only when their incoming native layers differ exactly, while still injecting
all layers and applying generation/frame intent sidecars.

## Executed acceptance

| Scope | Result | Boundary |
| --- | --- | --- |
| Linux GTK native chat | PASS | Final Debug rebuild and Xvfb/Openbox: actual keyboard editing, Unicode model/native text, semantic ATK composer name, caret under unrelated publication, remount after nonzero revision, Enter once, button focus, empty business after restart and LOCAL dark cold restore. Final run has zero ATK critical warnings. |
| Web Chrome native chat | PASS | Rebuilt after embedded-JavaScript formatting repair; actual two-peer cloud Copy/Join/all-peer Unicode, rapid input, caret under remote publication, Enter once, Send focus/replacement draft, LOCAL capture, business USER absence, same-SDK restart/rejoin/snapshot/send. aria-label values checked in DOM. |
| Android native widget/lifecycle | PASS | Final post-ownership-fix Debug APK on emulator API 34: actual cloud Unicode both directions with an owned Linux peer; native names/read-only/list, caret, Enter once, rotation/draft, font scale 1.0 to 1.15 with native size increase, Activity replacement/remount after nonzero revision, subsequent edit/Send/focus, immediate same-process fresh business/draft reset, obsolete-owner JNI teardown ignored, first fresh Copy clipboard sentinel. |
| Android plain native input | PASS (3/15); optional 40 NOT PASS | Final post-R6 plain APK, unpaced adb 3- and 15-character bursts with exact native text and focused composer. The 40-character adb stress input ended at the first 15 characters and remained there on a later observation. |
| Android native LOCAL cold process | PASS | Fresh UI profile with LOCAL absent from its first load-only launch, separately reused owned SDK identity, real native Dark menu action and published checked state, onStop/final native file save; external force-stop, changed PID, same nonempty SDK UID, Dark restored, empty history/draft, no business USER files. |
| Android optional long native input | Partial | Native IME commitText40 reached the model/transcript exactly once and cleared. Standard Instrumentation.sendStringSync40 passed exact native text/focus checks; the subsequent physical-key Enter transcript/clear predicate timed out, so this complete optional case is NOT PASS. |
| Android current Catalog affected consumers | PASS (root worker) | Two existing native cases: add/swipe-close/reopen/delete and localization reaching the existing child; 136.422 s, instrumentation code -1. |
| Android MinimalApp affected consumers | PASS (root worker) | Two existing native cases: Row B/Apply title and enabled state, plus active Detail Activity remount; 51.651 s, instrumentation code -1. |
| RAM-only/rejoin deterministic test | PASS (Windows worker) | Injected transport, old business-file sentinel untouched, checkpoint/restart empty, same-identity rejoin and both-way sends. |
| Windows actual three-peer cloud | PASS (Windows worker) | Unicode, offline send, same-SDK explicit re-admission and snapshot/send; see Windows report. |

Final commands/evidence are local, uncommitted artifacts because SDK profiles
and raw cloud logs can contain private identifiers:

- `.artifacts/generic-chat-linux-runtime.log`: explicit `apptraverse_shared_chat_linux_native_test` under Xvfb/Openbox, fresh UI profile and separately reused owned SDK identity.
- `.artifacts/generic-chat-web-runtime.log`: `python tools/shared_chat_web_generic_test.py --url http://127.0.0.1:8793/examples/shared_node_chat/web/ --profile-dir .artifacts/generic-chat-web-browser`.
- `.artifacts/generic-chat-android-final-sequential.log`: final post-R6 3/15 PASS followed by optional 40-character NOT PASS; this wrapper did not run later scopes.
- `.artifacts/android-window_catalog-root-final-runtime.log` and `.artifacts/android-minimal_app-root-final-runtime.log`: independently executed affected-consumer regressions.
- `.artifacts/generic-chat-android-native-runtime.log`: final native instrumentation result PASS, instrumentation code -1.
- `.artifacts/generic-chat-android-local-prepare.log` and `.artifacts/generic-chat-android-local-verify.log`: both instrumentation code -1, distinct processes, same owned SDK identity.
- `.artifacts/generic-chat-android-long-native.log`: IME40_PASS, native fresh-key40 text/focus established before the later hardware-Enter timeout.
- `.artifacts/generic-chat-android-build.log`: final Debug APK and dependency-free native instrumentation APK build.

## Input diagnosis and corrected validation

The first Android cache had SDK diagnostic logging enabled, unlike the accepted
Catalog/Minimal baseline. The final Debug/O0 configuration uses the project's
`AE_NO_DEBUG_LOG=ON` policy. Repeated full publication hydration initially loaded
81 materialized objects for each edit. Diagnostic samples showed about 631 ms
for that reload stage; after exact layer comparison, an edit changed two IDs
and one sample measured 71 ms for that stage. Host load varied, so these are
causal diagnostic observations, not a performance benchmark.

A temporary held-publication experiment isolated native key delivery under
publication load. That switch was removed before final acceptance. Actual
normal-publication 3/15-character typing subsequently passed. All temporary
Java/C++ input traces and common stage profilers were removed.

The initial peer helper had incorrectly encoded test literals and therefore
failed to recognize the outbound Unicode marker. That attempt is not evidence
of a transport defect. The corrected Java and Python helper sources use ASCII
Unicode escapes and verify the actual payload. Likewise, the first final Web
run was invalidated after C++ formatting split JavaScript operators and its
profile regular expression; protected embedded-JavaScript regions were repaired,
rebuilt and passed the actual Chrome flow above.

The initial focused LOCAL attempt timed out at new SDK readiness before any
preference stimulus. This is a prerequisite timeout, not a LOCAL restore result;
its underlying cause was not established. The passing case used a new UI profile
whose LOCAL file was confirmed absent, with its transport directory linked to a
separately owned, previously proven SDK profile. The resolved link stayed within
this app's private native test directories. No chat business state was copied.
The prepare/verify modes both used load-only from their first launch; this
avoids confusing a Debug fresh run's separate LOCAL path with the base profile.

The optional unpaced `adb shell input text` 40-character injection exited zero,
but the native field contained only its first 15 characters, still focused; a
later stable observation had the same result. A stock Settings comparison could
not obtain its UI observation and supplies no comparison result. In a separate
native route, IME commitText40 passed model/send/transcript/clear and once-only
checks. Instrumentation's 40-character key injection also reached the exact
native text with focus; the subsequent physical-key Enter send/clear check timed
out. The final field after that timeout was unavailable once instrumentation
finished, so Enter delivery versus send/ack is not attributed.

[AOSP Instrumentation](https://android.googlesource.com/platform/frameworks/base/+/a0e945efa25910646434cf7c74c9ac173cb1b729/core/java/android/app/Instrumentation.java)
refreshes individual key event times when injecting a generated string. An old
batch timestamp is a possible explanation for the adb stress failure, but no
actual InputDispatcher drop trace was captured here. The adb case and complete
hardware-Enter-after-stress case remain NOT PASS. No pacing workaround or broad
core optimization was added to conceal either result.

The Activity fixture observes the latest lifecycle-created instance and waits
for its native control before reading it. Font scaling is an actual system
setting change with an asserted 1.0 baseline and native text-size increase. The
fixture permits an explicit Activity.recreate if the OS has not replaced it;
this evidence does not separately certify automatic font-triggered recreation.
Earlier observer and test-literal failures remain private failed attempts, not
passing product evidence.

## Limits

Apple sources and runtime are excluded. Actual Windows 7, Release runtime,
physical-device/mixed-DPI/screen-reader matrices, Android Recents dismissal,
and actual OS image upgrade are NOT RUN in this worker's scope. DOM aria-label
checks are not a browser screen-reader certification. The historical Recents
gesture failed on stock Settings too; no 9/9 Android claim is made. Historical
platform reports remain historical and do not establish current RAM persistence.
