# SharedNode Chat rendezvous

`shared_chat_rendezvous` runs the SDK rendezvous service on a regular registered
Æther client. The service UID is reused from its persistent profile. Expose the
configured UDP port, currently 46001 on the test VM. The signal channel itself
uses Æther cloud delivery. The public IPv4 address and UDP port are given to
the service at startup. It returns that endpoint over the signal channel.

Build the Linux service with `APPTRAVERSE_BUILD_AETHER_LINK=ON`,
`APPTRAVERSE_BUILD_AETHER_RENDEZVOUS=ON`, and
`APPTRAVERSE_AETHER_USER_CONFIG` pointing to
`cmake/shared_aether_rendezvous_config.h`. The test VM unit expects the binary
under `/opt/shared-chat-rendezvous` with the compatible Linux runtime under its
`runtime` subdirectory. The VM's systemd instance manages the state directory.

Clients with native UDP can set `APPTRAVERSE_AETHER_RENDEZVOUS_UID` to override
the Windows default `4bb28061-35e4-47dc-bfd7-1bbfae68b22b`. An empty value disables
rendezvous. The earlier `APPTRAVERSE_AETHER_RENDEZVOUS` setting is accepted as
a source of the UID. The UDP endpoint always comes from the service.
Clients enroll the sockets of each active interface and try all confirmed
address pairs in batches. The service still accepts the earlier enrollment
messages from clients that already have its UDP endpoint.
Without a usable direct path, the SDK sends chat messages through the Æther
cloud connection.

The shared Æther user config sets `APPTRAVERSE_AETHER_TELEMETRY_DISK_BYTES` to
1,000,000 bytes per profile's telemetry object. The in-memory raw archive
reserves part of this budget for serialized metadata. Its committed object
layers are checked before the file is written. `APPTRAVERSE_AETHER_TELEMETRY_UPLOAD`
is off, including when a telemetry keeper is configured in the environment.
