# AppTraverse

AppTraverse is a C++ application lifecycle platform for building long-lived distributed applications.

It is a standalone platform — not merely an SDK and not a product application.

## Status

Early development. The repository currently provides the project layout and build integration only.

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

## Namespace

Root namespace: `apptraverse`.
