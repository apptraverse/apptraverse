# SharedNode minimal chat and native Client presence — 2026-11-11

Continues `feature/shared-node-native-memory` from `a85b1c2`. The user explicitly
requested the Client's online API and the simplest chat, without localization.

## Implementation

- Pinned a clean isolated SDK checkout to
  `89244bafb596509e915c8b4d3df9f30541b462d5` (`bench/presence-api-1s`). This
  contains `Client::IsLocallyOnline()` and `Client::QueryPeerPresence()`.
  Foreign SDK worktrees were inspected read-only. The common objects and its
  existing 27-patch chain were retained; the old SDK-specific Debug capture
  patch is no longer applicable and was removed.
- Removed application Probe/Reply and the adapter envelope. SDK
  `SizedPacketGate` frames the frozen native SharedSync bytes.
- Only local Online plus a fresh native remote Online permits business
  sends/chunks. Errors and stale answers map to Unknown. No data/ACK/echo
  inference is used. Queries serialize round-robin because the Client owns one
  action. An unfinished same-UID action retains its original start timestamp
  after timeout; a late result cannot become fresh through retiming.
- Added opt-in `shared_node_chat`: UID, Host room, invitation/Join, presence
  list, message log, input/Send. Native S0 Application/Room/Participant,
  Join/Text Events and a reflected BinaryArchive JoinRequest wrapper reuse the
  SharedNode admission/delivery runtime. Repeated JoinRequests keep the frozen
  admission. Link descriptors are shared; transport handles remain local.
- SDK/model work stays on the model thread. `ModelObjectProxy` queues GUI
  commands and the window consumes value-only publications. Native profile
  saves precede ACK; SDK identity saves before creating the application graph.
  Shutdown drains already accepted commands before its final save. Oversized
  UTF-8 drafts remain in the input instead of disappearing on rejection.

## Executed checks

MSVC x64 Debug, incremental existing build, exceptions/RTTI disabled. No fresh
dependency population, Release or other-platform validation was performed.

- SDK, adapter, worker, chat and explicit native GUI controller: **build PASS**.
- Existing seven SharedNode/native memory CTest entries: **7/7 PASS**, 10.40 s.
- Adapter native presence mapping/freshness/error tests and 85 KiB SDK framing:
  **1/1 PASS**, 0.09 s including CTest overhead. Old echo correlation and
  old-SDK-only budget tests are no longer claimed for this SDK.
- Targeted Node/clone/persistence/Application/WindowCatalog/MinimalApp
  headless regressions: **9/9 PASS**, 26.18 s.
- Actual native window inspected through computer-use: labels, layout,
  registration status and disabled room/send actions observed. This is UI
  startup verification, not successful room admission or message exchange.
- Explicit three-window controller: **FAIL / bootstrap BLOCKED** at its
  first client-ready stage; the Host window reports
  `Aether registration failed or timed out`. Evidence:
  `.artifacts/shared-chat-win32-native-api.log`, with unique private profiles
  under `.artifacts/shared-chat-native-api/`. The controller reaped its owned
  processes. Admission/Unicode/offline/restart assertions were not reached.

## Real-cloud blocker

The explicit headless campaign used three fresh owned processes/profiles:

```powershell
python tests/run_shared_aether_test.py --exe build/shared-node-debug/tests/apptraverse_shared_aether_worker.exe --out .artifacts/aether-native-presence-01 --deadline 480
```

**FAIL / bootstrap BLOCKED:** all workers remained unselected; H exited 2 after
the 120 s registration deadline. Controller summary records
`H exited 2; inspect its private worker log`. No snapshot, native presence
observation, offline recovery or checkpoint scenario began. Historical cloud
PASS from SDK `a5cfce84` does not apply to this pin.

Independent upstream experiment evidence was found in local SDK commit
`a6b6a1c5`, `examples/benches/presence_api/reports/validation-20261111.txt`:
both native API workers failed registration, with 7 bytes sent / 0 received
before server closure; ordinary no-proxy smokes also failed, while the working
push-SDK bootstrap control passed. Method 35 presence was never called.

Source comparison shows that this historical native-API line prepends a
RequestId to method 3 registration, while the working streaming/push line
sends `GetPublicKey(CryptoLib)` using push responses. The observations are
consistent with a bootstrap wire incompatibility; server-side cause and
presence-method support have not been established by this run. Checked
streaming/recovery branches contain echo benchmarks, not the requested native
Client API. No already-working branch combining both contracts was found.

The server, machine network configuration and foreign SDK sources were not
modified. A working native-API SDK/deployment is required before claiming
cloud chat, API Offline/recovery, or GUI restart acceptance. The explicit GUI
controller keeps these assertions and does not substitute a fake Online.

## Limits

This slice implements one room and the existing three-participant scenario on
one computer. Room merging, host failover, removal/rejoin, localization,
attachments and cross-machine clock ordering are outside scope. SDK profiles
and identities remain private local artifacts; no keys were exported.
