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

## AetherP2pTransport target boundary

AetherP2pTransport target boundary:

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

# Development methodology

- exactly one ready slice per Cursor session
- one slice must correspond to one coherent commit
- no adjacent cleanup
- no future features
- progress is updated in the same session
- after one slice Cursor stops
- a blocked slice is not fixed by a workaround
- tooling repair is a separate slice

# Canonical build environment

Ninja is the only C++ generator.

Planned profiles:

- `win64-ninja-msvc-debug`
- `linux-ninja-clang-debug`
- `android-x86_64-ninja-debug`
- `android-arm64-v8a-ninja-debug`

The Android application is launched via the Gradle wrapper, but the native CMake build uses Ninja.

Forbid:

- Visual Studio CMake generator
- MinGW for the Windows production profile
- accidental compiler selection from PATH
- random build directory names
- switching compiler inside an existing build directory

# Build policy

- incremental build by default
- do not clean/rebuild without a direct requirement of the selected slice
- configure only when the profile build directory is missing or CMake inputs changed
- narrow target first
- full release gate only as a separate slice
- missing tool/SDK is returned as a typed blocker
- Cursor does not install the toolchain automatically

# Planned phases and slices

Minimal backlog:

## ACT-S001

- initial Windows presentation replay
- status: blocked
- reason: previous local build environment/worktree failure
- expected change: two-line state replay after native controls creation

## ACT-S010

- canonical plan/progress/iterate trio
- status: in_progress in this session (done when this documentation slice is committed)

## ACT-S020

- checked-in Ninja CMake presets
- status: ready after S010

## ACT-S021

- canonical staged build runner
- status: blocked by S020

## ACT-S022

- JSON result/artifact/timeout contract
- status: blocked by S021

## ACT-S023

- thin MCP wrapper over canonical runner
- status: blocked by S022

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

Ninja-only build policy is documented

## ACT-A004

canonical runner supports bounded staged execution

## ACT-A005

MCP returns compact structured results and artifact references

## ACT-A006

Windows/Android chat functional baseline passes

## ACT-A007

transport/component architecture approved after read-only audit

This session closes only ACT-A001, ACT-A002, and ACT-A003.

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
