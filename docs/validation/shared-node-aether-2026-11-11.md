# SharedNode: optional Aether transport — 2026-11-11

Status: **PASS** — MSVC x64 Debug, real cloud and three private processes.
Branch: `feature/shared-node-native-memory`; starting commit: `ee1c48c`.
Extends [confirmed checkpoints](shared-node-checkpoint-memory-2026-11-11.md).

## Scope and provenance

The user requested Aether integration first and specified that the later chat
must be the simplest SharedNode demonstration, without localization or unrelated
features. No GUI is part of this transport slice. Existing native SharedNode
serialization, reliability, admission and checkpoint logic remain the model
contract; `AetherByteLink` replaces the byte transport.

The current exact SDK pin is `a5cfce84fb46c6e506e963af8f63aa01ca355b06`, branch
`bench/presence-streaming-recovery`. Its historical base `488383c8` contains
merge `39a2cfe0`, combining the presence probe on streaming `4c1d125e` with
deployed push API `5ddf5eaa`. The selected committed continuation adds:

- `99f41a9d`: allocation-free P2P/SafeStream wire-budget computation;
- `0e20c253`: bounded presence probes and streaming validation notes;
- `97f2fd53`: native ping ownership/deadline recovery on the streaming base;
- `a5cfce84`: selected-route and saved-startup observations.

These revisions became available during this integration. The pin was advanced
after the initial live attempts exposed the older budget-calculation defect.
Only committed SDK content and the explicit consumer patch are used in the
isolated checkout; foreign worktrees are not modified or used as mutable source.

`cmake/shared_aether.cmake` verifies the isolated SDK SHA and the single allowed
consumer patch's resulting blob. The Debug registration lambda's vector capture
made the closure 40 bytes for 32-byte SmallFunction storage; the consumer patch
captures a shared pointer instead. This is a capture-storage fix, without a
public ABI or protocol change. The common object dependency and its existing
27-patch chain remain unchanged.

Both `APPTRAVERSE_BUILD_AETHER_LINK` and the nested
`APPTRAVERSE_BUILD_AETHER_CLOUD_WORKER` default OFF. The optional targets are
`apptraverse_shared_aether`, `apptraverse_aether_link_test` and
`apptraverse_shared_aether_worker`. The cloud controller is an explicit run,
not an automatic network-dependent CTest entry. Build configuration uses
ordinary SDK TCP, cloud-compatible crypto, no benchmark observer/proxy hooks,
and persisted identity loading.

The selected SDK branch currently exists locally. Validation uses an isolated
checkout supplied with `APPTRAVERSE_AETHER_CLIENT_SOURCE`; this change does not
publish or modify the dependency's original branch/worktree. In an existing
MSVC Debug build with cached dependencies, enable:

```sh
cmake -S . -B build/shared-node-debug -DAPPTRAVERSE_BUILD_AETHER_LINK=ON -DAPPTRAVERSE_BUILD_AETHER_CLOUD_WORKER=ON -DAPPTRAVERSE_AETHER_CLIENT_SOURCE=<isolated-sdk-checkout>
cmake --build build/shared-node-debug --target apptraverse_aether_link_test apptraverse_shared_aether_worker
```

An isolated checkout can be created with `git worktree add --detach` from the
local SDK repository at the exact SHA above. The consumer applies only its
checked registration patch. Validation reused cached bcrypt/Hydrogen/Sodium,
ETL/stdexec/c-ares/Rapids/ICM sources and existing common dependencies; no
dependency was populated from the network and no Release build was used.

## Adapter contract

- SDK P2pStream/P2pSafeStream and SizedPacketGate carry a native reflected
  transport envelope and unchanged frozen SharedSyncMessage bytes. No custom
  fragment offsets or field protocol are introduced.
- Business messages are capped at 1 MiB. Native framing and bounded chunks
  handle messages larger than the SafeStream circular buffer.
- Per-peer timestamped probes default to 1 Hz and a five-second expiry. Online
  requires an outstanding exact sequence/request-time echo within that window.
  Expiry uses original monotonic send time; delayed buffered replies and
  uncorrelated native push ACKs cannot resurrect Online.
- One business message per peer, bounded probe/reply jobs and one native write
  are allowed at a time. Busy/unwritable/oversized sends fail locally; the
  SharedNode sender retains durable retry state. Offline suppresses new business
  sends while probes continue. Accepted in-flight data can still drain.
- Stalled/offline or failed native writes allow bounded session recreation.
  The local native packet collector is reset; generation checks suppress old
  callbacks. This is not a claim of arbitrary midstream network recovery.
- Context/client/AetherApp outlive the model-thread adapter. Subscriptions are
  removed before stream teardown. Transport discovery does not itself admit
  an application participant.

## Execution evidence

Windows MSVC x64 Debug, incremental configuration/build. No Release, other
platform runtime or GUI validation is implied.

Adapter unit result: **PASS** (0.08 s in the final combined profile). The cases
exercise native envelope shape/version/bounds, exact fresh echo correlation,
stale/duplicate/restart echoes, probe pacing/bounded bookkeeping, SDK framing of
an 85 KiB native message and subscription teardown. An explicit unlimited TCP
budget regression calls both SDK sizing helpers with UINT32_MAX/SIZE_MAX and
verifies bounded native PackedSize results without allocating trial payloads.

Final combined adapter/native/memory profile: **8/8 PASS** (9.26 s), including
the seven existing entries. The live-discovered backpressure regression is
included in `apptraverse_shared_sync_test`.

Targeted headless regressions: **9/9 PASS** (45.64 s). All nine targets were
built incrementally; assertions remain enabled, without RTTI or exceptions.

Explicit cloud command (use the configured optional build and a fresh private
output directory):

```sh
python tests/run_shared_aether_test.py --exe <aether-build>/tests/apptraverse_shared_aether_worker.exe --out <fresh-private-output> --deadline 480
```

Three-process real-cloud result: **PASS**, controller exit 0, artifacts in
`.artifacts/aether-live-04/`. The live convergence record observes initial native
payloads of **34,001 bytes** at A and **34,751 bytes** at B. All six directed
Event paths are exercised; host/foreign relay and covered old Event sends to B
remain zero. Final replicas have the same full model digest, checkpoint 1000,
one post-restart Event and no pending delivery; A's author index continues to 4.

The first attempts, `.artifacts/aether-live-01` and
`.artifacts/aether-live-02`, used historical SDK pin `488383c8` and failed during
`A watch`, before the SharedNode model scenario began. A debugger attached only
to the owned A worker captured
`MaxP2pStreamWritePayloadBytesForDatagramBudget(0xffffffff)` calling
`Encode(0x80000000)` and attempting approximately 2 GiB of encryption allocation.
Committed SDK fix `99f41a9d` removes payload allocation from that budget search.
Those failed runs do not prove synchronization, presence recovery or checkpoint
delivery.

`.artifacts/aether-live-03/` uses the corrected SDK and proves fresh peer
presence, but its first model snapshot starves: a queued H10 Event precedes
InitialState, repeatedly consumes the transport's one business slot and cannot
be ACKed before bootstrap. `SharedSyncRuntime::Service` now prioritizes frozen
InitialState packets. A deterministic memory test limits acceptance to one
message and verifies the initial bytes, coverage ACK cleanup and later Event
delivery. The complete live scenario then passes in run 04.

The worker also follows the existing Windows `RegisterClass` macro boundary and
refuses to create a new model if nonempty native storage has lost its root ID.

The owned process controller requires:

| Scenario | Evidence required |
| --- | --- |
| Independent startup | Three distinct persisted Aether client UIDs and three application graphs |
| Large native initial state | Native initial payload exceeds 16 KiB and restores full model digest |
| Admission and all-pairs Events | H admits B, then each author delivers directly to both other replicas without host relay |
| No historical replay | H/A do not send snapshot-covered Events incrementally to B |
| Offline B | Presence expires; new A→B business attempts stop while H/A continue |
| Restart B | Same UID/profile restores and receives only missing durable pending Events |
| Checkpoint | All participants publish progress and restore the same compacted state |
| Cold restart all three | Stable UIDs/checkpoint/digests restore; author index continues with a new directly delivered Event |

Private run output contains persisted client identities and model state.
Keep those profiles local; reporting should use bounded status/result evidence
without exporting keys or copying profile contents into repository documents.

Final `git diff --check`: **PASS**.

## Limits

The demonstrated runtime scope remains three participants on one computer.
There is no localization, GUI chat, room merge, peer removal/rejoin, admission
failover, cross-machine clock contract or full client-version negotiation.
Checkpoint compaction retains stamp/ID metadata and does not perform native
object-store garbage collection. Presence follows a probe-derived adapter
policy, not a newly invented SDK Presence API. Only executed cloud scenarios
can substantiate recovery; network impairment matrices and arbitrary long
outage recovery are separate work.
