# Apple chat: current main update — 2026-11-13

## Revision and scope

Updated AppTraverse from `908a2646` to upstream main `6751f326` by fast-forward
on `feature/chat-latest-mains-20261113`. Checked the dependent upstream mains:

| Repository | Main revision used |
| --- | --- |
| aether-objects | `fa4cd4d0f0bb1b79b03e58eb723651091deb20e9` |
| miscpp | `6839624442621a5b060ae610e91f480278ebaf6e` |
| numeric | `8bda57f2fe562109f722301dd916e907d1822244` |
| tele | `461475099559d8cbcd27f75e0817bcaf36e44896` |
| aether-client-cpp | `efd16915c6f2e0614c9d73390c9c4a18ce4eb294` |

The SDK pin remains `91aea53310335778b2bb4803cffff43d2b19b59b`, which contains
that SDK main plus the already published cloud-completion, socket-owner-loop,
and iOS c-ares fixes. Those three fixes are on the SDK feature branch, not its
protected main. This update does not modify SDK sources or pins.

Only native macOS and iOS Simulator Debug builds were requested/performed.
Windows, other backends, physical iPhone and release configurations were not run.
Existing profiles were preserved; cloud tests used fresh private profiles for
the current S0 schema and matching supply. `--load-only` explicitly exercises
cold UI restore in Debug, while ordinary Debug retains its fresh-graph policy.

## Adaptation and fixes

- Exposed the existing portable chat UI/model, checkpoint and durable-send tests
  to native macOS in CMake.
- Adapted Apple acceptance to the current common UI graph: participant scroll
  view, two native USER files (`user.bin` and `transport/user.bin`), and exact
  checks of native typed UID/message values.
- Corrected compact viewport geometry in the common model so history does not
  overlap the composer when the iPhone keyboard reduces the viewport. Regression
  checks cover 393×156 and 393×500 and preserve desktop restore geometry.
- Read TCP/pending diagnostics as a synchronized value copy from the live
  transport, outside the persistent UI graph. They are eventual counters, not
  part of the UI publication revision. Only the explicit acceptance driver
  requests periodic counter snapshots; product presentation remains event-driven.
- Fixed unnecessary UI model commits on transport diagnostics-only updates.
  A notifier regression first failed on the old `AcceptTransport`, then the
  projection comparison passed in the final 27-test run. It occurs before
  Event allocation/commit. Initial room
  actions remain independent of whether the projection changed.
- Split runtime pending diagnostics into total/progress counts. The current
  transport sends periodic progress reports, so total zero is not a reliable
  quiescence criterion. Acceptance still requires ACKs for every Event,
  initial state and coverage receipt, and explicitly checks a non-progress
  obligation while a recipient is offline.
- Added an activity assertion only to `CHAT_ACCEPTANCE`, whose windows are
  intentionally behind the foreground app. The assertion lasts through orderly
  shutdown; ordinary product lifecycle/energy behavior is unchanged.

## Executed build and deterministic checks

- macOS `shared_node_chat` and native acceptance executable: **PASS**.
- iOS Simulator `shared_node_chat` (x86_64, iOS 17.2): **PASS**, signed and installed.
- iOS XCTest runner build: **PASS**.
- Final selected native SDK/SharedNode/serialization/runtime/presenter/storage
  and chat UI model tests: **27/27 PASS** (40.92 seconds), including compact
  geometry and diagnostics-only notification regression. The first run also
  passed 26 tests plus the separate Debug-only UI test before the no-op fix.
- Explicit real-cloud UI checkpoint test: **PASS** — prior/later Sends settle,
  cold UI and transport USER agree, input remains available, callbacks fire once.
- Explicit real-cloud durable-send test: **PASS** — rejected drafts, local commit,
  durable records/cold restore, shutdown and startup-failure completions.
- Python driver syntax and `git diff --check`: **PASS**.

The UI model test is Debug-only; the final combined invocation used `ctest -C Debug`.
The two explicit cloud tests are deliberately outside default CTest.

## GUI acceptance

Final macOS run: **PASS**, 19 checks in 222.89 seconds. Three native SwiftUI
windows joined, rendered each Unicode message exactly once, verified TCP and
data ACKs, recovered durable delivery after sender/recipient force restart,
retained identities/history, exchanged another message after 180 seconds idle,
and shut down with both native USER files. Native screenshots were captured.

Final iOS Simulator run: **PASS**, XCTest 1/1 in 378.674 seconds and the external
Mac/iPhone driver PASS. Native typing/Join/Send, Unicode Mac reply, participants,
rotation actions, background/foreground, and cold restore all completed.
The driver verified the same iPhone PID before/after background and a different
PID after restart, four messages rendered on the Mac, and both UI and transport
USER files on both sides. The installed executable hash matched the final build.
This verifies lifecycle survival and eventual delivery; it does not establish
execution while OS-suspended or the exact delivery time within the background
interval. Debug simulator performance remains slow; samples showed immediate
model compaction during native typing, without proof of a mutex deadlock.

Visual inspection of the three portrait attachments (Unicode delivery, return
from background, cold restore): input, Send and status remain above the open
keyboard, with history constrained to its scroll viewport. The landscape
attachment contains a transitional/clipped rotation frame with a large black
region; it is **inconclusive for settled landscape visual layout**, not a visual
PASS. The independent model bounds test still covers the short viewport. The
four original attachments are exported to `chat-main675-ios-cloud-final/screenshots/`.

The first concurrently executed runs failed: macOS timed out waiting for its
first Unicode UI delivery; iPhone timed out in `Finding the host room`. The Mac
message was already present in all three transport USER files, so this was not
proof of lost transport delivery. A sample showed the UI worker in structural
publication with no demonstrated circular mutex wait. App Nap / scheduling and
concurrent simulator load are hypotheses, not established product root causes.
The second run reached three-way Unicode delivery, TCP verification and native
screenshots, then was deliberately interrupted after the invalid total-pending
zero assumption was identified. The final repeat uses separate progress counts
and the model no-op correction; timeout thresholds and message-delivery
assertions were not weakened.

## Local evidence and reproduction

Private logs, profiles, screenshots and XCTest results remain outside the repo
under the task workspace `work/`:

- `chat-main675-{macos,ios}-build.log` and `chat-main675-{macos,ios}-final-build.log`
- `chat-main675-activity-build.log`, `chat-main675-xctest-build.log`
- `chat-main675-final-corrected-{macos,ios}-build.log`
- `chat-main675-test-names.txt`, `chat-main675-ctest.log`, `chat-main675-ui-model-ctest.log`
- `chat-main675-final-ctest.log`, `chat-main675-noop-before.log`
- `chat-main675-checkpoint-final.log`, `chat-main675-send-final.log`
- `chat-main675-macos-cloud-1/`, `chat-main675-ios-cloud-1/`
- `chat-main675-macos-cloud-2/`, `chat-main675-macos-cloud-final/`
- `chat-main675-ios-cloud-final/`

Builds use existing incremental Ninja trees `work/build-chat-macos` and
`work/build-chat-ios` with the pinned local SDK checkout `work/chat-sdk-main`.
The macOS compiler is MacPorts clang 20/C++20, Swift is 5.9.2 from Xcode 15.2; the iOS
simulator is the installed iPhone 15 Pro, iOS 17.2. No simulator/profile reset.
