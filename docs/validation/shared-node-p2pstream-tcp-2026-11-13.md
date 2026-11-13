# SharedNode direct P2pStream over TCP — 2026-11-13

The user selected direct P2pStream and explicitly authorized publication to
main. The adaptive-stream experiment remains paused. The default Windows
chat configuration enables TCP and disables UDP; acceptance verifies the
actual send path on both peers rather than inferring it from configuration.

## Implementation

- `AetherByteLink` owns SDK `P2pStream` directly. SafeStream and SizedPacketGate
  are removed from the adapter. Native-archive message fragments are bounded
  by the encrypted SDK write budget and reassembled before SharedSync sees
  them. Complete retries may be redelivered to replace a missing business ACK.
- SharedSync prioritizes a bounded runtime cache of deferred ACKs before data
  retries, so simultaneous writes cannot starve each other's receipts.
- USER/model schemas and SharedSync serialization are unchanged. Runtime
  fragments, transport handles and deferred ACK cache are not persisted.
- SDK pin `cc8155d24a69c0c25b8515d4b9aebfe31c7b2180` is published on the
  client's main through [PR573](https://github.com/aethernetio/aether-client-cpp/pull/573).
  It contains the runtime foundation used by this chat, supported-channel
  cache selection and native I/O shutdown before graph reclamation.

## Executed Windows x64 Debug checks

The build used embedded chat supply with normal native USER persistence,
Hydrogen crypto, the clean exact SDK pin and telemetry main. Twelve requested
targets built. Eight scoped CTest executables passed in 14.43 seconds:
native channel persistence, adapter framing/budget, message fragments,
memory link, SharedNode memory, SharedSync, graph import and three-peer
replication. Fragment/SharedSync cases include 1 MiB payloads, missing/reordered
and duplicate fragments, lost ACKs, one-slot simultaneous backpressure,
bounded incomplete assemblies, malformed input and restart.

Six consecutive real three-window GUI runs passed. Each registered separate
private identities, admitted three peers, delivered Unicode/all-peer messages,
killed one participant, committed while it was stopped, restored the same
identity/USER, synchronized the missing message, sent from the restored peer
and closed all surviving processes successfully.

The explicit TCP network-cut controller passed in 23.83 seconds, including
worker cleanup. It verified TCP on both peers, disabled only the owned Client's
actual sockets, confirmed no delivery bypassed the closed gate, persisted
messages in both directions while offline, killed/restarted that client,
delivered an 8192-byte chat text, compared identical ordered histories without
duplicates and waited for zero retained journal events/pending packets. Both
processes were then killed and cold-restored with the same identities/history;
the restored TCP chat delivered another message and stopped with exit code 0.

Local evidence: `shared-p2p-tcp-build-v4.*`, `shared-p2p-tcp-unit-v4.*`,
`shared-p2p-tcp-gui-fixed-{1..6}.*`, `shared-p2p-tcp-acceptance-v4.json` and
`shared-p2p-tcp-recovery-details-v2/summary.json` in the task outputs. Identity
profiles and credentials are private test artifacts and are not committed.

## Discovered failure and correction

The first GUI runs completed delivery/restart but crashed during shutdown with
`0xc0000005`. A debugger captured IOCP `WinSocket::OnRead` →
`TcpBase::OnRecvData` → `AeContext::scheduler` using a reclaimed Aether context.
The SDK now joins native readiness workers at the start of AetherApp teardown,
before releasing its persistent graph; descriptor removal remains valid until
socket destruction. The six subsequent GUI runs and TCP controller above
used that fix. The harness also waits for actual control readiness during
WM_CREATE and records child exit codes/logs.

## Scope limits

This is Windows TCP functional acceptance, not a throughput or online Presence
benchmark. UDP local tests passed on the preceding SDK revision; real UDP fault
acceptance and the platform runtime matrix were not rerun for this publication.
Apple/C-client work, AeroAdmin app work, MinSizeRel/size measurements and a full
30-rule production audit are not completed by this change. Existing GUI
architecture debt remains separate from the verified transport behavior.
