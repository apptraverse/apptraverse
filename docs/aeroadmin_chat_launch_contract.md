# AeroAdmin Chat Launch Contract

This document describes how AeroAdmin (or any external launcher) starts the
AppTraverse chat client (`apptraverse_chat`) on Windows. The AeroAdmin product
executable itself is **not** part of this repository; only the chat client
behavior documented here is implemented in-tree.

## Executable

Windows build target: `apptraverse_chat`  
Built from: `examples/chat_demo/windows/`

## Command-line flags

All arguments are parsed as UTF-8 tokens from the process command line (via
`CommandLineToArgvW`). No shell is invoked; the raw command line is never
executed.

| Flag | Required | Description |
| --- | --- | --- |
| `--state-dir` | No | Profile directory. Unicode paths and spaces are supported. |
| `--peer-admin-id` | When opening a peer | Opaque AeroAdmin user identifier. |
| `--peer-aether-uid` | No | Explicit Aether transport UID hint (canonical UUID text). |
| `--peer-name` | No | Display name for a new chat entry. Unicode supported. |

`--peer-aether-uid` and `--peer-name` require `--peer-admin-id`.

Repeated flags or unknown options are rejected. A flag whose next token is
another option (starts with `--`) is treated as a missing value.

### Examples (PowerShell)

Plain start (default profile under `%LOCALAPPDATA%\AppTraverseChat`):

```powershell
& "C:\Path\To\apptraverse_chat.exe"
```

Custom profile with Unicode path:

```powershell
& "C:\Path\To\apptraverse_chat.exe" `
  --state-dir "D:\Users\Иван\App Data\Chat"
```

Open or select a peer:

```powershell
& "C:\Path\To\apptraverse_chat.exe" `
  --state-dir "D:\ChatProfiles\demo" `
  --peer-admin-id "123456789" `
  --peer-aether-uid "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee" `
  --peer-name "Support Agent"
```

## Profile location

- Default: `%LOCALAPPDATA%\AppTraverseChat`
- Override: `--state-dir <absolute-or-relative-path>`
- Persisted model graph: `<state-dir>/model/`
- Aether client state: `<state-dir>/aether/`
- Profile lock file: `<state-dir>/profile.lock`

The runtime normalizes `--state-dir` to an absolute lexical path for locking
and IPC validation.

## Single instance per profile

Only one live process may write a profile.

1. The primary instance acquires an OS-backed exclusive lock on
   `profile.lock` when it starts.
2. A second launch for the **same** normalized profile forwards an open-peer
   request to the primary over Win32 `WM_COPYDATA` and exits without loading
   Aether or model storage.
3. A second launch for a **different** profile starts a separate instance.
4. If the primary is closing or does not respond within the IPC timeout, the
   secondary exits with an error (no second writer).

Simultaneous cold starts race on the profile lock; the loser retries locating
the primary notification window briefly before failing.

## Repeated launch behavior

When `--peer-admin-id` is present and the profile is already open:

- The secondary encodes `OpenPeerRequest` + profile key and sends it to the
  primary's hidden notification window (title derived from a hash of the
  profile key; the full profile key is validated in the payload).
- The primary queues `ChatSession::OpenPeer` on the model thread and returns
  **accepted** (delivery to the peer is asynchronous).
- The primary raises its main window (best-effort foreground).
- Exit code: `0`.

Invalid or oversized IPC payloads are rejected without mutating workspace
state.

## Exit codes

| Code | Meaning |
| --- | --- |
| `0` | Success (primary ran, or secondary forwarded successfully) |
| `1` | Argument parse error, profile lock failure, IPC timeout/rejection, or primary startup failure |

## UID rules

- `--peer-aether-uid` must be canonical UUID text (36 characters,
  hyphenated hex). All-zero UUIDs are rejected.
- When a chat entry is already bound to an Aether endpoint, a conflicting UID
  hint is rejected.
- Self-connection (local UID equals target) is rejected once the local
  endpoint is known.

## AeroAdmin resolution

No AeroAdmin directory lookup API exists in this repository. `--peer-admin-id`
is stored and displayed as an opaque identifier; `--peer-aether-uid` is the
explicit transport hint when AeroAdmin already resolved the peer endpoint.
