# Chat latency: local backpressure incorrectly consumed the ACK timeout

Continues `feature/shared-node-native-memory` from `0f2fd3ae`. The user asked
why two chat processes on the same computer now took about 1.18 s when old
experiments had shown about 200 ms.

## Identified cause and fix

SharedSync::Service set next_send = now + retry_interval **before** calling
IByteLink::Send and discarded its bool result. AetherByteLink has one queued/
in-flight frame per peer; while that frame waits for SDK completion, Queue
returns false for a new frame. The existing frame may be an Event or an
application ACK, since both share that transport capacity.

The refused Event had transmitted nothing but was treated as awaiting its
remote ACK. Even if local capacity became available shortly afterward, its
first accepted send waited for the whole one-second deadline. This adds the
observed approximately one-second step before ordinary network delivery.

The fix keeps the tentative deadline before Send for synchronous-ACK safety,
but erases it when Send returns false. A refused frame can try admission on
the next Service turn, about 10 ms in this demo. An accepted frame retains
the full one-second ACK retry interval. No SDK/Rx/presence policy, SafeStream
configuration, wire serialization, disk policy or UI publication was changed.

False admission checks are local operations, not accepted network frames.
The IByteLink contract now documents this distinction. There is no busy loop
inside one Service call; the caller still controls servicing. A readiness
notification could avoid repeated local admission checks in a future API.

## Deterministic reproduction

The new TestRefusedSendDoesNotStartAckTimeout deliberately refuses admission
with a zero send budget, then restores capacity at retry_interval - 1. The
old implementation fails the immediate-first-send assertion; evidence:
`.artifacts/shared-chat-latency-refused-send-red.log` (**intentional red**).

The same test passes with the fix and additionally verifies no duplicate
before the full ACK deadline measured from acceptance, lost-ACK retry with
identical frozen bytes/packet ID, and termination of retries after ACK.
`.artifacts/shared-chat-latency-refused-send-green.log`: **PASS**, 2.33 s.

## Actual two-process control

Production ChatSession, real Aether cloud, separate Host/Client processes on
one Windows computer; MSVC x64 Debug; RAM-only chat and event-driven consumers.
QPC measures Send -> remote view/text projection; actual Win32 painting and
sender input/edit conversion are excluded. Each text payload is 128 ASCII
bytes. Both runs use the same seeded 20–180 ms inter-message jitter, two
separate first messages and six warmups, followed by 40 alternating measured
messages (20 each direction). First/warmup are excluded below. The two runs
are sequential, not randomized; other builds were active and CPU was sampled
at 100%, so this is not an isolated performance/SLA experiment.

| Version / presented endpoint | N | Mean ms | Median ms | P95 ms | Max ms |
| --- | ---: | ---: | ---: | ---: | ---: |
| Before fix | 40 | 1075.11 | 1176.63 | 2206.90 | 2297.73 |
| After fix | 40 | 302.01 | 221.41 | 346.46 | 1785.31 |

All planned messages completed, exact payload/sequence checks passed, no
timeouts, and zero publication notifications during the idle check in both
runs. Only the admission/ACK scheduling behavior changed in this control.
Together with deterministic red/green reproduction, this identifies the
fixed one-second backpressure penalty; it does not attribute every remaining
millisecond or tail event to that bug.

An independent **100/100** fixed-code measured campaign (50 each direction,
ten warmups plus two first messages) also passes:

| Direction | Median ms | P95 ms | Max ms |
| --- | ---: | ---: | ---: |
| Host -> Client | 316.62 | 1405.61 | 1821.77 |
| Client -> Host | 303.49 | 462.38 | 1831.98 |
| Both | 306.59 | 736.96 | 1831.98 |

The remaining tails up to 1.83 s were retained. Their exact cause was not
isolated in this experiment; neither uniform 200 ms delivery nor elimination
of every transport stall is claimed. The larger run also has zero timeouts,
zero idle publications, no chat disk files and clean worker shutdown.

## Regression checks

- Incremental Debug targets rebuilt; **9/9** adapter/native/memory CTests
  **PASS**, 27.64 s: `.artifacts/shared-chat-latency-cause-ctest.log`.
- Three real native chat windows **PASS**: admission, Unicode/all-pair
  delivery, continued room use with one process stopped, SDK UID restore
  with a fresh RAM chat and no disk history, clean shutdown.
  `.artifacts/shared-chat-latency-cause-gui.log`.
- Persistent H/A/B real-cloud transport campaign **PASS**: large snapshot,
  direct fanout, no incremental old-history replay, absent-peer pending,
  catch-up, checkpoints and cold restart. The worker now records exact logical
  Service timestamps for accepted retries of each (peer, Event ID); the
  controller requires at least one full second between accepted repeats.
  It no longer mistakes local Send refusals for network transmissions.
  During the three-second stopped-peer observation, 142 local admission checks
  occurred with **zero newly accepted frames**; the recorded minimum accepted
  same-Event retry gap was 1,000,084 us. B subsequently caught up and all stages
  completed. `.artifacts/shared-chat-latency-cause-transport/summary.json`.

All owned processes stopped. SDK/dependency pins and native object patch
chain are unchanged; no fresh dependencies or Release builds were used.

## Historical experiments

Repository history contains the old low-latency RAM chat at 7a1e1b7f and
the measured-write fix at dade96bd. Its Windows transport wrote directly to
P2pStream, retained multiple pending write subscriptions and used a 1 s Ping /
3 s receive window. The current transport adds SafeStream and bounded frame
admission. Those differences support investigating queue behavior; old code
was not transplanted into this fix. The local UI latency suffix in 3286488a
is not used as a remote-latency measurement. No archived numeric 200 ms chat
result was found in the searched records.

Every(1s) in the current SDK sets both interval and rx_window to 1 s, so it
does not itself specify a one-second application receive poll. We left this
setting unchanged for the control rather than assuming it explained the delay.

## Raw evidence

Committed samples: [before](data/shared-chat-backpressure-before-2026-11-12.csv),
[after](data/shared-chat-backpressure-after-2026-11-12.csv),
[fixed 100](data/shared-chat-backpressure-after-100-2026-11-12.csv).
Full private profiles, logs and JSON summaries are under
`.artifacts/shared-chat-latency-cause-baseline/`, `-fixed/`, and `-fixed-100/`.
