# SharedNode Chat rendezvous

`shared_chat_rendezvous` runs the SDK rendezvous service on a regular registered
Æther client. The service UID is reused from its persistent profile. Expose the
configured UDP port, currently 46001 on the test VM. The signal channel itself
uses Æther cloud delivery.

Build the Linux service with `APPTRAVERSE_BUILD_AETHER_LINK=ON`,
`APPTRAVERSE_BUILD_AETHER_RENDEZVOUS=ON`, and
`APPTRAVERSE_AETHER_USER_CONFIG` pointing to
`cmake/shared_aether_rendezvous_config.h`. The test VM unit expects the binary
under `/opt/shared-chat-rendezvous` with the compatible Linux runtime under its
`runtime` subdirectory. The VM's systemd instance manages the state directory.

Clients with native UDP can override the rendezvous setting through
`APPTRAVERSE_AETHER_RENDEZVOUS=UID@IPv4:PORT`. An empty value disables it.
The Windows chat defaults to the service on `aether-test-server` at
`34.69.190.161:46001` (UID `4bb28061-35e4-47dc-bfd7-1bbfae68b22b`).
Without a usable direct path, the SDK sends chat messages through the Æther
cloud connection.

The shared Æther user config sets `APPTRAVERSE_AETHER_TELEMETRY_DISK_BYTES` to
1,000,000 bytes per profile's telemetry object. The in-memory raw archive
reserves part of this budget for serialized metadata. Its committed object
layers are checked before the file is written. `APPTRAVERSE_AETHER_TELEMETRY_UPLOAD`
is off, including when a telemetry keeper is configured in the environment.
