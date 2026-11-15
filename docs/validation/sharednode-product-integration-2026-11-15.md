# SharedNode product integration — 2026-11-15

Source branch: private 6fa140ae, two commits c910c239 and 6fa140ae. Integrated into current AppTraverse main 54f715ae, preserving the newer pinned Aether client 0c2b8936. No AeroAdmin sources or SDK/Presence implementation changed.

Included process ConnectionState, ConnectionHub, ProcessServices, Windows network observer, independently injected transport and presence adapters, and ChatSession event publication. Aether CloudServerConnections::NetworkAvailable was removed upstream; adapter now calls Restream() after route change. ChatMessageView includes durable all-recipient delivery and sender-local ack_elapsed_us. The latter starts after local commit, ends when all-recipient ACK state is first published, and uses only the sender's monotonic clock; replayed/foreign messages have no elapsed value. A solo room does not report delivered.

Windows MSVC Debug Ninja scoped build: ConnectionRuntime, SharedSync, three-peer, SharedNode memory, graph import, Aether link and explicit ChatSession send-commit targets PASS. CTest 6/6 PASS, 17.77s. Explicit ChatSession acceptance PASS: rejection, local commit, persisted structured record and cold restore, shutdown/startup failure. Added assertion that a solo local commit has neither recipient delivery nor ACK duration. Build and test logs in local worktree: build-scoped-2.log, scoped-ctest.log, chat-send-commit-2.log.

The ConnectionRuntime suite tests independent graph channels, dropped frame retry and durable recipient ACK. SharedSync suite exercises restart, loss, duplicates and three-peer admission. No two-device timing measurement or consuming AeroAdmin build was run here; consumer must update its AppTraverse pin to the final main SHA and verify its own Windows build.
