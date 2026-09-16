# AppTraverse

AppTraverse is a C++ application lifecycle platform for building long-lived distributed applications.

It is a standalone platform — not merely an SDK and not a product application.

## Status

Active development. The repository provides AppTraverse core, SharedNode
sharing, and the `examples/chat_demo` multi-platform chat client (Windows
host implemented; Linux GTK and Android sources present). See
`docs/chat_delivery_worklog.md` for verification status per platform.

## Requirements

- C++20
- CMake 3.20+
- Desktop: Windows, Linux, or macOS

Dependencies are fetched with [CPM.cmake](https://github.com/cpm-cmake/CPM.cmake). No other package managers are used.

## Build

```bash
cmake -S . -B build
cmake --build build
```

This configures the project and builds `model_ui_runtime_demo` plus the object/domain subset used by App Traverse. The Aether client, sockets, crypto, and P2P stack are not built.

## Object system (Aether)

The object system is **not** part of AppTraverse. It comes from [aethernetio/aether-client-cpp](https://github.com/aethernetio/aether-client-cpp):

- headers: `aether/obj/`, `aether/ptr/`, `aether/domain_storage/`
- pin: `cmake/aether_version.cmake` → `APPTRAVERSE_AETHER_GIT_TAG`
- build glue: `cmake/aether_object.cmake` → static target `aether`

By default CMake uses a sibling checkout `../aether-client-cpp` when present. Otherwise CPM downloads the pinned commit into `build/_deps/aether-client-cpp-src/`.

Override the tree explicitly:

```bash
cmake -S . -B build -DAPPTRAVERSE_AETHER_REPO=/path/to/aether-client-cpp
# or
cmake -S . -B build -DCPM_aether-client-cpp_SOURCE=/path/to/aether-client-cpp
```

AppTraverse code (`include/apptraverse/*`, demo model classes) uses `ae::Obj`, `ae::Domain`, `AE_OBJECT_REFLECT`, and links `apptraverse` → `aether`.

## Chat Demo (Common Model & Local Workspace)

`examples/chat_demo` provides the common model, `ChatSession`, local persistence,
and Host/Client launch options:

- `ChatWorkspace` (`Node`): local persistent workspace root (`demo_role`, `host_uid_input`).
- `ChatEntry` (`Node`): local conversation keyed by peer Aether UID, draft, scroll, peer `Link`, shared `ChatRoom`.
- `ChatRoom` (`SharedNode`): shared messages replicated over Aether.
- `ParseChatLaunchOptions`: CLI (`--host` or `--client`, `--state-dir`, `--host-uid` prefill).
- Windows target: `apptraverse_chat`. Linux: `apptraverse_chat` (GTK3). Android Gradle project under `examples/chat_demo/android/`.
- Manual Host/Client steps: `examples/chat_demo/MANUAL_TEST.md`.
- Tests include `apptraverse_chat_demo_launch_options_test`, `apptraverse_chat_demo_model_test`, `apptraverse_chat_session_integration_test`, and platform smoke tests where the host supports them.

## Namespace

Root namespace: `apptraverse`.
