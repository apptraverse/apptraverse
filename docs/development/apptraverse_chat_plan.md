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
- other platform adapters come after runner behavior is stable

One repo-owned runner will own:

- environment preflight
- canonical profile selection
- configure when necessary
- incremental target build
- test invocation
- timeouts
- process termination
- artifact locations

## ACT-S022

- JSON result/artifact/timeout contract
- status: blocked by S021

The runner returns a compact machine-readable result. Full stdout/stderr goes
to artifacts. Cursor should normally receive only:

- status
- stage
- profile
- target
- duration
- failure_kind
- first_error
- artifact_id

Absolute local paths should not be returned unless required for a specific
diagnostic.

## ACT-S023

- thin MCP wrapper over canonical runner
- status: blocked by S022

MCP wraps the runner. MCP must not independently implement build logic.

Planned operations:

- `build_preflight(profile)`
- `build_start(profile, targets)`
- `build_status(job_id)`
- `build_cancel(job_id)`
- `test_run(profile, filter)`
- `artifact_failure_read(artifact_id, index)`

Long builds must run as background jobs. MCP output must remain bounded.

## ACT-S030

- read-only architecture audit
- status: blocked by tooling baseline

## ACT-S040

- transport simplification slices
- status: blocked by audit and user decisions

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
