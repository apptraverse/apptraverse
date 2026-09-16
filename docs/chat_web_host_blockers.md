# Chat demo web host — blockers (Commit 15)

Native Windows/Linux/Android delivery gates are stable on this branch. A browser
chat host was **not** implemented because required external prerequisites are
unavailable on the overnight Windows host.

## WEB_NETWORK_BLOCKED

| Prerequisite | Status on overnight host |
| --- | --- |
| Emscripten (`emcc`) in PATH | **Not installed** |
| `examples/surfaces_demo/web` reference host | Exists for surfaces only; no `examples/chat_demo/web` |
| Browser Aether transport branch | Not integrated; native pin unchanged |
| `BrowserChatConnection` / chat session bridge | **Not present** in-tree |

Implementing a web chat host would require:

1. Emscripten toolchain and a scoped EMSCRIPTEN CMake profile.
2. A browser-capable Aether transport compatible with the pinned
   `APPTRAVERSE_AETHER_GIT_TAG` (no silent native dep bump).
3. A thin DOM host mirroring `TryTakeUiUpdate` (same contract as Win32/GTK/Android).

**Decision:** record `WEB_NETWORK_BLOCKED`; leave native dependency pins and builds
untouched.

## WEB_DURABILITY_BLOCKED

Even if Emscripten were available, browser durability for chat would require:

- IndexedDB / IDBFS initialization before model profiles (surfaces_demo pattern).
- Explicit checkpoint after `Application::Save` and async IDBFS sync.
- Separate proof that application ACK boundaries align with browser persistence.

That integration is not started. Do not claim browser reload survives queued
messages until checkpoint semantics are wired for `ChatSession`.

## Reference only

`surfaces_demo/web` demonstrates surfaces WASM patterns (`web_surfaces_demo`,
IDBFS sync on publication). Those patterns are **not** copied into chat_demo
in this delivery slice.

## Native impact

**None.** No CMake, pin, or source changes were made for Emscripten in Commit 15.
