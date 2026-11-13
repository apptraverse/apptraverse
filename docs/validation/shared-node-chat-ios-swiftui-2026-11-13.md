# SharedNode iPhone SwiftUI chat — 2026-11-13

## Scope

Follow-up to the verified macOS port at `4a297522`: the user requested iPhone
with Swift. Controls are SwiftUI; UIKit supplies lifecycle/background execution
and clipboard only. The existing Apple presenter and bridge are shared with
macOS, and all model operations go through the common ChatSession proxy queue.
The host/client UID protocol and persisted graph schema are unchanged.

Deactivation queues a USER checkpoint and leaves the model, transport and
presenters alive. A bounded UIKit background task covers the completion; its
expiration cannot accidentally end a newer task. Ordinary model mutations
remain durable before ACK. No shutdown-only persistence or UI polling was added.

## Port blocker

The first iOS build failed in `dns_c_ares.cpp`: its native Apple platform guard
selected c-ares, while SDK CMake excluded `CMAKE_SYSTEM_NAME=iOS` from the
dependency. SDK commit `91aea53` adds iOS to that existing dependency condition;
no DNS implementation or wire protocol changed. Both Apple builds use this
exact SDK pin. The earlier cloud/transport fixes remain in its ancestry.

## Validation status

**BUILD PASS / UI ACCEPTANCE IN PROGRESS** — iOS Debug product built,
ad-hoc signed and installed on the simulator. Swift typecheck and standalone
XCTest runner build passed. Final macOS build and **18/18 CTests PASS (39.13 s)**
with SDK `91aea53`. No complete iPhone UI/cloud PASS is claimed. macOS product/acceptance rebuilt after common code
extraction; three native cloud clients passed delivery, forced restart and
USER/identity recovery again (`work/chat-macos-acceptance-7`). The preceding
macOS revision additionally passed 18 CTests and post-idle delivery.

The first iPhone acceptance attempt was blocked before XCTest by CoreSimulator
initialization waiting in filesystem reads. Restarting the simulator service
and device preserved app data and restored access. The second attempt is
still waiting for XCTest runner installation; the user observed that the
simulator is very slow and explicitly requested merging the current code
to main before continuing validation. No runtime/device data is committed.

The iPhone acceptance uses the actual product, standalone XCTest,
a private Mac host, and fresh private profiles. It checks Join/Send, rendered
Unicode replies, participant sheet, portrait/landscape, background continuity
(same process ID), and cold restore (new process ID, same history).

Environment: Intel Mac, MacPorts Clang 20, Xcode 15.2 / Swift 5.9.2,
iOS Simulator 17.2, iPhone 15 Pro. No Windows build, physical-device signing,
ARM64/device or MinSizeRel result is implied. Profiles contain credentials and
are excluded from repository and distributable app artifacts.
