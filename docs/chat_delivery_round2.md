# Chat delivery round 2 worklog

Starting SHA: `ade6445d12eb6dbf262b00e4bd02301fb3b7294d` (also `origin/main`).
`ade6445` is an ancestor of current HEAD (identical at R00 start).
Working tree at inspect: untracked `tools/build_commit01_msvc.bat`, `tools/build_commit02_msvc.bat` only (not committed).

## R00 — Current-source gates and environment

### Overnight observations treated as unproven until reproduced

| Report from `docs/chat_delivery_worklog.md` | Treatment |
| --- | --- |
| Bound-room ChatSession restart / GUI-mirror AV on MSVC (Commit 05) | Must reproduce with CURRENT sources; not mapped to SharedSyncRuntime-only tests |
| `object_link.h` vs aether-objects after reconfigure | Reproduced as include ownership: `_deps/aether-objects-src` lacked `GraphSerializationScope`; leftover `_deps/aether-objects-patched` had it. Case **A/C**: owned patch not on the SOURCE_DIR CMake compiles. |
| Final overnight checks used pre-existing binaries | Historical only. Label old EXE under `build/win64-ninja-msvc-debug` as stale. |
| Linux/Android not compiled on that host | Android SDK **is** present now (`C:\Users\nickc\AppData\Local\Android\Sdk`, NDK `29.0.14206865`). WSL Ubuntu exists (Stopped). |
| No Windows real-Aether two-process test | Still unproven until R09 |

### Commit 02 regression properties (SOURCE_REVIEWED on current `chat_session.cpp`)

| Property | Status in current source |
| --- | --- |
| `waiting_entry_by_endpoint` used before import | Present (import callback looks up map before BindChat) |
| Restored rooms not blindly RegisterNode twice | `FindNode` then RegisterNode |
| UID validated before assert-taking parser | `TryCanonicalizeAetherUid` |
| Presence posted to model queue | `EnqueueInternalModelWork` |
| Publication metadata per-entry edit revisions | `ChatUiUpdate.processed_edit_revisions_by_entry` |
| Root Save | `workspace.Save()` in persist_workspace |

Do not reimplement these.

### Behavior → actual test / binary map

| Required behavior | Actual test | Binary (after current-source build) |
| --- | --- | --- |
| Fake two-session bootstrap / bind | `apptraverse_chat_session_integration_test`, `apptraverse_chat_session_bootstrap_test` | tests/*.exe from **current** tree |
| Protocol ACK / wrong class | `apptraverse_shared_sync_protocol_test` | protocol EXE — **not** ChatSession restart |
| **REAL ChatSession bound-room restart** | **GAP** — integration comments 7–8 defer to protocol test; this row is **untested** until R03 | n/a |
| Publication revision pairing | `apptraverse_chat_session_publication_test` | |
| Session lifecycle | `apptraverse_chat_session_lifecycle_test` | |
| Win32 GUI smoke | `apptraverse_chat_windows_smoke_test` | GUI_PASS only if current EXE |
| Live Aether two-process | `apptraverse_chat_session_live_probe` / POSIX p2p | LIVE **NOT_RUN** until R09 |
| Model-only | `apptraverse_chat_demo_model_test` with `APPTRAVERSE_BUILD_AETHER_DEMOS=OFF` | |

### Tool inventory (this Windows host)

| Tool | Location |
| --- | --- |
| VS | Community 2026 `C:\Program Files\Microsoft Visual Studio\18\Community` (18.0.11205.157) via vswhere |
| vcvars | `...\VC\Auxiliary\Build\vcvars64.bat` |
| CMake | `C:\Program Files\CMake\bin\cmake.exe` 4.2.0 |
| Ninja (intended) | VS `...\CMake\Ninja\ninja.exe` (PATH also has msys ninja — must not use) |
| Python | 3.11.9 |
| Debugger | VS 2026 |
| Android SDK | `C:\Users\nickc\AppData\Local\Android\Sdk` (`ANDROID_HOME` unset; conventional path exists) |
| NDK | `29.0.14206865` |
| adb | `...\platform-tools\adb.exe` |
| WSL | Ubuntu 2 Stopped; docker-desktop Stopped |
| Emscripten | not on PATH |

Pins in `cmake/aether_version.cmake`: aether-client-cpp `0b0e3b54…`, aether-objects `1d302647…`, aether-miscpp `f8b2e1c6…`.

Current-source build command:

```
powershell -File tools/build_chat_demo.ps1 -BuildDir build/chat-r2-msvc-debug -Configure
```

Old `build/win64-ninja-msvc-debug` binaries: **historical**, not release evidence.

## R01 — Reproducible MSVC build from pinned sources

### object_link.h / GraphSerializationScope diagnose

First configure of `build/chat-r2-msvc-debug` failed compiling against unpatched
`_deps/aether-objects-src` (missing `DomainGraph::serialization_scope`).
Case **A/C**: owned patch not applied to the SOURCE_DIR CMake compiles (CPM
PATCHES skipped on cache; leftover `-patched` tree not used). Not case D.

Fix: `apptraverse_ensure_aether_objects_graph_scope()` after CPMAddPackage —
pristine → apply owned patch; already-applied → no-op; partial → FATAL.

### Build script

`tools/build_chat_demo.ps1`: vswhere + vcvars, VS Ninja (strip msys), quoted
paths, LASTEXITCODE abort, optional `CPM_libsodium_SOURCE` /
`CPM_libbcrypt_SOURCE` with forward slashes, `CMAKE_POLICY_VERSION_MINIMUM=3.5`,
`-BuildDir` / `-AetherDemos`.

### Gates

| Check | Result |
| --- | --- |
| Configure+build #1 `build/chat-r2-msvc-debug` | exit 0; EXEs present |
| Configure+build #2 same dir | exit 0; `SCOPE_PATCH=already-applied`; ninja no work |
| Dep SHA log | objects `1d302647…` TREE=dirty (owned patch); client `0b0e3b54…` |
| `apptraverse_chat_demo_model_test` | UNIT_PASS exit 0 |
| `apptraverse_shared_sync_protocol_test` | UNIT_PASS exit 0 |
| `APPTRAVERSE_BUILD_AETHER_DEMOS=OFF` separate tree | NOT fully verified this commit (wrong target name earlier; re-run pending) |

## Next

R02 build-info/receipts; R03 bound-room restart AV; continue R04+.

---

# AUTO_CONTINUATION (from `f4b232c`)

## A00 — Reconcile (2026-09-16)

| Item | Value |
| --- | --- |
| Repo | `C:\Users\nickc\Projects\apptraverse-chat-delivery` → remote `apptraverse/apptraverse` |
| Branch | `main` |
| Local HEAD | `f4b232c79f480f31de39a67f4d478f4f3cd98ed9` |
| origin/main | identical |
| Worktree | only unrelated `tools/build_commit0{1,2}_msvc.bat` untracked |
| `build/chat-r2-msvc-debug` | retained; cache still had foreign `CPM_libsodium_SOURCE` → surfaces-integration (defect for A01) |
| Android SDK | present `%LOCALAPPDATA%\Android\Sdk` |
| WSL Ubuntu | Stopped (may start later for A18) |
| Python | 3.11/3.13/3.14 on PATH |

Source-confirmed defects still present in tree: status_serial mutex split,
RetryConnection Join, Android `R.id/aether_uid` + `System.exit`, Checkpoint→SaveBounds.

A01 in progress: remove foreign SOURCE auto-fill; vcvars env import (no ASCII .cmd
path interpolation); cache mismatch checks; `APPTRAVERSE_CMAKE_DIR` patch paths;
new owned dir `build/chat-a01-msvc-debug` without overrides.

## A01 — Portable build (completed)

### Changes
- `tools/build_chat_demo.ps1`: no foreign checkout auto-fill; explicit
  `-LibsodiumSource`/`-LibbcryptSource` only; vcvars imported into process env
  (batch contains only VS path); CMake/Ninja via argument arrays; cache mismatch
  for Configuration/AetherDemos; `-NoAetherDemos` switch; comma Targets expand.
- `cmake/aether_version.cmake`: `APPTRAVERSE_CMAKE_DIR` /
  `APPTRAVERSE_AETHER_OBJECTS_SCOPE_PATCH`; ensure uses
  `CMAKE_CURRENT_FUNCTION_LIST_DIR`; reverse+forward `git apply --check`.

### Evidence

| Check | Result |
| --- | --- |
| `build/chat-a01-msvc-debug` configure without CPM_*_SOURCE | exit 0; SCOPE_PATCH=applied |
| Second `-Configure` same dir | exit 0; SCOPE_PATCH=already-applied |
| model + protocol tests | UNIT_PASS exit 0 |
| `build/chat-a01-model-only -NoAetherDemos` | BUILD_PASS exit 0; cache DEMOs=OFF |
| Unicode+space `build/чат demo a01` model-only | BUILD_PASS exit 0 |
| Foreign sodium path | absent from A01 CMakeCache |

## Next

A02 build-info/receipts; A03 bound restart reproduce.

## A02 — Binary identity + sodium ensure (completed)

### Changes
- Generated `chat_build_info_generated.h` via `cmake/generate_chat_build_info.cmake`
- `chat_demo_build_info` + `--build-info` / `--build-info-file` on Windows chat and live probe
- Build receipts under `build/.../receipts/*.json` after successful link
- `tools/test_chat_build_receipt.py`; packager refuses unlabeled/mismatched/dirty EXE
- `cmake/aether_object.cmake`: DOWNLOAD_ONLY + ensure-apply for libsodium/libbcrypt
  CMakeLists patches when CPM PATCHES skipped on cache (reconfigure regression)

### Evidence
| Check | Result |
| --- | --- |
| `apptraverse_chat --build-info-file` | exit 0; identity lines present |
| live probe `--build-info` | exit 0 |
| receipt sha256 vs EXE | match |
| `tools/test_chat_build_receipt.py` | exit 0 |
| second configure keeps sodium CMakeLists | `LIBSODIUM_CMAKE=present` |

Dirty tree embeds `source_dirty=dirty` (expected until this commit lands).

## Next

A03 bound ChatSession restart reproduce; A04 ownership; A05–A08 session fixes.

## A06/A07/A08 slice + host completion (completed)

### Changes
- `TryTakeUiUpdate`: status_serial under `status_mu_` only; publication slot separate
- `GetRuntimeStatus() const`; `stop_requested_` / `finished_` atomics; `IsFinished()`
- `RetryConnection()` enqueues only (no caller-thread Join/Start); removed last_config restart
- `Checkpoint(request_id)` enqueues root Save; advances `completed_checkpoint_id`
- Model loop no longer waits on `publication_cv_` while GUI is busy
- Win32/Linux close waits `IsFinished` before Join
- Android: `R.id.aether_uid`, remove `System.exit`, Checkpoint uses real API

### Evidence
| Test | Result |
| --- | --- |
| integration | UNIT_PASS exit 0 |
| fault (incl. Checkpoint 42) | UNIT_PASS exit 0 |
| windows smoke | GUI_PASS exit 0 |

### Remaining
- A05 WorkerState lifetime (exception drain still after try-scope locals)
- A07 full network_epoch endpoint reincarnation
- A03/A04 bound-room restart still GAP
- Android namespace rename + full UI (A15–A17)

## Next

A03 bound restart reproducers; A05 WorkerState.

## A03/A04 — Bound restart + canonical UI ownership (completed)

### Root cause (TEST_PASS evidence)
`BootstrapPair` returned by value moved `UiMirror` whose `RamDomainStorage`
was an embedded member. `ae::Domain` retains `IDomainStorage*`; after the move
`GetReader`/`Load` hit a dangling storage pointer → AV `0xC0000005` in
`DomainGraph::GetReader` during `ApplyStructuralPublication`.
`TestSendReplyWithoutResnapshot` kept `UiMirror` on the stack (no move) and
passed the same apply path.

### Changes
- UI-test mirrors: heap-allocate `RamDomainStorage`; hold workspace via
  `Domain::Find` (not `MakeFromThis`); structural apply through
  `ApplyStructuralPublicationAndUpdatePresenters` with GUI root keepalive.
- Hosts (Win/Linux/Android): already used Find+keepalive; confirmed.
- `TestBoundRoomFullRestart`: full destroy of sessions+UI Domains, reverse
  reload order, checkpoint before stop, continue messaging after reopen.

### Evidence (`build/chat-a01-msvc-debug`, dirty tree on `ef8cf8c`)
| Test | Result |
| --- | --- |
| `apptraverse_chat_session_integration_test` (incl. bound restart) | TEST_PASS exit 0 |
| `apptraverse_chat_session_fault_test` | TEST_PASS exit 0 |
| `apptraverse_chat_session_bootstrap_test` | TEST_PASS exit 0 |
| `apptraverse_chat_session_publication_test` | TEST_PASS exit 0 |
| `apptraverse_chat_session_command_limits_test` | TEST_PASS exit 0 |
| `apptraverse_chat_session_startup_test` | TEST_PASS exit 0 |
| `apptraverse_shared_sync_protocol_test` | TEST_PASS exit 0 |

Historical AV: REPRODUCED then FIXED (dangling Domain storage after UiMirror move).

## Next

A05 WorkerState; A07 network_epoch Retry; A08 command results; A09+ gates.
