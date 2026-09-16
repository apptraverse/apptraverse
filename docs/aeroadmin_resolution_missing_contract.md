# AeroAdmin Admin-ID → Aether UID Resolution — Missing Contract

**Status: BLOCKED WITH THE MISSING CONTRACT**

This document records the Commit 13 source inventory. No authoritative AeroAdmin
directory lookup API was found in authorized checkouts. The chat client therefore
does **not** resolve `--peer-admin-id` to an Aether transport UID.

## What was searched

Authorized trees only (no private files, credentials, or external services):

| Location | Search | Result |
| --- | --- | --- |
| `apptraverse-chat-delivery` (`examples/chat_demo/`, `docs/`, `include/`) | `AeroAdmin`, `admin_id`, `peer-admin-id`, directory lookup | Launch + IPC docs only; no resolver API |
| `aether-client-cpp-presence-rebase` (sibling) | `AeroAdmin`, `admin_id`, resolve UID | No matches |
| `apptraverse-prep-deps-assert` (sibling) | same | Plan milestone text only |

Matching field names (`peer_admin_id`, `--peer-admin-id`) are **not** a lookup
contract. They store and display an opaque AeroAdmin user identifier.

## What exists in-tree (working without lookup)

| Mechanism | Role |
| --- | --- |
| `--peer-admin-id` | Opaque label; opens/selects a local `ChatEntry` |
| `--peer-aether-uid` | Explicit transport hint when AeroAdmin already resolved the peer |
| Persisted `ChatEntry` + `peer_link` | Cached bound chats reload endpoint from profile |
| `OpenOrSelectChat` / `ChatSession::OpenPeer` | Same validated binding path for supplied UID |

Documented in `docs/aeroadmin_chat_launch_contract.md` § AeroAdmin resolution.

## What is missing (required before implementation)

An authoritative contract from the AeroAdmin product owner must specify at least:

1. **Service entry point** — HTTP/gRPC/RPC client, base URL, or in-process API.
2. **Input ID** — representation, normalization, and charset rules for Admin ID.
3. **Authentication** — credentials, tokens, or session requirements.
4. **Success response** — canonical Aether UID format and validation rules.
5. **Failure modes** — not-found, offline, timeout, unauthorized; distinct error codes.
6. **Desktop integration** — how AeroAdmin today supplies `--peer-aether-uid` to
   `apptraverse_chat` when launching a chat.

Until that contract exists:

- Do **not** add a directory server, hash mapping, or fake local lookup table.
- Do **not** claim Admin-ID-only onboarding works end-to-end.
- Preserve supplied-UID launch and cached bound chats unchanged.

## Intended adapter shape (when contract arrives)

One common asynchronous resolver posting completion onto the `ChatSession` model
queue, then reusing the Commit 02 validated endpoint binding path. Platform UIs
(Windows, Linux GTK, Android) must not each implement separate lookup logic.

## Related delivery status

- Commit 13: documentation only; **BLOCKED WITH THE MISSING CONTRACT**
- Final report item 8: **BLOCKED WITH THE MISSING CONTRACT**
