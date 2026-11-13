# SharedNode chat: macOS SwiftUI — 2026-11-13

## Scope and revisions

The user requested latest mains, a macOS chat port, and tests, and explicitly
selected SwiftUI. Windows builds are excluded by the user's subsequent explicit
instruction. No Windows compilation was started; the briefly resumed VM was
returned to its original suspended state.

AppTraverse base: `68233585` (main, including host/client roles and UID-only room discovery). Dependency mains fetched for this task:

| Repository | Revision |
| --- | --- |
| aether-client-cpp | `efd16915c6f2e0614c9d73390c9c4a18ce4eb294` |
| aether-objects | `fa4cd4d0f0bb1b79b03e58eb723651091deb20e9` |
| aether-miscpp | `6839624442621a5b060ae610e91f480278ebaf6e` |
| aether-tele | `461475099559d8cbcd27f75e0817bcaf36e44896` |
| aethernet-numeric | `8bda57f2fe562109f722301dd916e907d1822244` |

The SDK pin additionally includes the cloud-resolution completion fix
`fe204d2f5a20769b88d0d644dfb5af6588164318` and owner-loop transport fix
`dcc3197e564baeb2f3d4b8fcb19fac7dcc54bd70` (current pin), published on
`fix/cloud-resolution-completion-macos-20261113`, not merged to main.
Existing Objects patch order and crypto dependency pins are unchanged.
Numeric is resolved explicitly before telemetry/SDK so the application pin is
actually applied. Related Aether dependency mains are pinned to exact commits.

## Presentation and lifecycle

All chat controls are SwiftUI. AppKit supplies only the application/window
shell and standard menus. `ChatPresenter` consumes value-only `ChatView`
publications through a thin Objective-C++ bridge to the existing `ChatSession`.
Host/Join/Send use the session's `ModelObjectProxy` command queue. No second
model, direct UI domain mutation, or periodic UI refresh was introduced.
Deactivation leaves the session alive; closing the last window drains/stops it.
An advisory lock rejects concurrent use of the same private profile.

Debug was built with the exported embedded chat supply and native USER restore,
using macOS 13.7.8 x86_64, Xcode 15.2/Swift 5.9.2, SDK 14.2, MacPorts Clang 20,
Ninja and a deployment target of macOS 13.0. The acceptance binary compiles the
same SwiftUI view/bridge with a separate stdin test driver. It enables native
accessibility, presses actual SwiftUI buttons, reads accessibility text/control
state/frame, and captures the real window. Product builds have no test channel.

## Failures found and corrections

Initial UI checks exposed an incorrect success exit code for a locked profile,
unmaterialized SwiftUI accessibility nodes in the test host, and a collapsed
empty history column. These were corrected before repeating acceptance.

The first longer three-window run delivered Unicode messages to all peers over
TCP, but retained an old coverage receipt at the host. It later aborted in
`GetCloudFromCache` allocation: `etl::variant_pool<5>` was full. The crash stack
went through `ClientCloudManager::GetCloud`, `P2pStream::ConnectSend` and the
adapter's stream replacement path.

A focused SDK test reproduced the missing completion: `GetCloudFromAether`
emitted a successful result but never called `Finish()`, retaining the pool
slot. Both terminal request paths now finish; duplicate cloud publications
before deferred destruction cannot finish/destroy the action twice. The new
test covers unrelated publications, successful/error resolutions, duplicate
publications and twelve resolutions through a two-slot pool. It failed before
the fix and the complete eight-case cloud suite passed afterward.

A memory test reproduced the retained coverage receipt when H admits A and B
before either initial ACK returns. B's initial snapshot already includes A's
Join, so B never installed the older A admission plan and cannot acknowledge
its coverage notice. The host now excludes recipients whose own admission
snapshot covers that Join. This changes neither serialization nor model schema.
Experimental profiles created before this fix are preserved; no automatic
repair/migration of already-retained erroneous notices was added.

## Executed checks

Before the two protocol fixes, seven SharedNode/native graph CTests passed
(19.61 s), followed by eight adapter/SDK/framing CTests (13.43 s).
The new SDK regression failed before the fix and passed with all eight cases
in its suite after the fix. The new overlapping-admission memory regression
failed before the fix because its durable queues did not settle.

Post-fix build succeeded. All seven SharedNode/native graph CTests passed
(17.94 s), including the new overlapping-admission case and the existing
loss/reorder/checkpoint/restart cases. All eight adapter/SDK/framing CTests
passed (22.16 s), including the expanded eight-case cloud suite.

The first post-fix three-window run passed recovery and delivery stages, but
failed delivery after 180 seconds of idle. Native samples showed a lock inversion:
the model thread held the packet queue mutex while waiting for the socket poller
mutex, while the poller held its mutex and synchronously entered the queue.
Socket write readiness now schedules coalesced work on the owner loop. TCP
receive callbacks likewise run outside the receive buffer mutex. A focused
13-case transport suite failed two new cases before this fix and passed all
13 afterward. The latest-main product and acceptance builds succeeded. All **18/18** scoped
CTests passed (38.95 s), including packet queue readiness/receive regressions
and the domain hierarchy/pointer release checks. The latest-main three-window
SwiftUI/cloud acceptance **PASS**: UID-only admission, Unicode rendered once
on every peer, TCP, zero pending packets before and after forced restart,
offline delivery, identity/history restore, delivery after 180 seconds idle,
and orderly shutdown. Native screenshots were visually checked. The user
also confirmed that the manually launched macOS chat works with Windows.

Local evidence is under `work/chat-macos-*`, `work/chat-cloud-regression-*`, and
`work/chat-admission-regression-*` in the task workspace. Test profiles contain
SDK credentials and are not committed or included in app bundles. Windows,
iOS, device/ARM64, MinSizeRel and a full platform matrix are not covered here.
