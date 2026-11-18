# macOS chat: current-main Release and SwiftUI startup

Base: fetched main `6f37a363cf1b2fe2df712c70767f85920166c7c4`.
SDK: pinned clean checkout `0c2b8936b4418af55ed4cdb00405d99fc329422a`.
Work branch: `fix/apple-main-release-20261117`. Previous Apple worktree and its
unfinished SwiftUI changes were preserved. No Windows build or main merge.

## Failure and correction

Unmodified main built in Release, but its Apple consumer exited at startup with
`fatal: LoadApplication failed id=9500`. The shared UI builder now produces the
standard typed component graph; Apple's legacy registration/supply composition
had not been adapted. Its old bridge also read runtime layout fields that the
new graph does not serialize.

The macOS target now produces its own supply using the common builder and real
registered Apple text/button/list presenters. The shipping consumer loads that
exact supply, with LOCAL routing before Load and an OS baseline before initial
publication. The bridge measures native controls and runs the common constraint
solver on the mirror; viewport observations do not rewrite model geometry.
Input continues through the existing proxy/Event/publication path. There is no
headless/dummy presenter substitution or graph repair.

The app and window are created by SwiftUI `App` / `Window`. A native adapter
attaches to SwiftUI's window for geometry, OS observations, APNs and shutdown;
it does not create a replacement NSWindow or replace its delegate. Existing
editing ACK behavior and shared model/UI sessions remain in use. No core
library or SDK implementation changes were made. The common chat UI session
only gains a compile-time platform-supply include override.

Chat business/history/drafts/receipts remain RAM-only under current main's
contract. Native LOCAL and SDK identity remain separate. LOCAL placement is
reported as outer-frame logical points, with native monitor and DPI metadata.

## Executed evidence

- Release build: PASS. Ninja, MacPorts clang 20, Xcode Swift 5.9.2, SDK 14.2,
  minimum macOS 13.0. Existing dependency cache reused; other examples disabled.
  The transport supply is the checked-in `generated/chat_supply.h` from this main.
- Shipping-symbol check: PASS after Release dead stripping. `nm -C` contains no
  BuildUi, FinalizeDistilledGraph, WriteEmbeddedDistilledSupplyHeader,
  StabilizeDistilledPersistentSnapshot or SaveDistilledRoot symbols. Final
  executable: 3,957,896 bytes. Native system/Swift runtime frameworks remain
  dynamic, as shown by `otool -L`; this is not a fully static macOS executable.
- Exact emitted Apple consumer supply test: PASS. Loads through native
  USER/LOCAL/BACKING routing, verifies required layout/device links and title,
  publishes into a distinct ApplicationUiSession mirror, activates all twelve
  control projections after the window, and removes the window on teardown.
  Checks execute under NDEBUG. This is a code-level consumer test, not OS UI
  automation or native interaction proof.
- Repaired Release launch: process remains alive without the original fatal.
  Launch parameters, PID, profile and binary size are recorded in the external
  `work/chat-main-release-20261117-launch.json` manifest.
- Visual GUI inspection: NOT VERIFIED. Both native-view attempts failed with
  `Sky Computer Use service startup request failed`. No alternative UI
  automation was used. No new cloud delivery, APNs, input, cold-restore or iOS
  runtime acceptance is claimed.

External logs are under `work/`: `chat-main-release-20261117-build-final.log`
(original build), `chat-main-release-20261117-launch.log` (original failure),
`chat-main-release-20261117-apple-fix-final-build.log`,
`chat-main-release-20261117-final-link.log`,
`chat-main-release-20261117-supply-test-build.log`,
`chat-main-release-20261117-supply-test.log`, and
`chat-main-release-20261117-launch2.log` (repaired process).
The final symbol check is in `chat-main-release-20261117-symbol-check.log`.

## Scoped Canon dispositions

GPT-6 Astra ultra performed the required read-only source audit. Its conclusion
found no source blocker for this bounded Release startup repair. It did not
execute builds, UI interactions, commits or pushes; execution evidence above is
separate. Numbering follows Canon §25. These are source dispositions, not thirty
runtime PASS results.

| Rule | Disposition in examined scope |
| --- | --- |
| 1 | Typed required links are supplied by composition. |
| 2 | Normal mandatory Load; no recovery or partial graph substitution. |
| 3 | Common builder initializes data; native subclasses add no persistent fields. |
| 4 | Required readiness and parent lifecycle remain inherited. |
| 5 | Proxy/Event input retained; derived rectangles stay UI-local. |
| 6 | Native measurement uses the common constraint solver. |
| 7 | Existing shared model/UI sessions reused. |
| 8 | Explicit native registration and factory composition. |
| 9 | Existing normal-Debug fresh policy retained; this task executes Release. |
| 10 | Separate build-time producer and shipping consumer; Release dead stripping added. |
| 11 | Ordinary independent RAM stores retained. |
| 12 | Shutdown saves LOCAL; business state remains RAM-only. |
| 13 | Native serialization retained; projection dictionaries are transient. |
| 14 | Ownership remains in sessions; window/callback references are weak. |
| 15 | Domain and Clone contracts unchanged. |
| 16 | Solver filters a local layout copy; supply is not mutated. |
| 17 | Existing lazy graph semantics retained. |
| 18 | Existing history/compaction semantics retained. |
| 19 | Presenter teardown remains separate from business deletion. |
| 20 | Authorized fixed-English chat labels and native chrome retained. |
| 21 | PARTIAL: baseline, frame Events and initial LOCAL outer restore implemented; see limitations below. |
| 22 | Shared business model retained; supplied consumer projection test executed. |
| 23 | S0-only native subclasses; no old-profile migration. |
| 24 | No recovery subsystem or assert-only side effects introduced. |
| 25 | Native presenters have actual shipping consumers. |
| 26 | Existing C++20/no-RTTI/no-exceptions compile policy retained. |
| 27 | Release/macOS explicitly requested; caches retained. |
| 28 | Existing authorized SharedNode/APNs scope; no new SDK/network mechanism. |
| 29 | Source, process startup, consumer test and unverified GUI scopes separated. |
| 30 | Isolated feature branch; old WIP preserved; no main merge. |

Rule 21 does not establish full Apple LOCAL acceptance: live placement-revision
application, equal-data RequestFrame handling and work-area clamping are not
implemented by this adapter. Destination-window versus main-screen DPI behavior
and physical mixed-DPI/accessibility matrices remain unverified. iOS was not
built or run in this task. Historical non-Apple evidence is not extended by this
Apple-only repair.
