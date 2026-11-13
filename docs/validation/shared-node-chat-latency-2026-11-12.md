# Host + one client: headless chat delivery latency

Historical disk/polling configuration. The subsequent
[event-driven RAM chat](shared-node-chat-event-driven-2026-11-12.md) removes
both 100 ms presentation periods and chat disk saves; its timings have a
different endpoint/implementation and are recorded separately.

Measured on `feature/shared-node-native-memory` from `ee4698d0` on 2026-11-12.
The benchmark builds and runs; the first campaign exposed an intermittent
delivery stall. A second independent campaign completed all 100 samples.
This is measurement of the existing behavior, not a latency/recovery fix.

## Method

Two separate Windows processes run the unchanged production `ChatSession`
used by `shared_node_chat`. They use real Aether cloud transport, independent
fresh private identities/profiles and normal native disk persistence. Presence
is disabled and SharedSync's one-second ACK/retry interval is unchanged.

- Start: worker QPC immediately before `ChatSession::Send`, the entry point
  called by the GUI Send handler, after receiving the controller command.
- Observed: QPC immediately after a remote `View()` first returns the exact
  message. The consumer polls nominally every 1 ms. The production session's
  own 100 ms publication cadence is included.
- Presented: QPC after a headless display has consumed that view and built
  its message text, on an independent nominal 100 ms refresh cadence matching
  the window. This includes both publication and presentation pacing.
- Worker timestamps use the same machine-wide QPC, frequency 10,000,000 Hz.
  Controller/stdout receipt time is not subtracted or used as an endpoint.

Physical input, sender edit extraction/UTF-8 conversion, Win32 control updates
and actual painting are excluded. Presented is a virtual-display measurement,
not literal button-to-pixels timing. Benchmark scheduling, polling and the
observed-event stdout flush before the virtual display can add overhead.
The period values are nominal, not scheduling guarantees under load.

MSVC x64 **Debug**, Intel Core i9-12900HK, 20 logical processors,
Windows 10.0.26200. Other chats were building projects concurrently; sampled
CPU utilization was 99–100%. No idle-machine or Release result is claimed.
All participant processes ran on this one computer, through the real cloud.

Each campaign sends one first message in each direction, ten alternating
warmup messages, then plans 100 measured messages (50 each direction).
Text payload is exactly 128 ASCII bytes, including a unique sequence marker.
Messages are sequential, waiting for remote presentation before the next
send; seeded random 20–180 ms pauses outside the measurement reduce coupling
to periodic timers. Percentiles use nearest rank. Slow completed samples
are retained; timed-out samples are disclosed separately, not assigned a
fabricated finite duration or included as if delivered.

## Results

Run 02: **100/100 measured samples completed**, zero timeouts, exact text,
no observed duplicate messages, valid clock ordering and graceful stops.
Two first-message and ten warmup samples are excluded from this table.
Whole campaign including setup, first/warmup messages and shutdown: 117.63 s.

| Endpoint / direction | N | Minimum ms | Mean ms | Median ms | P95 ms | Maximum ms |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Presented, Host → Client | 50 | 259.41 | 867.18 | 448.75 | 2331.67 | 2421.46 |
| Presented, Client → Host | 50 | 267.42 | 975.67 | 1264.26 | 1877.02 | 2312.95 |
| Presented, both | 100 | 259.41 | 921.42 | 731.86 | 1877.02 | 2421.46 |
| Observed, Host → Client | 50 | 230.04 | 816.64 | 437.01 | 2254.09 | 2341.13 |
| Observed, Client → Host | 50 | 214.43 | 922.06 | 1211.44 | 1806.67 | 2250.65 |

Run 02 first messages after admission: Host → Client 209.69 ms presented
(208.18 ms observed); Client → Host 221.70 ms (219.83 ms observed).
These are first chat messages on an already admitted room, not cold startup.

Run 01 is **FAIL**: 94 measured messages completed (47 each direction), then
the 95th measured message, Host → Client sequence 107, did not appear within
30 seconds. Five remaining planned messages were never sent. Both published
states still said ready/Connected. The completed samples alone had presented
median 1250.35 ms, P95 1436.92 ms and maximum 2896.33 ms; these are conditional
statistics of delivered messages and exclude the unresolved >30 s sample.
The second run used a 60 s per-message timeout and fresh profiles; it does not
establish that the earlier stall is fixed. Its cause was not demonstrated.
CPU saturation is context, not a proven cause of the timeout.

Most delay in these samples is already present before the receiver consumes
the published view. A pure GUI repaint optimization would not account for
the measured second-scale delays. Root-cause profiling and stalled delivery
recovery remain follow-up work; no transport/SDK policy was changed here.

## Reproduction and evidence

Build `apptraverse_shared_chat_latency_worker` in the existing Debug tree.
Run explicitly; normal CTest never creates network identities:

```powershell
python tests/run_shared_chat_latency.py --exe build/shared-node-debug/examples/shared_node_chat/apptraverse_shared_chat_latency_worker.exe --out .artifacts/shared-chat-latency-new --samples 100 --warmup 10 --payload-bytes 128 --message-timeout 60
```

The controller checks worker/source/sequence identity, exact payload,
duplicate observations and `sent <= observed <= presented`. The worker target
build, Python syntax/help checks and run 02 passed. All owned workers stopped.
Production chat, SDK pins and SharedNode serialization/protocol are unchanged.

Committed raw samples: [run 01](data/shared-chat-latency-2026-11-12-run01.csv),
[run 02](data/shared-chat-latency-2026-11-12-run02.csv). The timed-out sequence
has no invented CSV latency. Full JSON summaries, event logs and private
profiles remain in `.artifacts/shared-chat-latency-01/` and `-02/`.
