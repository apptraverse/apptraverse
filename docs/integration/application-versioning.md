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

## Stage 9A (this slice)

Implemented and tested:

```text
EmbeddedDistilledGraph → RamDomainStorage → LoadApplication (headless)
```

Files: `embedded_distilled_domain.h/.cpp`, `apptraverse_embedded_distilled_domain_test`.

Not in 9A: USER/BACKING overlay policy, release packages, active-release, production
startup changes, or edits to `examples/minimal_app` / WindowCatalog executables.

## Compatible mechanisms (both lines)

- Native `ObjPtr` and ClassId
- v0 object schemas
- `InitializeRuntimeNode`, historical `Node::base`
- Standard Æther storage (`RamDomainStorage`, domain load/save)
- `EmbeddedDistilledGraph` install into backing
- `ApplicationModelRuntime`, `ApplicationUiSession`, `WinApplicationHost`

## Requires separate integration tasks

**OverlayDomainStorage** — application branch: simple RAM overlay; versioning branch:
writable USER, BACKING, supply constants, dynamic layer comparison.

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

1. **9B** — USER/BACKING `OverlayDomainStorage` integration with supply constants.
2. Build-time export tooling (`distilled_domain_export`) wired into apps.
3. Minimal app / WindowCatalog embedded startup (replacing dev distill bootstrap).
4. Release packages, active-release selection.
5. Historical base / schema compatibility and acceptance matrix vs versioning PLAN.
