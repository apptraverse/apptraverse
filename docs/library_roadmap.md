# AppTraverse reusable libraries — roadmap

Phase **1 (this branch, Windows)** delivers layered static libraries, generic
`ModelPublicationSession`, Surfaces demo as a consumer, and a minimal second
consumer without Surface types.

## Phase 1 (done / in progress on `cursor/surfaces-presenter-decomposition`)

| Target | Role |
|--------|------|
| `apptraverse_core` | Storage, serialization, Node/Event, graph walk — no Surface demo types |
| `apptraverse_ui` | Control models, neutral presenters, object localization |
| `apptraverse_application_runtime` | UI mirror, model proxy, publication session, presentation size queue |
| `apptraverse_shared_sync` | SharedNode / sync frame (optional; not required for local UI) |
| `apptraverse` | INTERFACE aggregate: runtime + shared_sync (backward-compatible link) |
| `apptraverse_ui_windows` | Win32 control presenters |
| `apptraverse_surfaces` / `apptraverse_surfaces_windows` | Surfaces **demo** model + chrome (consumer, not the runtime) |

**Not in phase 1:** `apptraverse_win_application_host` (message pump extracted
from `win_app.cpp`), full Win32 merge of `Win32Surface*` with `ui/*` presenters,
full schema-v0 sweep outside Surfaces types, updater/manifest.

## Phase 2 — Platform ports (after Windows)

Move AppKit, GTK, Web, iOS, and Android adapters from `examples/*` into
platform libraries (`apptraverse_ui_*`, `apptraverse_*_application_host`).
One shared model/runtime; native loop integrated via platform host capability
flags (desktop Z-order vs mobile pages). Do not declare platforms supported until
host + presenters are verified on device/CI.

## Phase 3 — Optional Æther components (after ports)

Separate optional library: serializable config/state/peer refs and app messages;
explicit client/thread/subscription lifecycle; no network on Load/replay; callbacks
queue into the model. No mandatory SharedNode, no new ACK protocol. No empty
“future” interfaces in phase 1–2.

## Phase 4 — AeroAdmin / apptraverse-app

Build product from runtime, controls, and optional network components; own root,
business model, UI composition, and protocol. Out of scope until phases 1–3 land.
