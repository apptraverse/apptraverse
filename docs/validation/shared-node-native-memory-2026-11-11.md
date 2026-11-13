# SharedNode native serialization / memory — 2026-11-11

Status: first slice implemented and verified on Windows MSVC Debug.
Base: main `8b592f994c2ad2931bcef22a044a6a21dbe05c4e`.
Branch: `feature/shared-node-native-memory`.
Contract and subsequent stages: [SharedNode](../architecture/SharedNode.md).

## Change

An explicitly linked `apptraverse_shared_node` library provides persistent link
descriptors, event-sourced shared link topology, local peer/delivery metadata,
native graph export/parsing, and a deterministic bytes-only memory transport.
No dependency pins or Aether patches changed; no real network client is linked.

The generic `LocalPtr` network load consumes the empty source-local slot while
retaining the destination's local edge. Entire local peer collections are
behind one LocalPtr, excluding their contents and shape from the shared graph.
Link descriptors are independently identified Nodes so native historical replay
preserves aliases through multiple SharedNodes and local delivery metadata.

## Build identity

- Separate worktree under `Documents/Codex/2026-11-11/sharednode/apptraverse`.
- MSVC 19.50.35717, x64, Ninja, Debug, `/GR- /EHs-c-`, `_HAS_EXCEPTIONS=0`.
- Build directory: `build/shared-node-debug`.
- `BUILD_TESTING=ON`, `APPTRAVERSE_BUILD_EXAMPLES=OFF`, legacy examples OFF,
  evolution matrices OFF, `AE_NO_DEBUG_LOG=ON`.
- Existing local dependency sources reused. Final runs use aether-objects
  `79283b667cabf689da1059d618fe2a22abf51db4` with exactly main's 27 patches,
  from the previously verified `20261111T031551101092Z/tree` artifact. Config
  hash and all 12 final patch-touched source hashes match that artifact;
  five patch text comparisons differ only in CRLF/LF. An initial exploratory
  build used a newer cached 29-patch tree; final builds/tests were repeated
  against the correct 27-patch source. Shared cache sources were not changed.

## Executed tests

```sh
cmake --build build/shared-node-debug --target apptraverse_shared_node_headless_check
```

All **4/4** passed after the final fixture change:

| Test | Verified boundary |
| --- | --- |
| `apptraverse_network_local_ptr_test` | Sender-private concrete type excluded; fresh receiver local slots empty; known canonical instances retain a different receiver-local concrete type; inherited layers, cycles, aliases; ordinary cold local Save/Load |
| `apptraverse_memory_link_test` | Value-copied bytes, source UID, delay/drop/duplicate/out-of-order, three endpoints, Offline suppression, replacement endpoint, reentrant receive and callback destruction |
| `apptraverse_shared_node_memory_test` | Native snapshot and standalone application Join containing Bob + Link UID B; source destroyed before delivery; frozen state; no sender-private graph; shared aliases; local delivery survives earlier-event replay and cold load; independent per-peer/per-Node progress; local collection survives network refresh |
| `apptraverse_native_object_graph_serialization_test` | Existing independent native Event graph proof, now runnable without enabling historical GUI examples |

All **9/9** targeted regressions built and passed with the same dependencies:

```sh
ctest --test-dir build/shared-node-debug --output-on-failure -R '^apptraverse_(event_sourced_core|object_clone|historical_base_materialization|model_persistence|single_file_user_storage|object_scope_storage|application_contract_headless|window_catalog_headless|minimal_app_headless)_test$'
```

`git diff --check` passed. The toolchain emits existing exception-option override
warnings and a path-length configure warning for an unrelated, unbuilt long-name
test target; the selected build/test targets completed successfully.

## Limits

These are native graph/persistence and transport-stand proofs. There is no
implemented synchronization service, network ACK/retry, heartbeat timeout,
unknown-UID registry, client-version negotiation, initial coverage propagation,
late snapshot merge or cross-packet identity reconciliation. The per-peer
`MarkDelivered` API is local metadata, not proof of remote delivery. The memory
stand supplies availability observations directly. Fresh-Domain snapshot loads
do not prove safe import into an occupied independently allocated ID space.

The ordering proof uses distinct timestamps on one computer and verifies native
historical insertion/replay. It makes no equal-timestamp or cross-machine clock
claim. Automatic shared-history compaction remains blocked. GUI, Release,
other-platform runtime tests and real Aether transport were not run for this
headless-only slice.
