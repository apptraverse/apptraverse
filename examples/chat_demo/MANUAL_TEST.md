# Manual test — App Traverse Chat example (Host / Client)

This example is a two-party Host/Client conversation. It is not a product remote-control chat.

## Launch

Use the packaged `apptraverse_chat.exe` and the launchers next to it:

- `start-host.cmd` → `apptraverse_chat.exe --host`
- `start-client.cmd` → `apptraverse_chat.exe --client`

Profiles are separate:

- Host: `%LOCALAPPDATA%\App Traverse\ChatExample\host`
- Client: `%LOCALAPPDATA%\App Traverse\ChatExample\client`

Do not point both processes at the same `--state-dir`.

## Steps

1. Start Host. Wait until **Host UID** shows the real Aether UID (not `Registering…`).
2. Press **Copy**.
3. Start Client.
4. Paste into **Host UID** and press **Join** (or Enter in that field). The host user does not enter the client UID.
5. Send a message from Client, then a reply from Host.
6. Scroll up, type an unsent draft, close both windows, and reopen with the same launchers.
7. Confirm history, draft, viewport, and the same Host UID. Client must **not** auto-Join; press **Join** again.
8. Confirm the same conversation (no duplicate rooms/messages).
9. Disconnect/reconnect during an already joined run. Offline/Online and pending delivery should not change identity.

## Limits

- Two-party conversation only (one Host, one Client per chat).
- `--host-uid` prefills Client input; it does not Join.
- Exactly one of `--host` / `--client` is required for a normal launch.
