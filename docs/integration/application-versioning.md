# Application Framework + Versioning integration

**Рабочая ветка:** **`integration/unified-application`**.

**Активный план (единственный):** [docs/versioning/PLAN.md §P0–P6](../versioning/PLAN.md#активный-план-мультиплатформенный-windowcatalog-p0p6). **Архитектурный канон:** [AppTraverse_Architecture_RU.md](../architecture/AppTraverse_Architecture_RU.md) (срез **`8c863bc`**). **Прогресс:** [PROGRESS.md](../versioning/PROGRESS.md).

**Цель (2026-10-28):** сначала библиотечный application template и controls на **семи backend** (Windows, Linux, macOS, Android, iOS/iPadOS, Web, Headless), затем WindowCatalog как тонкий потребитель на всех семи, затем администрирование. Независимый MinimalApp проверяет отсутствие зависимости библиотеки от каталога. **NEXT P3.T1** — воспроизводимый Æther/WASM patch; далее P3.T2/T3 → P4.C → A1. Прежний Web initial-render PASS не равен готовности template.

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
DirectoryDomainStorage (+ checkpoint sidecar)  ← переходная реализация (не целевой контракт)
          ↓
Model Domain
          ↓
ApplicationModelRuntime
          ↓
ApplicationUiSession (UI mirror)
          ↓
Platform presenters / host
```

**Целевая цепочка persistence:** embedded **BACKING** + **USER** (см. PLAN **P2**). Текущий **DirectoryDomainStorage + checkpoint sidecar** — переходный путь WindowCatalog на Windows; совместимость старых профилей после смены формата **EventRecord** (P1.1) не обещается — для разработки использовать **новый** state-dir.

Детали checkpoint/template — **P2** в PLAN; не дублировать канон здесь.

---

## Связанные документы

- WindowCatalog demo: [window_catalog_demo.md](../window_catalog_demo.md)
- Build notes: [window_catalog_build.md](../window_catalog_build.md)
- I0 inventory: [I0_cleanup_ledger.md](./I0_cleanup_ledger.md) (история; см. пометку P0 в шапке)
