# Offline mailbox — Windows Debug verification, 2026-11-17

Base: `fd08d202` (`main`). Scope: optional opaque-blob mailbox service and
transparent AppTraverse Aether transport composition. No GUI/model persistence
policy or SDK revision change is included.

## Executed evidence

Windows 11 x64, CMake 4.2, VS2022 desktop v143 / MSVC 19.44, Debug.

| Check | Outcome |
| --- | --- |
| `apptraverse_offline_mailbox_test` | PASS: bounded codec, truncation/invalid frames, failed-write silence, authenticated sender, retry ID/deadline, direct delivery, cumulative read ACK, lost ACKED, no-reader retention, backpressure, single-slot fairness and transient native ACK bypass. |
| `apptraverse_offline_mailbox_rocksdb_test` | PASS: binary blobs, synchronous receipts, idempotent STORE, first deadline wins, inclusive recipient-scoped deletion, stale/future ACKs, dedup after ACK, TTL filtering/sweep, counter preservation and batched reads. Includes sender-offline → database restart → recipient-read scenario with lost STORED and ACKED replies. |
| `apptraverse_offline_mailbox_crash_test` | PASS: child writer calls `_Exit` after a sync commit; a separate reader process restores the blob and counter from WAL. This is not a physical power-loss test. |
| `apptraverse_shared_sync_test` | PASS: existing native replication / loss / ACK regression suite. |
| `apptraverse_connection_runtime_test` | PASS: existing connection/channel cases plus stable-ID qualification and release through ConnectionHub, and transient control forwarding. |
| `apptraverse_aether_link_test` | PASS: existing native adapter framing/presence checks; no live registration. |

Six selected CTest tests passed. Service executable and
`apptraverse_shared_chat_session` compiled/linked with the enabled feature.
The service's no-argument invocation prints its usage and exits without
registering an identity.

The enabled build used a synthetic configured UID
`11111111-1111-4111-8111-111111111111`; no service was deployed at that address.
After rebuilding `apptraverse_shared_aether` with an empty configured UID,
`dumpbin /symbols` found **zero** `OfflineMailboxLink` references in
`aether_connection.obj`, versus 60 symbol matches in the enabled object.
This verifies compile-time exclusion of the decorator, not a cloud traffic trace.

## Dependency correction

The original mutable `aether-tele=main` cache entry was at `65eda03`, which lacks
`tele::raw::DecodeState` used by the already-pinned SDK. The initial service
build exposed this incompatibility. Pinning telemetry to
`461475099559d8cbcd27f75e0817bcaf36e44896` resolved it; the succeeding builds and
tests above used that revision. No dependency source was edited; the Aether SDK
pin remains `0c2b8936b4418af55ed4cdb00405d99fc329422a`.

## Boundaries

- Live Aether three-process exchange / network ACLs: NOT RUN.
- Windows GUI interaction, actual Windows 7, Linux, Android, Web and Apple:
  NOT RUN for this feature. Existing historical platform results are unchanged.
- Delivery is at least once; native SharedSync applies Events idempotently.
  Mailbox ACK covers handoff to a registered callback, not an application's
  model transaction. Client retry/hint state is runtime-only; server storage
  becomes independent of the sender after STORED.
- Chat retains its existing RAM-only history policy. This does not implement
  chat graph restoration after a client process restart.
- New code and the selected primary RocksDB license are Apache-2.0. RocksDB's
  embedded BSD material and the existing SDK's transitive notices remain;
  an exclusively Apache-2.0 transitive dependency set is not claimed.

Configuration, protocol and limits: [service README](../../services/offline_mailbox/README.md).
