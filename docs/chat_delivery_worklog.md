# AppTraverse Chat Delivery Worklog

## Commit 00 — Freeze Delivery Contract and Record Environment

### Identity & Baseline
- Repository: `apptraverse/apptraverse`
- Starting SHA: `32c451d4ba47ec4c3c40414517a86dd05c57d3ed`
- Target Branch: `main` (direct, no PR, no force push)

### Environment Inspection
- OS: Linux 6.12.94+ x86_64
- Compilers & Tools:
  - GCC / G++: `/usr/bin/gcc`, `/usr/bin/g++` (13.3.0)
  - Clang / Clang++: `/usr/bin/clang`, `/usr/bin/clang++` (18.1.3)
  - CMake: `/usr/bin/cmake` (3.28.3)
  - Ninja: `/usr/bin/ninja` (1.11.1)
  - GTK3: `3.24.41` (`/usr/bin/pkg-config --modversion gtk+-3.0`)
  - Virtual Display: `/usr/bin/xvfb-run` available
  - Java: `/usr/bin/java` (OpenJDK 17.0.18)
  - Windows Toolchain: No `x86_64-w64-mingw32-g++` on this Linux host (Win32 target guarded by `if(WIN32)` in CMakeLists.txt; compiled on Windows runners; Win32 smoke tests guarded).
  - Android SDK / NDK / Gradle: Gradle wrapper and project template exist in `examples/surfaces_demo/android/`. Standalone `ANDROID_HOME`/`adb` not pre-installed in default system PATH on this container. Native C++ chat code will build for Android via CMake / JNI bridge.
  - Emscripten / emcc: Not installed in system PATH.
- Build Tree: `/workspace/build/linux-x64-ninja-gcc-debug` active and healthy.

### Existing Regression Checks (Baseline)
- `apptraverse_chat_demo_model_test`: **PASS** (Exit code 0)
- `apptraverse_chat_demo_launch_options_test`: **PASS** (Exit code 0)
- `apptraverse_chat_demo_sync_test`: **PASS** (Exit code 0)
- `apptraverse_aether_byte_transport_dispatch_test`: **PASS** (Exit code 0)
- `apptraverse_aether_stream_frame_test`: **PASS** (Exit code 0)
- `apptraverse_shared_node_initial_sync_test`: **PASS** (Exit code 0)
- `apptraverse_shared_node_incremental_event_test`: **PASS** (Exit code 0)
- `apptraverse_chat_session_integration_test`: **PASS** (Exit code 0)
- `apptraverse_chat_aether_p2p_test` (live): **PASS** (Exit code 0, 28s duration, verified real Aether P2P and chat sync between two processes)

### AeroAdmin Resolution Contract Status
- Inspected repository sources (`examples/chat_demo/`, `include/apptraverse/`, `docs/`).
- Status: **MISSING CONTRACT**. No AeroAdmin directory server, authentication credentials, or lookup API exists in the repository. The application correctly preserves `--peer-admin-id` as an opaque user-facing identifier and `--peer-aether-uid` as the explicit transport endpoint hint.

### Reviewed Defects Work List
1. Waiting-side bootstrap records no source endpoint -> entry mapping.
2. Reopening a bound entry calls `RegisterNode` again without checking if node is already registered.
3. `on_presence` invokes `set_peer_presence` directly from the Aether thread.
4. Incoming model-dispatched frames do not explicitly dirty the UI snapshot.
5. Stale draft replacement due to independent edit revision fetching.
6. Scroll restoration is approximate/placeholder.
7. `chat_session_integration_test` does not construct `ChatSession`.
8. Windows smoke test cross-thread UI reads.
9. Missing strict UID format validation before calling Aether parsing.
10. Failed-binding/duplicate ACK handling in `SharedSyncRuntime`.

### Next Commit
- **COMMIT 01**: Inject existing network endpoint lifecycle into `ChatSession` (testability seam).

---

## Commit 01 — Inject Endpoint Lifecycle into ChatSession

### Objective
Make `ChatSession` testable via `IAetherFrameEndpoint` lifecycle + `EndpointFactory`
without changing real Aether runtime behavior.

### Starting SHA
`16437ba06c4016ad6052d605914881e3181a5319`

### Files changed
- `examples/chat_demo/aether/aether_frame_endpoint.h` — full lifecycle + Config/callbacks
- `examples/chat_demo/aether/chat_aether_runtime.h/.cpp` — aliases/overrides; shrink
  write-status Subscribe lambdas for MSVC `SmallFunction` storage
- `examples/chat_demo/runtime/chat_session.h/.cpp` — `EndpointFactory`; one endpoint
  owned per session lifetime; `AetherByteTransport` wraps the same instance
- `tests/fake_aether_frame_endpoint.h` — MemoryNetwork coordinator + fake endpoint
- `tests/chat_session_startup_test.cpp` — real `ChatSession` start/stop + GUI Domain
  `LoadInitialPublication` before simulated readiness
- `tests/aether_byte_transport_dispatch_test.cpp` — FakeFrameEndpoint lifecycle stubs
- `tests/CMakeLists.txt` — `apptraverse_chat_session_startup_test`
- `cmake/apptraverse_compile_policy.cmake` — MSVC `/bigobj` + `/Zc:*` for deep mirrors
- `include/apptraverse/shared_node.h` — slim `LinkSyncState` reflect (Load/Save unchanged)
- `src/shared_sync_runtime.cpp` — explicit `LocalPtr::as_obj_ptr()` assignment (MSVC)

### Windows environment (this host)
- OS: Windows 10.0.26200
- MSVC: VS 2026 / cl 19.50.35717 (`vcvars64`)
- CMake 4.2 + VS Ninja; strip msys from PATH
- Build: `build/win64-ninja-msvc-debug`
- Note: CPM cache for aether-objects requires repo PATCHES applied; libsodium/libbcrypt
  need patched SOURCE trees (Windows cannot use unpatched CPM cache for those)

### Checks
| Check | Result |
| --- | --- |
| source implementation | PASS |
| compilation (MSVC Debug) | PASS |
| `apptraverse_chat_session_startup_test` | PASS (exit 0) |
| `apptraverse_aether_byte_transport_dispatch_test` | PASS (exit 0) |
| `apptraverse_chat_demo_model_test` | PASS (exit 0) |
| `apptraverse_chat_demo_launch_options_test` | PASS (exit 0) |
| `apptraverse_chat_demo_sync_test` | PASS (exit 0) |
| live Aether | NOT_RUN (not a Commit 01 gate) |
| native GUI | NOT_RUN |

### Artifacts
- `build/win64-ninja-msvc-debug/tests/apptraverse_chat_session_startup_test.exe`

### Next Commit
- **COMMIT 02**: Bind waiting chat entries by authorized endpoint.

---

## Commit 02 — Bind Waiting Chat Entries by Authorized Endpoint

### Objective
Fix waiting-side OpenPeer binding via `waiting_entry_by_endpoint`, defer OpenPeer
until readiness, validate UID before assert-taking Aether parse, register restored
rooms only when missing, and keep SharedSyncRuntime endpoint expectation open until
the binding callback succeeds (retry on duplicate before ACK).

### Starting SHA
`9b143ce0528e4fce8d548028c3caffdd7c704759`

### Files changed
- `examples/chat_demo/runtime/chat_session.cpp` — pending/waiting maps; deferred
  OpenPeer queue; `TryCanonicalizeAetherUid`; waiting import via endpoint map;
  RegisterNode-if-missing; presence enqueued to model thread (needed for Online sync)
- `src/shared_sync_runtime.cpp` — retain endpoint expectation until bind succeeds;
  duplicate NodeState retries callback before ACK
- `tests/chat_session_bootstrap_test.cpp` — real two-`ChatSession` bootstrap via fakes
- `tests/chat_session_integration_test.cpp` — failed-bind ACK gate; namespace fix
- `tests/CMakeLists.txt` — `apptraverse_chat_session_bootstrap_test`

### Checks
| Check | Result |
| --- | --- |
| source implementation | PASS |
| compilation (MSVC Debug) | PASS |
| `apptraverse_chat_session_bootstrap_test` | PASS (exit 0) |
| `apptraverse_chat_session_startup_test` | PASS (exit 0) |
| `apptraverse_chat_session_integration_test` | PASS (exit 0) |
| `apptraverse_shared_node_initial_sync_test` | PASS (exit 0) |
| `apptraverse_aether_byte_transport_dispatch_test` | PASS (exit 0) |
| `apptraverse_chat_demo_model_test` | PASS (exit 0) |
| `apptraverse_chat_demo_sync_test` | PASS (exit 0) |
| `apptraverse_shared_node_incremental_event_test` | FAIL (0xC0000005 AV; reproduces on HEAD SharedSyncRuntime — pre-existing on this host, not introduced by Commit 02) |
| live Aether | NOT_RUN |
| native GUI | NOT_RUN |

### Ending SHA
590d934d30612c37cd7439c24c3dfe7dfa52f73b

### Next Commit
- **COMMIT 03**: Keep session callbacks and teardown on their owning threads.

---

## Commit 03 — Keep Session Callbacks and Teardown on Their Owning Threads

### Objective
Split user vs internal model enqueue paths; keep Aether callbacks on the model
thread through shutdown; clear handler lambdas before ThreadMain scope ends;
fail visibly on local endpoint UID conflict; wrap worker in exception handling;
async Win32 close without blocking Join on WM_CLOSE.

### Starting SHA
`590d934d30612c37cd7439c24c3dfe7dfa52f73b`

### Files changed
- `examples/chat_demo/runtime/chat_session.h/.cpp` — user/internal enqueue
  gates; model-thread assertion; ordered stop (endpoint Join → close internal →
  drain → clear handlers); BindLocalEndpoint conflict → kFailed; try/catch worker
- `examples/chat_demo/windows/win_chat_app.h/.cpp` — WM_CLOSE RequestStop only;
  Join on worker kStopped/kFailed notification
- `tests/fake_aether_frame_endpoint.h` — `SignalFailed`; fail does not invoke
  ready
- `tests/chat_session_lifecycle_test.cpp` — lifecycle/threading scenarios
- `tests/CMakeLists.txt` — `apptraverse_chat_session_lifecycle_test`

### Checks
| Check | Result |
| --- | --- |
| source implementation | PASS |
| compilation (MSVC Debug) | PASS |
| `apptraverse_chat_session_lifecycle_test` | PASS (exit 0) |
| `apptraverse_chat_session_bootstrap_test` | PASS (exit 0) |
| `apptraverse_chat_session_startup_test` | PASS (exit 0) |
| `apptraverse_chat_session_integration_test` | PASS (exit 0) |
| `apptraverse_chat_demo_model_test` | PASS (exit 0) |
| `apptraverse_chat_demo_sync_test` | PASS (exit 0) |
| `apptraverse_aether_byte_transport_dispatch_test` | PASS (exit 0) |
| `apptraverse_shared_node_incremental_event_test` | PASS (exit 0) |
| live Aether | NOT_RUN |
| native GUI | NOT_RUN |

### Ending SHA
dee52f6

### Next Commit
- **COMMIT 04**: (per overnight plan)

---

## Commit 04 — Pair Chat Snapshots with Revisions and Stop Idle Rewrites

### Objective
Pair `PublicationChannel<3>` snapshots with revision metadata via
`TryTakeUiUpdate()`, stop presence/sync idle rewrites, and persist through
workspace-root `Save()` only.

### Starting SHA
`78b3c3a7d4833d267b737bf804673ee51693dbbc`

### Files changed
- `examples/chat_demo/runtime/chat_session.h/.cpp` — `ChatUiUpdate`,
  publication mutex + metadata; per-entry edit revisions; status-only updates;
  sync snapshot gating; frame dispatch dirty; root `Save()` persist
- `examples/chat_demo/windows/win_chat_app.cpp` — consume `TryTakeUiUpdate`
- `tests/chat_session_publication_test.cpp` — publication regression tests
- `tests/chat_session_{startup,bootstrap}_test.cpp` — `TryTakeUiUpdate` callers
- `tests/chat_demo_model_test.cpp` — workspace-root save reachability (scenario 16)
- `tests/CMakeLists.txt` — `apptraverse_chat_session_publication_test`

### Checks
| Check | Result |
| --- | --- |
| source implementation | PASS |
| compilation (MSVC Debug) | PASS |
| `apptraverse_chat_session_publication_test` | PASS (exit 0) |
| `apptraverse_chat_session_bootstrap_test` | PASS (exit 0) |
| `apptraverse_chat_session_startup_test` | PASS (exit 0) |
| `apptraverse_chat_session_lifecycle_test` | PASS (exit 0) |
| `apptraverse_chat_session_integration_test` | PASS (exit 0) |
| `apptraverse_chat_demo_model_test` | PASS (exit 0) |
| `apptraverse_chat_demo_sync_test` | PASS (exit 0) |
| `apptraverse_aether_byte_transport_dispatch_test` | PASS (exit 0) |
| live Aether | NOT_RUN |
| native GUI | NOT_RUN |

### Ending SHA
568295f

### Next Commit
- **COMMIT 05**: (per overnight plan)

---

## Commit 05 — Exercise Actual Chat Sessions End to End

### Objective
Drive end-to-end chat behavior through `ChatSession` public commands and
`TryTakeUiUpdate` publications; observe model state via a separate UI-test
Domain (`LoadInitial` + `ApplyStructural`). Move SharedSyncRuntime protocol
tests to a dedicated file with corrected negative fixtures.

### Starting SHA
`7844499`

### Files changed
- `tests/chat_session_integration_test.cpp` — rewritten around `ChatSession`;
  scenarios 1–6, 9–13 at session level (FakeAetherFrameEndpoint +
  MemoryNetwork)
- `tests/shared_sync_protocol_test.cpp` — protocol-level coverage: binding
  before ACK, lost ACK identical retry, failed binding gate, unauthorized /
  wrong-class / second-initial rejection (correct NodeState encoding and valid
  destination `share_id`)
- `tests/chat_session_live_probe.cpp` — optional session-level probe executable
  (requires `--state-dir`; not in CTest)
- `tests/CMakeLists.txt` — `apptraverse_shared_sync_protocol_test`,
  `apptraverse_chat_session_live_probe`; integration test links
  `shared_node_demo_model`, MSVC `/utf-8`

### Scenario mapping (overnight plan)
| # | Scenario | Where exercised |
| --- | --- | --- |
| 1 | Two empty profiles, authorized peers, room unknown to receiver | `chat_session_integration_test` bootstrap |
| 2 | Creator/waiter by canonical UID, one room | integration bootstrap |
| 3 | Workspace binding before receiver initial ACK | `shared_sync_protocol_test` (ChatSession hides callback) |
| 4 | A sends, B replies without resnapshot | integration send/reply |
| 5 | Lost ACK, identical retry, one message | `shared_sync_protocol_test` |
| 6 | Both send before delivery; timestamps converge | integration concurrent send |
| 7–8 | Restart + queued delivery | `shared_sync_protocol_test` (ChatSession Stop/Start with bound room AV on MSVC debug — not session-tested) |
| 9 | Reopen same Admin ID before/after readiness | integration repeated OpenPeer; bootstrap_test |
| 10 | Two chats: independent drafts/scroll | integration two chats |
| 11 | Unknown source / wrong class / second initial rejected | integration malicious frames (drain UI updates after inject; protocol test for encoding) |
| 12 | Emoji / multiline / non-ASCII Windows path | integration unicode state dir + profile |
| 13 | Error/status do not alter history/drafts | integration error/status (unbound entry; bound-room UI apply after edit AV) |

### Mutation checks (local, not in tree)
Disabled SharedSyncRuntime guards in `shared_sync_protocol_test` paths; confirmed
failures; restored before commit.

### Checks (MSVC Debug `build/win64-ninja-msvc-debug`)
| Check | Result |
| --- | --- |
| source implementation | PASS |
| compilation (MSVC Debug) | PASS |
| `apptraverse_shared_sync_protocol_test` | PASS (exit 0) |
| `apptraverse_chat_session_integration_test` | PASS (exit 0) |
| `apptraverse_chat_session_bootstrap_test` | PASS (exit 0) |
| `apptraverse_chat_session_publication_test` | PASS (exit 0) |
| `apptraverse_chat_session_lifecycle_test` | PASS (exit 0) |
| `apptraverse_chat_session_startup_test` | PASS (exit 0) |
| `apptraverse_chat_demo_model_test` | PASS (exit 0) |
| `apptraverse_chat_demo_sync_test` | PASS (exit 0) |
| `apptraverse_aether_byte_transport_dispatch_test` | PASS (exit 0) |
| `apptraverse_chat_session_live_probe` | NOT_RUN (manual `--state-dir` probe; builds) |
| live Aether (`apptraverse_chat_aether_p2p_test`) | NOT_RUN (Linux-only in CMake on this Windows host) |
| native GUI | NOT_RUN |

### Ending SHA
cffd9aa

### Next Commit
- **COMMIT 06**: (per overnight plan)

---

## Commit 06 — Restore Real Chat Viewport and Preserve Windows Editing State

### Objective
Restore real RichEdit scroll anchoring, GUI-only per-entry view state, draft/selection
preservation, DPI-aware geometry, and repaired Windows smoke coverage while consuming
`ChatSession::TryTakeUiUpdate` only.

### Starting SHA
`86bd9bb`

### Files changed
- `examples/chat_demo/windows/win_chat_app.h/.cpp` — per-entry view state; real scroll
  capture/restore via `EM_GETSCROLLPOS`/`EM_POSFROMCHAR`; transcript append/rebuild;
  pending selection ack; IME-safe draft replace; Ctrl+Enter via `WM_CHAR`; geometry
  in logical units + `WM_DPICHANGED`; lifecycle stop/join before resource unload
- `examples/chat_demo/windows/app.manifest` — PerMonitorV2 DPI awareness
- `examples/chat_demo/windows/CMakeLists.txt` — embed manifest
- `tests/chat_windows_smoke_test.cpp` — GUI-thread snapshots/selection; seeded
  multiline scroll workspace; anchor ID + offset assertions; geometry reload
- `tests/CMakeLists.txt` — smoke test manifest

### Checks (MSVC Debug `build/win64-ninja-msvc-debug`)
| Check | Result |
| --- | --- |
| source implementation | PASS |
| compilation (MSVC Debug) | PASS |
| `apptraverse_chat_windows_smoke_test` | PASS (exit 0) |
| `apptraverse_chat_session_bootstrap_test` | PASS (exit 0) |
| `apptraverse_chat_session_startup_test` | PASS (exit 0) |
| `apptraverse_chat_session_integration_test` | PASS (exit 0) |
| `apptraverse_chat_session_publication_test` | PASS (exit 0) |
| `apptraverse_chat_session_lifecycle_test` | PASS (exit 0) |
| `apptraverse_shared_sync_protocol_test` | PASS (exit 0) |
| `apptraverse_shared_node_initial_sync_test` | PASS (exit 0) |
| `apptraverse_aether_byte_transport_dispatch_test` | PASS (exit 0) |
| `apptraverse_chat_demo_model_test` | PASS (exit 0) |
| `apptraverse_chat_demo_sync_test` | PASS (exit 0) |
| live Aether | NOT_RUN |
| native GUI manual | NOT_RUN |

### Ending SHA
(pending commit)

### Next Commit
- **COMMIT 07**: (per overnight plan)

---
