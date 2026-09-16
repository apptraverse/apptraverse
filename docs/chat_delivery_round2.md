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
