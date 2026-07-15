# AppTraverse

AppTraverse is a C++ application lifecycle platform for building long-lived distributed applications.

It is a standalone platform — not an SDK and not an application.

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

This configures the project and builds the `aether` dependency from [aethernetio/aether-client-cpp](https://github.com/aethernetio/aether-client-cpp).

## Layout

```
cmake/          CPM.cmake
include/        Public headers (namespace apptraverse)
src/            Library sources
tests/          Tests
examples/       Examples
docs/           Documentation
```

## Namespace

Root namespace: `apptraverse`.
