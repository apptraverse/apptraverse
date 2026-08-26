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

## Namespace

Root namespace: `apptraverse`.
