# SharedNode native presence on the working push/streaming SDK

Historical native-presence result. The 2026-11-12 follow-up disables presence
by default and verifies network chat through application ACK/retry; see
[current validation](shared-node-ack-retry-2026-11-12.md). The missing native
GetClientTiming replies below remain specific to the opt-in presence path.

Continues `feature/shared-node-native-memory` from `392f393`. The user asked to
combine the native Client presence API with the working SDK used by the
neighboring App project. This record supersedes the earlier registration
blocker; it does not claim that remote presence or network chat now passes.

## Frozen integration

The public SDK base is `5ddf5eaaac5bd154ec384ff8c74f247a0ab80fb7`, the exact
commit used by App. `aether-client-shared-native-presence.patch` adds the
streaming fixes and native presence port as SDK source changes. A separate
Debug-only registration capture fix keeps the existing SubApi callback within
SmallFunction capacity. Ordinary registration, Ping, P2P message and push ACK
formats remain unchanged. Only the already defined GetClientTiming RPC uses
request IDs and result/error dispatch. SharedSync still uses native archive
serialization and SDK message framing.

The development SDK branch is `feature/sharednode-native-presence`, commit
`423ab1e1364a05cd4349764bde0d2341a18c82de`. It freezes the SDK port from
`bench/presence-api-streaming` on working streaming base `e89359c8` (which
already combines published push `5ddf5eaa` and streaming `4c1d125e`). Foreign
worktrees were read only. No foreign benchmark reports, credentials or private
commit history are required by the consumer package.

The consumer patch contains 56 source/test files, including the ordinary A/B
control's configurable timeout. A manifest checks each postimage, permitted
changes and root CMake variant. It accepts either the public base plus the
frozen patches or the equivalent development checkout. Patch application,
postimage checks and configuration passed on a clean isolated public-base
worktree; final compilation/runtime used the development checkout with the
same verified source/test postimages. The root CMake difference only adds
disabled benchmark examples. The existing common Objects pin and its 27-patch
chain are unchanged. No fresh dependency population or Release build was run.

SDK changes include native local confirmed Ping/Pong schedules, remote
Personal Cloud queries, request/result correlation and bounded pending RPC
responses. Remote soft timeouts retain late-response subscriptions without
Restream of shared connections. Failure/absence is not fabricated Offline.
The adapter still requires local Online and a fresh native remote Online.

The ClientTiming archive now follows server `ClientServerApi.adsl.yaml`:
lastActivityDeltaMs, then nextPingDeltaMs. The legacy C++ field name
last_connect_delta_ms is retained for source compatibility. An independent
server-shaped fixture failed against the old ordering and passed after the
fix, including a negative next-Ping delta. Local or remote echo is never used
as availability evidence.

## Executed validation (MSVC x64 Debug)

- SDK/chat/worker/control builds: PASS, exceptions and RTTI disabled.
- Qualified adapter/native/memory and SDK presence/RPC/registration CTest
  profile: **13/13 PASS**, 39.61 s. The native presence executable includes
  15 deterministic cases; RPC checks cover mixed push/RPC, errors, truncated
  replies, pending-response lifetime and sender teardown. Evidence:
  `.artifacts/combined-qualified-ctest.log`.
  The final adapter rerun also passed (0.21 s), including the restored bounded
  TCP-budget regression check.
- Targeted common Node/clone/persistence/Application/WindowCatalog/MinimalApp
  regressions: **9/9 PASS**, 72.78 s;
  `.artifacts/combined-regressions.log`.
- Actual SDK A/B cloud control: **PASS**, exit 0, four messages delivered in
  both directions, fresh private profiles, 5.60 s. Evidence:
  `.artifacts/sdk-ab-combined-01/exchange.log`. This verifies ordinary
  transport, not presence or SharedNode synchronization.
- Actual GUI local acceptance through computer-use: room creation, Unicode
  Text Event, graceful close and same-profile reopen restored the identical
  SDK UID, room invitation and one message. The profile is private under
  `.artifacts/chat-local-combined`. All test chat windows were closed. This
  is a single-participant persistence check, not network delivery.

An additional legacy SDK aggregate Ping harness is **not qualified**: its
Debug mock replaces an outstanding write when Debug GetMyIp writes to the
same stream, producing two write-failure fixture assertions. A temporary
fixture experiment was reverted. This aggregate is outside the five targeted
SDK presence/RPC/registration checks and is not reported PASS. Fake object
names in its separate source files were made unique to remove duplicate
registry IDs. The production Ping write-failure path was not changed to
silence those assertions. A full SDK test-suite PASS is not claimed.

## Remaining network blocker

The combined SDK successfully registers all three independent H/A/B clients
and its local Client API reports Online. However, remote native queries have
three resolved peer-cloud servers and three queried servers with **zero timing
samples and zero native Online results**. The adapter therefore remains
Unknown and starts no business sends. This is later than the previous
registration failure, and earlier than SharedNode initial-state delivery.

The final headless campaign is `.artifacts/aether-combined-05/`: exit 1,
`timeout: native 16KiB initial state over Aether`. Admission, direct delivery,
remote Offline, restart catch-up and checkpoint stages are not reached.
Earlier attempts 02–04 also encountered Windows status-file sharing failures;
the disposable controller status publisher now defers transient sharing locks
for a bounded period, and its reader grants FILE_SHARE_DELETE. Native state
saves and ACK durability are unchanged. Run 05 reaches the actual replication
timeout without a status-publication exit.

The explicit three-window GUI controller also fails at joining participants:
Host creates the room; Alice and Bob remain waiting for the host.
Evidence: `.artifacts/shared-chat-combined-final.log`. No GUI peer delivery,
native remote Offline or multi-participant restart PASS is claimed.

SDK request/response format and local source schema were checked; absence of
a cloud response does not prove a server-side cause or that the deployed
server implements the same RPC contract. The server and machine networking
were not changed. A working native GetClientTiming response is required to
unblock strict availability gating. The prior echo-based cloud PASS remains
historical and is not transferred to this native-API integration.
