# Direct P2pStream UDP recovery acceptance — 2026-11-13

Executed on Windows x64 Debug with framework `2419938d` (main tree
`3f0eec2a`) and SDK `cc8155d2` (main tree `efd16915`). The application uses
plain P2pStream; no adaptive stream or SafeStream wrapper was enabled.

- Current UDP build: PASS, 12 selected targets.
- Scoped local tests: 8/8 PASS, 37.985 seconds including the test runner.
- Real UDP recovery controller: PASS, 32.859 seconds including clean shutdown.
- Cached SDK identities were copied from the closed TCP acceptance profiles;
  business USER started in a fresh room. Both peers verified actual UDP paths.
- Client network cut blocked traffic in both directions. Both sides committed
  messages while disconnected. Forced Client termination and restart retained
  its identity and delivered both offline directions, including an 8192-byte text.
- A bidirectional burst under deterministic loss, duplication, delay and reorder
  converged to 23 identically ordered messages without duplicates. Actual fault
  counters: outgoing dropped 35, duplicated 20, delayed 48, reordered 44;
  incoming dropped 11, duplicated 7, delayed 15, reordered 11.
- Confirmed event journals and pending packets collapsed. Both processes were
  force-stopped, then cold-started with the same identities and complete history.
  Message 24 was delivered over the restored UDP transport.
- Final worker shutdown exit codes were zero; each profile used one native
  `user.bin` with no separate `root.id` checkpoint.

Evidence in the overnight workspace outputs:
`shared-p2p-udp-build-resumed-v3.json`,
`shared-p2p-udp-unit-resumed-v3.json`,
`shared-p2p-udp-recovery-resumed-v1.json`, and
`shared-p2p-udp-recovery-details-resumed-v1/summary.json`.

This is a correctness/recovery gate, not a throughput or online-Presence
benchmark. It does not establish Android/Web/Apple chat acceptance.
