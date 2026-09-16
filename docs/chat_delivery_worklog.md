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
