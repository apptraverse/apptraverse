# Windows SharedNode Chat: direct UDP

Historical experiment: direct UDP was disabled in the subsequent cloud-only
build and later re-enabled in the MinSizeRel module study. The measurements
below describe the earlier UDP-enabled version.

Windows SharedNode Chat now enables the Æther SDK UDP peer transport while
retaining the cloud TCP transport. After client selection, the connection
configures the SDK peer with the same app ID used for registration. The SDK
chooses a direct route when available and retains its cloud route as fallback.
Other platform configurations continue to use their previous transport.

## Evidence

- A clean x64 MinSizeRel build of `shared_node_chat` completed. The EXE size is
  3,074,048 bytes (2.93 MiB). No route diagnostic code is in the product.
- Two Windows processes on one computer exchanged 12 messages each way from
  the actual Send button to the other chat window. All 24 arrived. Median
  remote display latency was 156.9 ms client to host and 156.4 ms host to
  client; median local display latency was 110.9 and 108.9 ms, respectively.
- A separate Release diagnostic run, using the same UDP configuration,
  reported final cumulative SDK route counts of `cloud=1, direct=0, lan=81,
  hole=0` on one side and `cloud=1, direct=4, lan=79, hole=0` on the other.
  These counters confirm that the ongoing peer traffic used direct LAN UDP.
  That run's median remote display latencies were 147.0 and 146.3 ms.

The UI measurements include send-button handling, local SharedNode updates,
network transit and remote painting. They cover one Windows host and one
Windows client on the same machine; they do not establish NAT traversal or
Android latency.
