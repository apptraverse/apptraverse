# Offline mailbox for AppTraverse / Æthernet

The service is an ordinary C++ Æthernet client. It stores opaque blobs in a
local RocksDB directory. Application code and the chat API do not issue mailbox
commands: `MakeAetherTransport` composes the infrastructure when a service UID
is configured at compile time.

## Build and run

Use a C++20 compiler and the repository's normal Debug toolchain (desktop MSVC
v143 on Windows). Build the service first, without a client service UID:

```sh
cmake -S . -B build-mailbox -DCMAKE_BUILD_TYPE=Debug \
  -DAPPTRAVERSE_BUILD_EXAMPLES=OFF \
  -DAPPTRAVERSE_BUILD_AETHER_LINK=ON \
  -DAPPTRAVERSE_BUILD_MAILBOX_SERVICE=ON
cmake --build build-mailbox --config Debug --target apptraverse_mailbox_service
apptraverse_mailbox_service service-identity mailbox-db
```

The executable prints `MAILBOX_UID ...` after registering/restoring its SDK
identity. Preserve both directories across restarts. The configured Æthernet
parent/network permissions must allow peers to communicate with that UID.

Compile clients, including the existing `shared_node_chat` example, with:

```sh
cmake -S . -B build-chat -DCMAKE_BUILD_TYPE=Debug \
  -DAPPTRAVERSE_BUILD_AETHER_LINK=ON \
  -DAPPTRAVERSE_BUILD_SHARED_NODE_CHAT=ON \
  -DAPPTRAVERSE_MAILBOX_SERVICE_UID=REPLACE-WITH-THE-PRINTED-UUID \
  -DAPPTRAVERSE_MAILBOX_TTL_SECONDS=86400
```

Use a separate client identity/profile for every running client. The service
uses `MakeDirectAetherTransport`, so its own replies never enter another mailbox.
Empty `APPTRAVERSE_MAILBOX_SERVICE_UID` (the default) selects the original
transport. There are no envelopes, polls, RocksDB linkage or service dependency
in that client path. Existing raw service channels, such as push, bypass mailbox
envelopes and retain their original framing.
Mailbox-enabled peers use the mailbox-capable framing; mixed enabled/disabled
outgoing peer compatibility is not negotiated by this example.

## Protocol

| Command | Meaning |
| --- | --- |
| `STORE(messageId, recipient, expiresUtcMs, blob)` | Idempotent insert, scoped by authenticated sender + recipient + ID. |
| `STORED(messageId, sequence, expiresUtcMs)` | Sent only after WAL-synchronous, atomic commit of body, counter and dedup receipt. It confirms service storage, not recipient delivery. |
| `FETCH` | Return the authenticated caller's oldest unacknowledged entries, in increasing sequence order. Reading does not delete. |
| `BATCH(entries)` | At most 32 entries and less than one MiB per frame. Expired entries are omitted. |
| `ACK_THROUGH(sequence)` | Delete all entries with sequence **less than or equal** to this value in the caller's mailbox. |
| `ACKED(sequence)` | Durable cumulative deletion cursor; stale ACKs cannot move it backwards. |

The sender's message ID is a random process epoch plus a counter. Repeating an
internal STORE reuses that ID and deadline. The recipient's `sequence` is a
different, persistent uint64 counter assigned by the service. It is never reset
when a queue becomes empty, entries expire or the service restarts. Different
recipients have independent counters. Gaps after TTL expiry are legitimate.
ACKs beyond the highest sequence actually offered to that recipient are rejected.

The sender always attempts direct delivery and service storage, independently
of presence. Lost STORED/FETCH/ACKED replies are retried. The stored message body
is removed by cumulative ACK; its small dedup receipt stays until the original
deadline, preventing a delayed STORE from resurrecting acknowledged data.
Deadlines are absolute UTC milliseconds; retries never renew an existing record.
Clients and the service need usable UTC clocks. TTL is 1 second to 7 days.
Reproducible native SharedSync ACKs use the transient direct path; they do not
create storage obligations or fill the auxiliary queue during a service outage.

SharedSync's existing immutable node/packet identity is propagated through
ChatLink and ConnectionHub. Native packet retries reuse their transport ID and
do not enqueue another service record. Completing the native obligation releases
the transport hint. New byte-identical calls to ordinary `Send` are distinct
messages. The mailbox never interprets the blob's contents.

Delivery to AppTraverse is **at least once**. Repeated native packets reach
SharedSync so it can regenerate a lost ACK; native Event identities prevent
applying an Event twice. Mailbox ACK means the complete batch was handed to the
registered receive callback, not that an application persisted its model. In
particular, this does not change the chat's existing RAM-only history policy.
Sender retry state is also runtime-only: STORED is the boundary after which the
service retains data independently of the sender process.

Runtime admission is bounded: 128 unconfirmed writes / 16 MiB, 4096 outstanding
native packet hints / 16 MiB. A full queue refuses admission so SharedSync can
retry. Blob size is at most 900 KiB to leave room for transport metadata inside
the SDK's one-MiB message limit. Expired service records are excluded immediately
and physically removed in bounded sweeps (up to 256 records per second).

## License

New source files use Apache-2.0, like AppTraverse. RocksDB is pinned to v9.10.0,
commit `c344e30e276b5359f1a0970a00395a0d70a2bf2b`; this integration selects its
Apache-2.0 license option, not GPLv2. Optional compression, flags and benchmark
dependencies are disabled. RocksDB nevertheless contains third-party material,
including BSD-licensed xxHash; its original notices must be preserved. This is
not a claim that every transitive component of RocksDB or the existing Æthernet
SDK is exclusively Apache-2.0.

## Focused checks without building the SDK/UI

```sh
cmake -S services/offline_mailbox -B build-mailbox-tests -DCMAKE_BUILD_TYPE=Debug
cmake --build build-mailbox-tests --config Debug
ctest --test-dir build-mailbox-tests -C Debug --output-on-failure
```

Set `APPTRAVERSE_BUILD_MAILBOX_STORE=OFF` for protocol/client-only checks without
fetching RocksDB. These deterministic checks do not register live cloud clients.
