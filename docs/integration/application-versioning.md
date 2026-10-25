# Application Framework + Versioning integration

This branch combines the working application platform from
`cursor/surfaces-library-controls` with release/versioning mechanisms from
`integration/serialization-surfaces`, without merging those branches wholesale.

## Source baselines (fixed for planning)

| Line | Branch | SHA (start of integration) |
|------|--------|----------------------------|
| Application framework | `cursor/surfaces-library-controls` | `55c4d539c9454b0df3768feb938aea466092f09f` |
| Versioning mechanisms | `integration/serialization-surfaces` | `17b7830ae397871ee90e31dc65feb82cfdfc6716` |

Integration work lives on **`integration/application-versioning`** only.

## Target runtime chain (full)

```text
Build-time distilled graph
          ↓
EmbeddedDistilledGraph
          ↓
RamDomainStorage (BACKING)
          ↓
OverlayDomainStorage (USER / BACKING)
          ↓
Model Domain
          ↓
ApplicationModelRuntime
          ↓
ApplicationUiSession
          ↓
WinApplicationHost
```

## Stage 9A — DONE

```text
EmbeddedDistilledGraph → RamDomainStorage → LoadApplication (headless)
```

Files: `embedded_distilled_domain.h/.cpp`, `apptraverse_embedded_distilled_domain_test`.

## Stage 9B1 — DONE (storage primitive)

`OverlayDomainStorage` with internal or external USER storage, BACKING, supply
constant ObjIds, dynamic skip-unchanged Store, and supply-constant discard Store.

CTest: `apptraverse_overlay_user_backing_test`.

Not in 9B1: `force_materialize` / historical dynamic base materialization (**9B2**),
build-time export (**9C**), application startup on embedded release, release packages,
active-release, T01–T18 matrix. Examples still use `DirectoryDomainStorage` only.

## Compatible mechanisms (both lines)

- Native `ObjPtr` and ClassId
- v0 object schemas
- `InitializeRuntimeNode`, historical `Node::base`
- Standard Æther storage (`RamDomainStorage`, domain load/save)
- `EmbeddedDistilledGraph` install into backing
- `ApplicationModelRuntime`, `ApplicationUiSession`, `WinApplicationHost`

## Requires separate integration tasks

**OverlayDomainStorage (9B1 landed)** — USER/BACKING/supply-constant policy ported;
**9B2** still needs Æther `force_materialize` patch before historical dynamic base
Save paths match versioning branch.

**Node / EventFor** — versioning adds historical-base semantics, event-layer
compatibility, compaction on rollback; must not overwrite application `Node`
without regression work.

**Æther CPM patches** — application branch: `aether-objects-objptr-reset-flags.patch`.
Versioning adds `reject-unknown-stored-class`, `force-materialize`,
`should-save-version`; joint application needs a conflict audit.

**Storage policy** — application line keeps trusted prototype storage (no automatic
recovery, integrity telemetry, or temp-file staging from legacy versioning).

**Release R / T01–T18** — versioning headless fixtures for embedded release, supply
packages, active-release, USER/BACKING, dynamic schema layers; full matrix not PASS
on either line alone. Import incrementally, not in one commit.

## Next steps (planned)

1. **9B2** — `aether-objects-force-materialize.patch` + historical dynamic base Save.
2. **9C** — build-time export tooling (`distilled_domain_export`).
3. Minimal app / WindowCatalog embedded startup (replacing dev distill bootstrap).
4. Release packages, active-release selection.
5. Historical base / schema compatibility and acceptance matrix vs versioning PLAN.
