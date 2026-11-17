# SharedNode Chat generic UI — 2026-11-17

Scope: common ChatUi composition/session and Windows native consumer, built with
MSVC desktop v143 14.44 in Debug. Apple source files remain untouched; their
legacy public Presentation/ControlState/Layout symbols are retained for source
compatibility. Apple hosts still need a separately authorized native-factory
migration and matching supplies. No Apple runtime or actual Windows 7 result is
claimed here.

## Implemented boundary

ChatUi composes the standard UiDesktopWindow/UiPresentedWindow presenter,
UiLayoutState/UiLayoutDefinition, supply-owned labels/locale, UiDeviceState,
UiPushButton and UiListContainer. The reusable UiTextControl supplies native
label, single-line edit and read-only multiline text roles. All native widget
creation, native font measurement and rectangles belong to the shared backend;
ChatUi no longer serializes ControlState.frame or viewport arithmetic. Its old
Resize API does not rewrite the portable semantic frame.

Every non-Apple host installs the complete typed UiFactories set before starting
the model. Headless callers explicitly select HeadlessUiFactories. Incomplete
composition is rejected; individual missing native factories do not silently
fall back to headless controls. Windows uses WinApplicationMessageHost and
WinPresentationHost, with no application-owned native control or window layout.

Input follows native control -> ModelObjectProxy -> UiTextEdited/Accepted or
UiPushButton Event -> common model -> publication -> UI mirror. A per-control
edit sequence prevents an older publication from replacing newer native input.
Send restores composer focus through a generic button presentation policy.
ChatTextControl and ChatActionButton use their most-derived NodeFor and retain
their ChatUi owner as a historical live reference. Their owner remains the
canonical live domain object through root and child immediate compaction.

The common session routes ClientLocalState before Load and prepares the native OS
baseline before its first publication. Only the UiDeviceState ownership graph is
saved to native LOCAL storage at lifecycle checkpoints/shutdown. Chat names,
drafts, messages, history and pending send receipts stay in RAM. CaptureUser is a
legacy compatibility API returning an empty native table; CaptureLocal exposes
the actual LOCAL table for Web lifecycle persistence. SDK identity storage stays
separate. Normal Debug uses a fresh graph and LOCAL directory; explicit
--load-only restores LOCAL only, never an experimental chat business user.bin.

## Executed evidence

All results below use the final common publication and accessibility source.
The final native supply was emitted by shared_node_chat and the checked-in
header was consumed by the rebuilt common model test.

| Boundary | Result |
| --- | --- |
| Common model send completion/rejection/revision handling, fixed English labels, local solver, independent mirror and final embedded supply loading | PASS, apptraverse_shared_chat_ui_model_test |
| Sequential typed input under root+child immediate compaction, canonical owner pointer/domain identity; partially configured factory negative | PASS in the same model test |
| Actual Win32 text controls: held stale publication, rapid edits, stable HWND/focus/caret, Send-clear versus newer typing, Enter once, read-only/multiline, hidden slots, native-font label minimum, local resizing, dark appearance, unload/remount | PASS, apptraverse_ui_text_control_win32_test (0.62s) |
| Native MSAA semantic names, EDIT role, value, readonly and disabled states | PASS in the same native text test |
| Native LOCAL checkpoint/restore, empty portable USER, exclusion of root/participants/text/buttons and draft content from LOCAL, fresh name/draft/history | PASS, explicit apptraverse_shared_chat_ui_checkpoint_test |
| Injected transport: RAM send/checkpoint, same-identity stop/start/rejoin/send, old business-file sentinel unchanged | PASS, apptraverse_shared_chat_ram_test (0.63s) |
| Real-cloud three native Windows peers | PASS: Copy, CLI/manual admission, Unicode all-peer delivery without duplicates, offline send, same-identity crash/re-admission/snapshot, subsequent all-peer send, common resize, accepted user-frame LOCAL restore, fresh RAM business state and clean shutdown |
| Core native serialization, presenter load ordering, clones, nested frame publication, unload/disk lifecycle, model runtime and UI session | PASS, seven current regressions; combined with RAM/native widget tests, 9/9 in 3.75s |

The RAM-only restart exposed a known-member re-admission defect: an already ACKed
admission no longer had an initial snapshot pending. Per-join request identity
and deduplication now permit a fresh snapshot for a known member, retaining an
existing pending snapshot unchanged. Both the injected pair and actual Windows
re-admission passed.

Windows accessibility uses the standard EDIT role/state/value and MSAA direct
annotation for its semantic accessible name. The API is documented for Windows
XP and later in [SetHwndPropStr](https://learn.microsoft.com/en-us/windows/win32/api/oleacc/nf-oleacc-iaccpropservices-sethwndpropstr)
and [direct annotation](https://learn.microsoft.com/en-us/windows/win32/winauto/using-direct-annotation).
GUID definitions come from the Windows SDK. No actual Windows 7 runtime claim is
made. The static PE check of final executable SHA256
A3B04CE16DF03BF303A5E23110B4F5E78CE8117DF43EF085D8C7384AAA155417 found
subsystem 6.00 and none of the checked known post-Windows-7 direct imports;
COM/ole32 is now used for accessibility. This does not prove CRT provisioning or
execution on Windows 7.

WindowCatalog and MinimalApp post-change native consumer refresh is separately
pending at this report's freeze; earlier baseline runs are historical evidence.
MainDyn and Surface final current-core acceptance is owned by their separate
validation report. Linux, Web and Android full chat acceptance is recorded by
the platform migration owner; this report does not infer it from Windows.

## Debug input timing investigation and correction

A temporary Android phase profiler measured a 10.5KB/81-object projection:
keepalive 46ms, decode 0ms, inject 14ms, root reload 480ms, all-object reload 530ms,
update 164ms (native sync 32ms), release 139ms, total 1475ms. Native layout took
5–21ms. Host load varied, so these are diagnostic samples, not benchmarks.

The opt-in chat caches initially had AE_NO_DEBUG_LOG=OFF; the prior Android
Catalog/Minimal cache used ON. The Windows baseline also used OFF. Setting ON
alone did not fix rapid input loss, including a quiescent-host test. Holding
future publications after startup on the same widget delivered all 15 native
characters, establishing publication work as the source of the stall.

Publication apply now compares exact native layer bytes against its existing
RAM storage before injection. It still validates/injects all incoming layers,
retains generation and frame-intent sidecars, and hydrates only changed objects
already materialized in the mirror. One DomainGraph spans the operation, so
cycles and canonical references share one load walk; absent objects remain
lazy. There is no new persistence or cache format. The regression checks
unchanged presenter/root load counts, child-only changes, duplicate publication,
canonical object identity and existing lazy/unload/clone behavior.

Android diagnostic samples after this change showed two changed objects rather
than 81, all-object reload 71ms rather than 631ms and total 786ms rather than
approximately 1.22s. These variable-load samples establish reduced work, not a
performance guarantee. The final APK with all diagnostics removed passed actual
native bursts of 3 and 15 characters with normal publication and retained focus.
All temporary profiling and held-publication source was removed.

Builds remain ordinary Debug without optimization flags. For new optional chat
builds CMake selects the SDK's existing AE_NO_DEBUG_LOG=ON verbose-diagnostic
policy before dependency configuration only when it is undefined. An explicit
OFF override is respected. This makes the checked build policy reproducible
without forcing unrelated consumers or disabling the telemetry API.

The dedicated Windows cache is
C:/Users/nickc/Documents/Codex/build-generic-chat-win7, using the clean pinned SDK
0c2b8936b4418af55ed4cdb00405d99fc329422a from C:/cpm/aether-client-cpp/e7aa and the
existing patched aether-objects 3785 source. No dependency download was required.
An out-of-space linker failure justified removing only this cache's .ilk scratch
and disabling incremental linking (/debug /INCREMENTAL:NO); source compilation
remains incremental Debug. Test profiles, binaries, SDK identities and raw logs
are private artifacts and are not part of this source report.
