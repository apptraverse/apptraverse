# SharedNode and Shared Chat across seven platforms

Scope is the opt-in SharedNode library and Shared Chat on AppTraverse main
`18ed55a5`. The [framework-wide review](canon-seven-platforms-2026-11-23.md)
covers WindowCatalog and MinimalApp. This review follows §20 and §25 of the
[Canon](../architecture/AppTraverse_Architecture_RU.md) through the chat ports
and the common model. The [SharedNode contract](../architecture/SharedNode.md)
and the specific authorization in [AGENTS.md](../../AGENTS.md) supply the
replication and chat persistence rules.

| Rules | Source review and corrections |
| --- | --- |
| 1–3 | Chat UI supply builders and native Load establish the graph. Session initialization applies the startup environment before publishing. Native failures propagate at startup or storage boundaries. |
| 4–6 | Windows, GTK, Android, Web and SwiftUI dispatch input through presenters and the model proxy. The session now skips notification during shutdown and abandons the unused snapshot at terminal teardown. Accepted work and transport completions still drain before the LOCAL save. |
| 7, 26 | The chat shares its model and runtime across all UI adapters, and uses the common C++20 policy. The object compression reader can use a combined user configuration that imports Æther constants. Only that translation unit receives the SDK include path. This fixes a Windows build with a combined configuration while preserving the core target's link dependencies. |
| 8, 25 | Common UI registration now belongs to the platform entry points and fixtures. Android anchors its native presenter registrations once at the process boundary, including for successive Activity owners. |
| 9–10 | Debug builds the chat graph. The explicit load-only mode restores UI LOCAL. Shipping code reads the generated supply. This review follows those source paths; the verification below distinguishes Debug from shipping builds. |
| 11–14 | Chat business state stays in RAM, and native lifecycle adapters save UI LOCAL and manage SDK identity separately. The common graph and publication use native object serialization. At stop, the model and native ownership remain alive until their queues drain. A Linux backtrace identified a second-launch abort when a full local telemetry archive kept the previous writer bound. The SDK patch discards complete overflow blocks and startup telemetry that cannot fit in local-only mode, preserving the disk cap and allowing the previous writer to detach. |
| 15–18 | SharedSync exports and imports the native network scope, including canonical object IDs. Runtime transport links stay local. UI event folding and distributed delivery checkpoints have different lifetimes. Their histories and replay use the existing object model. |
| 19–21 | The native owner controls close, background/deactivation and presentation remount boundaries. Views render labels from the active locale and feed native placement or viewport observations back to the model. |
| 22, 27, 29 | The headless target now gives CTest the selected configuration and includes peer-message fragmentation. The checkpoint scenario saves an accepted LOCAL change when the view closes without applying another publication. |
| 23–24, 28 | The app uses S0 UI state. SharedNode's library versions, ACKs, retries and network support are part of the expressly authorized opt-in extension. |
| 30 | Main was fast-forwarded before editing on the working branch. |

## Verification

- **Windows** — Win64 Debug built with the combined Æther configuration.
  Both chat CTests passed, as did the explicit LOCAL checkpoint test. The
  Win32 acceptance passed Unicode delivery among three real peers, identity
  restore and rejoin, clean shutdown, and geometry and maximization restore
  on a two-monitor desktop.
- **Linux** — WSL2 Debug. The RAM test and GTK/X11 native acceptance passed
  with the patched SDK object and the configured build's link inputs. The
  native run exercised real input, send, remount, resize, idle layout and a
  second session in the same process. Its telemetry object occupied 751,589
  bytes including directory metadata, within the 1,000,000-byte disk budget.
  The same GTK scenario reproduced the abort with the previous SDK object.
- **Android** — Debug APK and instrumentation APK built. The API 34 x86-64
  emulator passed the native LOCAL save and cold restore pair with the same
  SDK UID in a new process. Native IME and key input delivered both 40-character
  messages through the model and cleared the composer on Enter. Native
  acceptance passed Unicode exchange with a headless peer, accessibility
  labels, caret, rotation, font-scale Activity remount and replacement.
- **Web** — Emscripten Debug built and the Chrome test passed with two live
  peers. It covered copy, join, bidirectional delivery, caret and focus,
  Unicode, Enter, page lifecycle LOCAL save, SDK identity and live-room rejoin
  after reload. The Node service worker test also passed.
- **Headless** — The Debug CMake target passed all eight checks on Visual Studio
  and Linux, including fragments, shared synchronization, native
  serialization and three-peer replication.
- **macOS** — Source review of the SwiftUI/AppKit entry point, native placement
  observation, shared registration, publication and desktop shutdown. Build
  and runtime were not run because no Apple toolchain or runner is available.
- **iOS/iPadOS** — Source review of the SwiftUI/UIKit entry point, device and
  viewport observation, shared registration, publication and the deactivation
  checkpoint. Build and runtime were not run because no Apple toolchain or
  simulator is available.

The Windows, Android and Web runs cover the application and CMake fixes. The
later SDK telemetry overflow regression was reproduced and rerun on Linux.
