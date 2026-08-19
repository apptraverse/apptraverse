# Linux GTK3 single-client chat

x86_64 GTK3 host for the same ChatComponent / Aether / AetherP2pTransport stack
used by Windows and Android.

## Build

From the repository root:

```
cmake -S . -B build-linux -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build-linux --target linux_single_client_chat
```

## State directory

Default:

- `$XDG_DATA_HOME/apptraverse/single-client-chat`
- or `$HOME/.local/share/apptraverse/single-client-chat` if `XDG_DATA_HOME` is unset

Override:

```
./build-linux/examples/single_client_chat/linux_shell/linux_single_client_chat --state-dir /tmp/chat-linux
```

## Event-loop ownership

GTK owns widgets and the GTK main loop. Aether, ChatComponent, sync, and
persistence run on one background thread. Widget updates are dispatched with
`g_idle_add`.
