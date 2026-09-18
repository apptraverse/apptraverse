# Messenger («Мессенджер»)

Minimal Win32 AppTraverse example: one window, one dialog, real Æther identity,
and pair journal sync. Based on `examples/surfaces_demo` lifecycle (model thread,
GUI mirror, publications) — not the Host/Client chat demo.

## Build

Incremental MSVC Debug (existing tree `build/chat-a01-msvc-debug`):

```bat
cmake --build build/chat-a01-msvc-debug --target win32_messenger
```

Target: `win32_messenger` (distillation enabled).

## Run two instances

Use separate state directories:

```bat
build\chat-a01-msvc-debug\examples\messenger\windows\win32_messenger.exe --state-dir messenger_a
build\chat-a01-msvc-debug\examples\messenger\windows\win32_messenger.exe --state-dir messenger_b
```

1. Wait until each window shows its own Æther UID (copy becomes enabled).
2. In A, paste B’s UID into the peer field and press Enter.
3. In B, paste A’s UID and press Enter.
4. Type a message and press Enter to send. Transcript shows `→` (outgoing) and `←` (incoming).

Invalid peer UID text is ignored (not sent to the network API).

## State

`--state-dir` holds the AppTraverse Domain graph. Æther identity is under
`<state-dir>/aether`. Restarting the same directory restores draft, window
geometry, peer, and conversation history; journals converge without duplicates
when both sides reconnect.

## Tests

```bat
build\chat-a01-msvc-debug\examples\messenger\messenger_model_test.exe
build\chat-a01-msvc-debug\examples\messenger\messenger_aether_uid_test.exe
```
