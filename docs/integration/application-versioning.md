# Application Framework + Versioning integration

**Рабочая ветка:** **`integration/unified-application`**.

**Активный план (единственный):** [docs/versioning/PLAN.md §P0–P6](../versioning/PLAN.md#активный-план-мультиплатформенный-windowcatalog-p0p6). **Архитектурный канон:** [AppTraverse_Architecture_RU.md](../architecture/AppTraverse_Architecture_RU.md) (срез **`8c863bc`**). **Прогресс:** [PROGRESS.md](../versioning/PROGRESS.md).

**Цель:** один **WindowCatalog** на шести платформах через публичные headers/CMake AppTraverse; прикладная модель остаётся в примере.

---

## Исторический merge (baseline)

| Event | SHA / notes |
| --- | --- |
| Merge serialization-surfaces + application-versioning | **`4848402`** |
| **U1** catalog model test | **`8835d26`** |
| **U2** manual Windows catalog | **manual PASS** (Add/Open/Delete, EN/RU, geometry, restart) |
| **U3** doc sync | doc-only |
| **U4** architecture audit | [unified-architecture-audit.md](./unified-architecture-audit.md) (срез `53245f0`; рекомендации U5.2+ **не** активная очередь) |
| **U5.1** shutdown-save exit code | **`8c863bc`** |

**U5.2–U6**, **9D–9H** как последовательность исполнения **сняты** (2026-10-27, **P0**). Смысл перенесён в **P1–P6** (см. PLAN).

**I0** ledger и **9A–9C** fixtures — историческая инвентаризация; **SharedNode** в I0 «DEFERRED» **отменено** → **полное удаление** в **P1.1**.

---

## Target runtime chain (контракт)

```text
Build-time distilled graph
          ↓
EmbeddedDistilledGraph / Directory bootstrap (переходный)
          ↓
DirectoryDomainStorage (+ checkpoint sidecar)
          ↓
Model Domain
          ↓
ApplicationModelRuntime
          ↓
ApplicationUiSession (UI mirror)
          ↓
Platform presenters / host
```

Детали persistence, checkpoint и template — **P2** в PLAN; не дублировать канон здесь.

---

## Связанные документы

- WindowCatalog demo: [window_catalog_demo.md](../window_catalog_demo.md)
- Build notes: [window_catalog_build.md](../window_catalog_build.md)
- I0 inventory: [I0_cleanup_ledger.md](./I0_cleanup_ledger.md) (история; см. пометку P0 в шапке)
