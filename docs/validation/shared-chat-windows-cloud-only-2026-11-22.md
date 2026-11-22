# Windows SharedNode Chat: Æther cloud-only build

Direct UDP peer transport was disabled. The chat uses the Æther SDK cloud TCP
transport, with no peer transport configuration in `AetherConnection`.

An x64 MinSizeRel build completed. The EXE is 2,205,184 bytes, 868,864 bytes
smaller than the preceding direct-UDP MinSizeRel build (3,074,048 bytes).

Two Windows processes on one computer exchanged 12 messages each way through
the Send button. All 24 arrived. Median time to display remotely was 248.1 ms
client to host and 231.8 ms host to client. Neither process opened a UDP
endpoint during the run.
