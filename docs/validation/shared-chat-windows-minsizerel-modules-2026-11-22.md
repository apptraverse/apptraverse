# Windows SharedNode Chat MinSizeRel module sizes

All figures are bytes of the final x64 `shared_node_chat.exe`, built with VS2022
v143 in MinSizeRel. Stream hierarchy is OFF throughout. TCP and system DNS are
ON throughout; their individual sizes were not isolated.

| Configuration | EXE bytes | Difference |
| --- | ---: | ---: |
| Earlier cloud TCP + system DNS, libhydrogen baseline | 2,205,184 | — |
| + IPv6, UDP peer transport, LAN discovery, hole punching; no native WSS, libhydrogen | 3,075,584 | +870,400 from baseline |
| + native WSS (IXWebSocket + mbedTLS), libhydrogen | 3,806,208 | +730,624 |
| Same full set, libsodium instead of libhydrogen | 4,037,120 | +230,912 |

With the full libsodium and native WSS configuration held constant, independent
builds that disabled one feature at a time measured:

| Disabled feature | EXE bytes | Feature's measured addition |
| --- | ---: | ---: |
| Hole punching | 3,997,696 | 39,424 |
| LAN discovery | 4,030,464 | 6,656 |
| IPv6 | 4,035,584 | 1,536 |

These are linked-binary deltas, not static library sizes. They need not be
additive across different crypto and transport configurations because link-time
optimization, dead-code removal and section alignment can change the result.
The remaining UDP peer infrastructure is shared by LAN, direct IP and hole
punching and was not assigned to just one feature. IPv6 support covers Æther's
system sockets; the current SDK's native peer sessions require IPv4 UDP.

The SDK crypto providers report 42 bytes of symmetric encryption overhead for
libhydrogen (`hydro_secretbox_HEADERBYTES + uint64_t`) and 24 for libsodium
(`crypto_aead_chacha20poly1305_ABYTES + nonce`), a difference of 18 bytes per
encrypted block. Their asymmetric encryption overheads are 84 and 48 bytes,
respectively. These values exclude Æther framing and IP/transport headers.

Both full MinSizeRel builds registered on the configured Æther servers and
delivered 12 UI-submitted messages in each direction between two Windows
processes. The final libsodium EXE passed the same 24-message check after the
module probes; median remote display latency was 143.6 ms client to host and
135.9 ms host to client. Native WSS compilation was verified by inclusion of
`native_websocket_backend.cpp`, IXWebSocket and mbedTLS. This test does not
prove that the cloud connection selected WSS or that NAT hole punching works.

The final Windows-only libsodium selection was rebuilt to the same 4,037,120
bytes. Linux and Android retain the previous libhydrogen configuration.
Mixed-profile Windows tests also passed 24/24 messages in both role assignments:
libhydrogen host with libsodium client, and libsodium host with libhydrogen
client. Their median remote display latencies were about 200 ms. These tests
cover wire interoperability on the tested server, not profile migration on
other platforms.
