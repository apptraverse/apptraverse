# SharedNode chat scrolling and Send-state investigation — 2026-11-14

Branch: `feature/aether-fcm-push`, following `294947ea`.
Scope: the existing Windows host and Android emulator chat, with the same
profiles and Firebase service. No credentials or persistent schema changed.

## Changes

- Android scrolls changed message history to its end in the next pre-draw,
  after TextView measurement and viewport layout. Scrolling does not move
  composer focus. Draft/status-only publications do not trigger it.
- Windows selects the explicit end of changed history before `EM_SCROLLCARET`.
  The previous start index of `-1` only removed the selection, so it was not a
  reliable way to select the new end. The composer keeps focus.
- Send diagnostics distinguish transport command queue time, local commit time
  and elapsed time through `ChatUi::CompleteSend`. They contain no message text,
  device tokens or credentials. The last measure excludes the subsequent native
  presentation; it is not a delivery or push latency measurement.

## Send-button behavior and reported pause

`ChatUi::RefreshControls` enables Send when
`connection.ready && connection.in_room && !editing.sending`.
Participant Online status is not part of this expression.

`SendStarted` marks an operation in flight. Completion of the local SharedNode
commit calls `CompleteSend`, which clears that flag on both success and failure.
Success clears only the submitted draft revision; a newer draft survives.
Failure retains the draft and presents the rejection. This waits for local
durability, not the recipient's delivery ACK or the Firebase response.

The user reported that the button eventually recovered by itself. The original
long pause was **not reproduced or attributed to a proven cause**. Queueing,
local persistence and model publication were inspected; no permanent busy-state
path was demonstrated. The send state machine was not changed to hide the delay.
Added diagnostics are for identifying the delayed stage if it recurs.

## Executed verification

- Incremental Windows Debug build and UI-model CTest: **PASS, 1/1**. Checks
  include busy Send/usable Input, duplicate-click suppression, failed completion
  and preservation of a newer draft.
- Explicit real-cloud UI checkpoint test: **PASS**. While an injected transport
  flush gate is held, the UI mirror has a disabled Send and enabled Input;
  after release Send becomes enabled and the newer draft remains. Existing
  checkpoint ordering, callback and cold-restore assertions also pass.
- Explicit real-cloud send-commit test: **PASS**. Rejected drafts, durable local
  commit/cold restore, shutdown and startup-failure completions.
- Firebase-enabled Android Debug assemble and lint: **PASS**, zero lint errors,
  the same two existing example warnings as the prior review.
- Live Windows → Android: scroll history up, keep an Android draft with the
  keyboard open, then receive a Windows message. The new last message becomes
  visible and draft/focus remain. Scroll up again and type: history stays up.
- Live Android → Windows: Windows history starts at scrollbar value 0 with a
  draft in the focused composer. Android's message moves it to value 100;
  the new last line is visible and draft/focus remain.
- Three Windows sends (Return, Return after idle, mouse Send) all arrived on
  Android and completed with enabled Send/empty submitted draft. Queue times:
  **13 / 2 / 10 ms**; local commits: **27 / 22 / 25 ms**; model completion:
  **171 / 135 / 151 ms**. All three push-service responses were status 0.

Evidence in ignored build artifacts: `scroll-native-build.log`,
`scroll-android-build.log`, `send-checkpoint-01.log`, `send-commit-01.log`,
`scroll-host-err.log`, `scroll-android-{before,after,draft-only}.png`, and
`scroll-android-{sent,idle,final}.xml`. Windows scrolling and composer state were
also inspected through native screenshots/accessibility.

Linux/Web message presenters and Apple's existing count-based scroll were
inspected for the analogous behavior. They were not changed or runtime-tested;
this report claims scrolling acceptance only for Windows and Android.
Long stalls, outages and physical Android hardware are not covered by this run.
The updated Windows and Android apps and existing service were left running.
