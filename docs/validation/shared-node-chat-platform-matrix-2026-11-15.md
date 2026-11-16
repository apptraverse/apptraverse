# SharedNode Chat platform matrix — 2026-11-15

Scope: the same `ChatSession`/SharedNode application through Windows, Linux,
Android, Web and a native headless command-line consumer. Debug was built and
run before the opt-in `APPTRAVERSE_MINIMUM_64=ON` MinSizeRel profile. The
transport BACKING was generated once by the Windows Debug
`apptraverse_chat_supply_exporter` and embedded in MinSizeRel builds; USER
state was kept in separate per-instance profiles. The pinned Æther SDK is
`0c2b8936b4418af55ed4cdb00405d99fc329422a`.

| Platform | Debug | MinSizeRel | Runtime evidence |
| --- | --- | --- | --- |
| Windows x64 | PASS | PASS | Native GUI real-cloud three-participant acceptance: Unicode/all-peer delivery, offline delivery, restart and matching USER. Native headless host sent and cold-restored its own message with the same UID in both configurations. `apptraverse_shared_chat_ui_model_test` and `apptraverse_connection_runtime_test` passed. |
| Linux x64, Ubuntu in WSL | PASS | PASS with GCC | Debug and MinSizeRel headless hosts sent and cold-restored their own messages with the same UID; both GTK builds opened X11 windows. Separate Windows host and Linux Debug headless client exchanged messages over the real Æther cloud, including a message sent while Linux was stopped and delivered once after its restart with the same UID. GUI interaction and Linux MinSizeRel cross-peer delivery were not proven in this WSLg run. |
| Android x86_64 emulator | PASS | PASS | Android 14 emulator: Debug and MinSizeRel native chat UI created separate host identities, sent messages with the Send button, and cold-restored the same UID and message. Release APK was signed with the standard debug key only for emulator installation. No physical Android device or ARM64 build was used. |
| Web, Chromium | PASS | PASS | Browser UI sent a message and recovered it after reload from IndexedDB in both configurations. COOP/COEP static test server enabled Emscripten pthreads. MinSizeRel uses wasm32. |
| macOS / iOS | NOT RUN | NOT RUN | This Windows machine has no Apple host, Xcode, `xcrun` or `swiftc`. Source targets exist, but no Apple build or runtime result is claimed. |

Artifact sizes below are exact bytes. Debug sizes include development code and
symbols; APKs include packaging, whereas the `.so` values are stripped native
libraries. The MinSizeRel APK signed with the standard debug key for emulator
installation was 3,261,089 bytes.

| Artifact | Debug bytes | MinSizeRel bytes |
| --- | ---: | ---: |
| Windows GUI `.exe` | 12,931,584 | 1,740,288 |
| Windows headless `.exe` | 11,443,200 | 1,503,232 |
| Android x86_64 APK (MinSizeRel unsigned) | 36,979,247 | 3,254,498 |
| Android x86_64 stripped `.so` | 18,477,168 | 3,236,648 |
| Web `.wasm` | 143,268,176 | 911,136 |
| Web `.js` | 247,022 | 101,304 |
| Linux GUI ELF | 194,329,472 | 1,448,328 |
| Linux headless ELF | 173,041,832 | 1,227,136 |

The minimum profile no longer enables MSVC `/fp:fast` or Clang/GCC
`-ffast-math`, `-fno-math-errno`, or `-fno-trapping-math`. Windows MinSizeRel
Ninja commands contained `/fp:precise` and no `/fp:fast`; Web and Linux
MinSizeRel command files contained none of the removed fast-math flags.

An experimental Web wasm64 MinSizeRel binary built but did not instantiate in
the installed Chromium or Chrome: `WebAssembly.instantiate(): cannot import
i32 memory as i64`. It is **not** the passing Web artifact. The working Web
MinSizeRel profile uses wasm32; native minimum builds remain x64.

The initial Linux MinSizeRel attempt with Ubuntu Clang failed while compiling
the pinned Æther SDK: its `std::vector` template-template parameter does not
match Clang's strict arity check in `event_multi_subscription.h`. A separate
GCC build produced the passing Linux minimum artifacts; no SDK source was
modified for this experiment. Its headless profile cold-restored UID
`0274e0b5-b644-4b8b-90d6-0f9d40bba8c0` and message
`LinuxMin: LinuxMin20261115` after a clean process restart.
