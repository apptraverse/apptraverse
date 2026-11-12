# SharedNode delivery without presence

Follow-up: a longer two-peer [latency campaign](shared-node-chat-latency-2026-11-12.md)
found an intermittent >30 s delivery stall. The short acceptance PASS below
retains its executed scope and does not qualify sustained delivery reliability.

Continues `feature/shared-node-native-memory` from `6e3a921`. The user asked
to disable Online checks for now and resend whenever the application ACK
has not arrived by its timeout.

## Behavior

`AetherLinkOptions::use_presence` defaults to false. The adapter makes no
peer-presence queries and does not gate sends or native chunks on local or
remote Online. Valid peer UIDs report Unknown, including during a stopped
recipient's absence. Unknown does not fabricate an availability observation.
The minimal chat shows participant names without Online/Offline badges.

SharedSync already allows Unknown and owns the durable ACK/retry loop, so its
protocol and serialization are unchanged. It persists a frozen packet before
sending, retains it until a matching application ACK and retries at its
configured interval (one second in this demo). SDK write acceptance/completion
does not clear the obligation. Duplicate Events are not applied again; a
receiver acknowledges its already persisted result after retransmission.
Native presence remains an explicit opt-in, with its previous strict policy.

The adapter still admits at most one queued message per peer. A failed write,
or a stalled queued write in the default mode, permits bounded SDK session
recreation. SharedSync retains the durable packet through that replacement.
There is no additional application delivery queue or echo protocol.

## Executed validation

MSVC x64 incremental Debug; the existing SDK, dependency pins and object
patch chain are unchanged. No fresh dependency population or Release build.

- Chat, native UI controller, cloud worker, adapter test and SharedSync test
  builds: **PASS**.
- Adapter, native presence fixture and native/memory SharedNode CTest profile:
  **9/9 PASS**, 9.49 s. Evidence:
  `.artifacts/ack-retry-ctest-2026-11-12.log`.
- Extended the live-memory ACK case to lose the Event first, verify no retry
  before the exact timeout, deliver the identical retry, lose its ACK, restart
  the receiver and verify another timed retry. Duplicate delivery applies the
  Event once; the durable ACK stops retries, including after sender restart.
  Both peers stay Unknown. Existing loss/reorder/Join/checkpoint memory cases
  also passed in the same profile.
- Actual H/A/B cloud campaign: **PASS**. Initial native snapshots reach
  34,001 bytes at A and 34,751 bytes at B. The host admits the third participant,
  every pair delivers its own Events directly, and snapshot-covered history
  is not resent as incremental Events. Stopping B leaves A's packet pending;
  three further send attempts occur in the three-second ACK-timeout check.
  B restores the same UID and catches up. All three confirm progress, compact,
  restart from their checkpoints and deliver a new Event with its author
  index preserved. Presence queries remain zero and valid peer availability
  remains Unknown. Evidence: `.artifacts/aether-no-presence-03/summary.json`
  and `controller.jsonl`. The earlier completed run 02 also passed; attempt
  01 stopped on a controller variable-order bug before sending, then was fixed.
- Explicit native three-window chat controller: **PASS**. Fresh private
  Host/Alice/Bob profiles, admission, names without presence badges, Unicode
  messages from all three windows, delivery while B is stopped, same-UID
  restart/catch-up, a new message from restarted B and clean shutdown.
  Evidence: `.artifacts/shared-chat-no-presence-2026-11-12.log` and private
  profiles under `.artifacts/shared-chat-no-presence/`.

Cloud processes and GUI windows created by these controllers were stopped.
Real-cloud checks do not inject packet loss; deterministic Event/ACK loss,
duplicates and reordering are covered through memory. This result does not
qualify the optional native GetClientTiming service, whose earlier missing
replies remain recorded in [native-presence validation](shared-node-presence-streaming-2026-11-11.md).
