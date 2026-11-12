# Event-driven chat with RAM-only state

Continues `feature/shared-node-native-memory` from `773c92d9`. The user
removed the demo's disk-save requirement and periodic presentation updates.

## Change

The chat's Apply handlers call the existing Node materialized-change notifier.
The session observes its Application and Room, marks the view dirty and
publishes after the model transaction. Status transitions also mark it dirty.
Unchanged value publications are suppressed. There is no periodic snapshot
construction, generation polling, or 100 ms publication timer.

`ChatSession::Changed` runs after publication, outside the value-copy lock,
on the model thread. The window posts `WM_APP + 1` and consumes `ChatView`
when that message is dispatched. `SetTimer`, `KillTimer` and the `WM_TIMER`
refresh handler are removed. Closing joins the model thread before destroying
the window, so notification callbacks do not outlive the consumer. ACK and
retry bookkeeping do not observe/dirty the visible Room.

The chat graph, journal, receipt ledger and pending packets live in RAM.
Its SharedSync callback uses native SaveRoot into RamDomainStorage to keep
lazy object layers consistent; it never flushes that storage to disk.
SaveUserStorageFile/LoadUserStorageFile and root.id are removed from the chat.
ACK therefore confirms the native in-memory transition,
not crash-durable storage. Closing loses the chat state. An embedding
application chooses its own object-saving policy. The SDK still stores its
identity in a separate profile; old chat files are neither loaded nor deleted.

SDK I/O servicing and ACK/retry deadlines remain transport work, independent
of presentation. No SDK wire format, retry interval or Rx policy was changed.

A transport recovery gap was also fixed: Send used to refuse an unwritable
SDK stream before Queue, while Pump only recreated stalled nonempty queues
or recorded write failures. A permanently unwritable empty session could
therefore refuse every retry. It now retains its one bounded frame while
unwritable, enabling the existing stall/recreation path. SharedSync still
retains its obligation until application ACK; SDK write acceptance is not ACK.

## Executed checks

- Incremental MSVC x64 Debug chat, native controller and headless worker
  builds: PASS. No Release build or fresh dependency download.
- Final adapter/native/SharedNode memory CTest profile: **9/9 PASS**, 17.60 s,
  `.artifacts/chat-event-driven-final-ctest.log`.
- An additional SharedSync regression verifies notifications through
  mid-journal replay on both replicas, then replays a duplicate Event and its
  ACK without further materialized-change notifications: **PASS**, 1.52 s,
  `.artifacts/chat-event-driven-notifier-ctest.log`. This also executes the
  whole updated SharedSync suite. Only the test model's Apply was instrumented;
  the common Node and SharedSync implementations were unchanged.
- Real-cloud host + client, unchanged production session, event-driven
  headless consumer: **100/100 measured messages PASS** (50 per direction,
  128-byte ASCII payload), plus two first messages and ten warmups. Zero
  timeouts, exact payload/sequence/order checks and graceful worker stops.
- Both consumers then observed **zero publication notifications during three
  seconds of idle**, including ordinary ACK/retry servicing. No `native/`
  chat store or `root.id` was created in either fresh profile. Consumer View
  polling and its previous virtual 100 ms display timer are removed; it waits
  on a condition variable signalled by the same callback used by the GUI.

Headless evidence: `.artifacts/shared-chat-event-driven-03/summary.json`,
`controller.jsonl`; committed [raw samples](data/shared-chat-event-driven-2026-11-12.csv).
The first development attempt stopped before measurement because the chat
Apply handlers had not yet called NoteMaterializedChange; this was corrected
before successful development run 02. Final run 03 also includes native
RAM graph commits and the transport queue recovery correction.

Final three-window native GUI acceptance **PASS** in
`.artifacts/shared-chat-event-driven-gui-05.log`: Host/Alice/Bob admission,
Unicode/all-peer messages, continued H/A use while B is stopped, restoration
of the SDK UID with empty chat membership/history after restart, no chat
checkpoint files, and clean shutdown. Test button dispatch now uses the
native BN_CLICKED parent notification; cross-process BM_CLICK was ambiguous
for inactive windows. Earlier GUI runs 01/03/04 stalled on all-peer delivery;
run 02 stopped at room creation. No GUI PASS is inferred from those attempts.
Only run 05 includes the queue recovery correction.

The separate three-process **transport regression PASS** in
`.artifacts/shared-chat-event-driven-transport-02/summary.json` covers large
snapshots, direct all-pairs delivery, no old-history Event replay, retries
while B is absent, preserved-profile catch-up/checkpoints/restarts. That
worker explicitly chooses persistent storage; it is not a RAM-chat restart
claim. Its first attempt rejected a controller watch command; unreadable
command files are now left for a later service turn instead of being treated
as rejected business input, with the existing controller deadline. Final
rerun passed all stages. All owned worker processes and chat windows stopped.

## Timing scope

QPC starts immediately before Send and ends after notification consumption
and text projection. It includes the actual network/RAM apply path, but not
physical button input, sender edit conversion or Win32 painting. Same-machine
processes use real Aether on a shared machine with other builds and GUI
validation active. This is not an isolated/idle performance environment.

| Direction | N | Observed median ms | Presented median ms | Presented P95 ms | Presented max ms |
| --- | ---: | ---: | ---: | ---: | ---: |
| Host → Client | 50 | 1184.96 | 1185.14 | 2132.34 | 2260.36 |
| Client → Host | 50 | 1182.14 | 1182.29 | 1276.47 | 2727.28 |

The mean per-message gap between observed view and virtual presentation is
0.19 ms. Second-scale delivery delays remain before presentation. This is
not a controlled A/B speed comparison with the previous disk/polling build,
and the previously observed latency timeout was not traced sufficiently to
attribute its cause to the now-corrected unwritable-queue path.

## Build environment

An initial compile hit zero free disk space. Only regenerated top-level
test PDB files in this checkout's Debug build were removed (26 files,
1,478,860,800 bytes); sources, profiles and dependency caches were retained.
A separately owned RAPIDS source cache disappeared during the checks; the
Debug build was reconfigured to a local copy of an existing cached 24.02
tree with disconnected fetching. No server or system networking was changed.
