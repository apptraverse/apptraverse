Status: active
Role: architecture, sequencing and acceptance for App Traverse Chat
Progress: apptraverse_chat_progress.md
Iterate prompt: apptraverse_chat_iterate_prompt.md
Stop signal: APPTRAVERSE_CHAT_BASELINE_COMPLETE

# Mission

Create a reusable Chat component that can be embedded in:

- AeroAdmin Windows
- AeroAdmin Linux
- AeroAdmin macOS
- AeroAdmin Android
- AeroAdmin iOS
- standalone demo applications

The near-term product goal is architecture validation, not product polish:
build the same minimal chat functionality that already exists in the current
Windows and Android demo applications on all five current-baseline platforms.
Do not add product features. The five-platform work exists to:

- expose bad platform assumptions
- simplify ChatComponent / host / transport boundaries
- validate persistence and synchronization architecture
- establish a clean reusable foundation before integration into aeroadmin-x

# Architecture boundaries

## ChatComponent

ChatComponent:

- owns chat commands
- owns presentation snapshot
- owns ChatSyncController
- does not create Window/Activity/View
- does not own AetherApp
- does not own the application event loop
- does not create a platform thread

## Application host

Application host:

- owns AetherApp
- owns P2pStream transport
- owns Window/Activity/SwiftUI host
- forwards transport callbacks
- manages foreground/background policy

## ChatTransport (target conceptual boundary)

ChatComponent must depend on a transport-neutral boundary. Æther is one
transport implementation, not the permanent ChatComponent API.

Target `ChatTransport`:

- connect(remote endpoint)
- send(remote endpoint, bytes)
- receive callback
- optional connection/status callback

Transport must not own:

- Chat events
- SyncPacket lifecycle
- application ACK
- persistence
- UI
- delivery semantics above raw byte transport

Future adapters to keep in mind:

- Aether transport
- in-memory transport for tests
- console/headless transport host
- Emscripten/web transport
- future AeroAdmin/offline-service transport

Audit item: current use of `ae::Uid` in reusable chat APIs may leak Æther
identity into the generic component and must be reviewed before architecture
freeze. Do not change the C++ API until a slice owns that review.

## AetherP2pTransport (current implementation)

Current Æther adapter surface:

- Connect(peer)
- Send(peer, bytes)
- Receive(peer, bytes)
- optional stream-state notification

It must not own:

- SyncPacket lifecycle
- application ACK
- packet retry
- Chat pending state
- payload dedupe
- persisted model

# Current baselines

## Architectural baseline

`review/chat-component-v3`:
`4786ac6bf7e384c530a1f6994c4f3e91ce35bb04`

Treat v3 as the current clean architectural baseline.

## Experimental branches

The following branches are sources of diagnostics and evidence. They must not be merged automatically:

- `review/chat-component-v4`
- `review/chat-component-v5`

Separately: v5 contains packet-aware transport diagnostics and SyncWriteGate that require a separate architectural decision.

## Cross-platform product target

User-approved current baseline (x86_64 only):

1. Windows x86_64
2. Linux x86_64
3. Android x86_64 emulator
4. macOS x86_64
5. iOS x86_64 Simulator

ARM64 is out of the current baseline. Do not add Android arm64-v8a or Apple
arm64 acceptance/build requirements yet. The reason is not lack of expected
Æther ARM support. ARM builds are outside the current architecture-validation
baseline.

# UI host strategy

## Windows

- existing Win32 host
- no new Windows UI framework

## Android

- existing Android UI/JNI/native runtime
- x86_64 emulator only for the current baseline

## Linux

- GTK4
- GTK4 is a Linux host implementation, not the common cross-platform UI layer
- target ordinary modern Linux desktop environments
- packaging/distribution compatibility is a later concern
- do not introduce Qt or another cross-platform GUI abstraction

## Apple

Maximize macOS/iOS code sharing.

Target architecture:

```
shared SwiftUI presentation code
        ↓
shared Apple bridge API
        ↓
Objective-C++ implementation
        ↓
C++ ChatComponent / runtime
```

macOS and iOS should share as much SwiftUI/view-model/bridge code as practical.
Platform-specific code should be limited to actual lifecycle/platform
differences.

Future Æther Apple networking may use native Apple transports such as
NSURLSession / Network.framework behind the same transport boundary.
Do not implement Apple networking until a slice owns it.

# Minimal five-platform feature parity

The five apps need only the functionality represented by the current
Windows/Android chat baseline:

- create/load persistent local chat state
- show current transcript including Join entries
- show local Æther UID
- add remote peer by UID
- text input
- Send
- synchronize chat state/history through the existing Æther transport
- recover persisted chat state after application restart

Do not add:

- attachments
- address book
- file transfer
- voice
- notifications
- group-management UI beyond what the current model requires
- background push
- AeroAdmin branding
- settings UI
- a common cross-platform widget framework

# Additional validation hosts

These are not extra GUI platforms. The product baseline remains the five GUI
platforms above.

## Console application

- canonical minimal headless host
- useful for interoperability, persistence and transport tests
- should compile on desktop platforms
- not implemented in this slice

## Emscripten

- future architecture-validation target
- must expose blocking/thread/filesystem/platform assumptions
- transport may later use WebSocket/WebTransport/HTTP
- no browser UI or Emscripten build in this slice

# Canonical build environment

Ninja is preferred for fast incremental C++ builds. Ninja is not mandatory.

Windows requires MSVC. `Visual Studio 17 2022` is the canonical explicit
fallback generator. Backend selection is by profile name, never by implicit
fallback inside one build directory.

Checked-in desktop CMake presets (`CMakePresets.json`):

- `win64-ninja-msvc-debug` — Ninja, MSVC `cl`, `build/win64-ninja-msvc-debug`
- `win64-vs2022-msvc-debug` — Visual Studio 17 2022, x64, `build/win64-vs2022-msvc-debug`, build configuration Debug
- `linux-x64-ninja-clang-debug` — Ninja, `clang++`, `build/linux-x64-ninja-clang-debug`
- `macos-x64-ninja-appleclang-debug` — Ninja, `clang++`, `CMAKE_OSX_ARCHITECTURES=x86_64`, `build/macos-x64-ninja-appleclang-debug`

No Release profiles yet. No ARM64 canonical profiles.

Windows must not fall back to MinGW/GCC. The Ninja Windows profile still
requires `cl.exe` on PATH. The Visual Studio Windows profile does not require
Ninja or `cl` on PATH; CMake/VS locates the toolchain.

Canonical orchestration entry point:

`tools/runners/run_apptraverse_build.py`

Cursor must not construct long PowerShell/bash build command sequences. Python
may invoke cmake, the selected build backend, ctest, Gradle and xcodebuild.
PowerShell/bash is reserved for cases that cannot reasonably be expressed in
the shared Python runner. `shell=True` is forbidden unless strictly necessary.

# C++ testing policy

- new C++ unit/component tests should use GoogleTest where practical
- CTest remains the umbrella test runner
- existing custom CHECK executables are not migrated in this slice
- Python runner tests continue using stdlib unittest
- do not add GoogleTest or rewrite C++ tests until a slice owns that work

## Android / iOS profile policy

Do not invent fake top-level CMake presets for the final Android/iOS apps.

Android x86_64: the canonical application entry point remains the Gradle
wrapper. The runner will invoke the existing Android build while ensuring the
native externalNativeBuild/CMake path uses Ninja and ABI=`x86_64`. No
arm64-v8a in the current baseline.

iOS: the final application build uses xcodebuild. Shared C++ remains
platform-neutral. Do not replace the Apple application toolchain with Ninja.

Where CMake owns a direct C++ build, use a checked-in preset. Android/iOS
application toolchains remain Gradle and xcodebuild.

Forbid:

- implicit generator fallback inside one profile or one build directory
- MinGW for Windows production profiles
- accidental compiler selection from PATH
- random / ad-hoc build directory names
- switching compiler or generator inside an existing build directory
- ARM64 profiles in the current baseline

# Build policy

Incremental build is the default.

Never run clean or rebuild merely to verify a normal source change.
Never delete a valid canonical build directory between ordinary slices.

Use:

```
cmake --build <canonical-dir> --target <small-target>
```

for normal iteration once configured.

Configure only when:

- the canonical build directory does not exist
- CMake configuration inputs changed
- the selected slice explicitly requires configure

Clean/rebuild only when:

- explicitly requested by the user
- the selected slice specifically owns clean-build verification
- a canonical release gate requires it

A failure does not automatically authorize:

- clean
- rebuild
- a new build directory
- another compiler
- another generator

One retry maximum.

If a build directory was configured with the wrong compiler/generator, return
typed blocker `build_profile_conflict`. Do not create `build2`, `build-final`,
`build-new`, `build-msvc2`, or similar.

Additional rules:

- narrow target first
- full release gate only as a separate slice
- missing tool/SDK is returned as a typed blocker
- Cursor does not install the toolchain automatically
- a documentation/configuration-only slice performs no configure, build, or tests
- every external configure/build command has a 15-minute hard wall timeout (`command_timeout`)

# Planned phases and slices

Minimal backlog:

## ACT-S001

- initial Windows presentation replay
- status: blocked until the tooling baseline is usable
- reason: previous local build environment/worktree failure
- expected change: two-line state replay after native controls creation

## ACT-S010

- canonical plan/progress/iterate trio
- status: done

## ACT-S020

- checked-in Ninja CMake presets and cross-platform target
- status: done in this session

## ACT-S021

- canonical staged build runner
- Windows entry point: `tools/runners/run_apptraverse_build.py`
- supported profiles: `win64-ninja-msvc-debug`, `win64-vs2022-msvc-debug`
- stages: `preflight`, `configure`, `build`
- Ninja preferred; Visual Studio 2022 is an explicit second profile
- do not initially implement Linux/macOS/Android/iOS adapters
- status: done at the tooling level
- product compile success is not required to prove runner orchestration

One repo-owned runner will own:

- environment preflight
- canonical profile selection
- configure when necessary
- incremental target build
- test invocation
- timeouts
- process termination
- artifact locations

## ACT-S022A

- bounded synchronous build artifacts and compact JSON results
- status: in progress in this session
- `--json` emits one compact object; full child logs stay in `.artifacts/apptraverse-build/`
- status: done

## ACT-S022B

- background build jobs with start/status/cancel
- status: done
- schema `apptraverse.build_job/1`
- artifacts `.artifacts/apptraverse-jobs/<job-id>/`, public `artifact_id` `apptraverse-jobs/<job-id>`
- operations: start, status, cancel, `_worker`
- job states: starting, running, completed, cancelled, failed, not_found
- product `compile_failed` is job state `completed`
- must not implement MCP

## ACT-S022

Historical name for the result/artifact/timeout contract. Split into S022A (this slice) and S022B (background jobs). Compact public fields:

- status
- stage
- profile
- targets
- duration_ms
- failure_kind
- first_error
- artifact_id

Absolute local paths are not returned in the public result.

## ACT-S023

- thin MCP wrapper over canonical runner and background job controller
- status: done at the tooling level
- files: `tools/mcp/apptraverse_mcp.py`, `tools/mcp/setup_apptraverse_mcp.py`
- SDK pin: `mcp==2.0.0`
- stdio server only; repo root from `__file__`, never cwd
- canonical setup: User-level `~/.cursor/mcp.json` via `python tools/mcp/setup_apptraverse_mcp.py`
- `.cursor/mcp.json.example` is documentation only; project-local `.cursor/mcp.json` is not canonical (ACT-T001)

MCP wraps `start_job` / `status_job` / `cancel_job`. MCP must not independently implement build logic.

Worktree ownership (ACT-T002):

- MCP server process binds to checkout path in User MCP config
- build artifacts/directories belong to that checkout
- another worktree does not reuse configured CMake directories from a different checkout
- canonical MCP checkout should be the primary Windows development worktree until explicit repo/worktree routing exists

Tools:

- `apptraverse_build_start(profile, stage, targets)`
- `apptraverse_build_status(job_id)`
- `apptraverse_build_cancel(job_id)`
- `apptraverse_build_failure_excerpt(artifact_id)` — bounded `apptraverse-build/<run-id>` excerpt only

Long builds must run as background jobs. MCP output must remain bounded.

## ACT-S024

- live Cursor MCP proof of the S023 wrapper
- status: done
- goal: prove Cursor MCP tool → background job → compact status → bounded failure excerpt → no terminal/build-log pollution
- do not add tools or build logic
- do not fix ACT-B001

Evidence: server identifier `user-apptraverse`; preflight completed status=ok; background start/status/excerpt without terminal or full logs. Tooling findings ACT-T001 (project MCP/worktree routing) and ACT-T002 (worktree-local build directories).

## ACT-S024R

- canonical User-level Cursor MCP setup for worktrees
- status: done after S024
- `setup_apptraverse_mcp.py` registers/updates `apptraverse` in User `~/.cursor/mcp.json` only
- preserves unrelated User MCP entries; idempotent; no project-local `.cursor/mcp.json`
- documents worktree ownership: MCP and build directories bind to the checkout path in User config
- do not implement multi-worktree routing, arbitrary repo paths, or build-directory symlinks
- build stage on unconfigured worktree may fail (valid); configure remains explicit; no implicit configure fallback

## ACT-S025

- structured runtime JSONL logging
- status: done
- schema `apptraverse.runtime_event/1`; one JSON object per line
- writer under `examples/single_client_chat/common/runtime_jsonl.{h,cpp}`
- enablement via `APPTRAVERSE_RUNTIME_JSONL`, `APPTRAVERSE_RUN_ID`, `APPTRAVERSE_INSTANCE`
- artifact convention `.artifacts/apptraverse-runtime/<run-id>/<instance>.jsonl`
- parser `tools/runtime/runtime_jsonl.py`; MCP `apptraverse_runtime_log_query` (max 100 records)
- Windows host events only: `runtime_started`, `peer_add`, `text_submit`, `presentation`, `message_visible`, `runtime_stopped`
- do not instrument ChatComponent internals; human logs remain

## ACT-S026

Split after S025:

## ACT-S026A

- two-Windows Python process harness and real JSONL proof
- repair slice ACT-S026A-R1: harness owns completion; do not use `--exit-after-message`
- delivery gate: both sides `message_visible` for the remote text plus local `text_submit`
- `presentation.last_entry_*` is insufficient for remote delivery
- runner: `tools/integration/run_two_windows_chat.py`
- MCP: `apptraverse_two_windows_chat_run` (sixth tool)
- assertions use `apptraverse.runtime_event/1` only
- scenario `two_windows_bidirectional_chat`
- artifacts `.artifacts/apptraverse-integration/<run-id>/`

## ACT-S026B

- GoogleTest/CTest wrapper over the proven Windows scenario
- status: blocked until S026A

## ACT-S026C

- Windows ↔ Android x86_64 emulator orchestration
- status: blocked until S026B

## ACT-S027

- debugger inspection adapter
- Windows first; breakpoints and bounded variable/memory reads
- do not name or require a specific debugger product until the exact tool/API is identified

## ACT-S100A

- prepare canonical Windows chat GUI for manual validation
- status: done
- restore explicit initial presentation replay after CreateNativeWindow()
- ordinary no-argument launch uses Run(...); not --print-aether-uid
- mutual AddPeer is the current expected peer-authorization behavior
- ACT-B002 does not block this slice

## ACT-S100A-Q

- quiet-by-default Windows human diagnostics
- status: awaiting_manual_validation
- opt-in APPTRAVERSE_VERBOSE_LOG=1
- do not change JSONL, sync, transport, or Aether

## ACT-S100B

- canonical Android x86_64 emulator GUI
- status: not ready until ACT-S100A-Q manual PASS

## ACT-S030

- read-only architecture audit
- status: blocked by tooling baseline

## ACT-S040

- transport simplification slices
- status: blocked by audit and user decisions

# Product blockers

## ACT-B001

- kind: `transitive_dependency_drift`
- status: done
- strategy A: App Traverse pins aether-miscpp to `eabf068d369ec98e4d541ea229f1c8401e186b66` before fetching pinned Æther `7294f92a0cf749c5d56eedc28673d8089d1f5cb2`
- that miscpp revision still ships `aether-miscpp/reflect/domain_visitor.h`; later `main` moved it
- do not treat historical C1083 as a runner failure
- do not float `main` for aether-miscpp while Æther remains at `7294f92a`


## ACT-B002

- kind: print_uid_teardown_heap_corruption
- status: deferred
- special --print-aether-uid mode emits a valid UID; process later exits 0xC0000005
- debug heap reported a modified free block; first corrupting write was not captured
- Full Page Heap requires elevated GFlags and was not enabled
- automation-only early-exit path; does not currently block ordinary GUI validation
- do not mark fixed

# Acceptance IDs

Registry:

## ACT-A001

plan/progress/iterate trio exists and is internally linked

## ACT-A002

exactly one ready slice can be identified mechanically from progress

## ACT-A003

Ninja-preferred Windows MSVC policy is documented, with explicit VS 2022 fallback profile

## ACT-A004

canonical runner supports bounded staged execution

## ACT-A005

MCP returns compact structured results and artifact references
status: done (ACT-S023 tooling; ACT-S024 live Cursor MCP workflow proof)

# Tooling findings

## ACT-T001

Cursor project-scoped MCP server was discoverable but not invokable from a worktree because its project-prefixed server identifier could not be resolved. User-level registration is canonical.

## ACT-T002

CMake build directories are worktree-local. A new worktree cannot reuse another worktree's configured build directory. Build on an unconfigured worktree may fail until configure runs explicitly.

## ACT-A006

Windows/Android chat functional baseline passes

## ACT-A007

transport/component architecture approved after read-only audit

# Definition of Done

Baseline complete only when:

- reusable component boundary approved
- Ninja profiles work
- staged runner works
- MCP wrapper works
- Windows and Android builds pass
- initial render, pairing, pre-pair merge, restart and Wi-Fi recovery pass
- architecture audit actions resolved
- stop signal recorded in progress: `APPTRAVERSE_CHAT_BASELINE_COMPLETE`
