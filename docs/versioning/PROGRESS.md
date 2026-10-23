# AppTraverse — прогресс версионирования поставок

## Source of truth

1. Google Doc: [AppTraverse — версионирование состояния и жизненный цикл поставок](https://docs.google.com/document/d/196STRvU1QMbMu0ArGRLahPpgnTb6HQoldUV-7tOIcfo/edit) — **прямая сверка не выполнена** (403 при fetch).
2. Архитектурный контракт и roadmap: [PLAN.md](./PLAN.md).

**Branch:** `integration/serialization-surfaces`

**Baseline SHA (верифицированные fixtures / старт tracking):** `19d7da61954ddbab02ef098eec540ac17e936fcf`

**Текущий статус:** **PLANNED** — сквозная release-архитектура R0–R5, каталоги `state/R`, единый Release config и полный lifecycle **не реализованы**.

Общие правила разработки: [.cursor/rules/apptraverse-coding-agent.mdc](../../.cursor/rules/apptraverse-coding-agent.mdc). Корневой журнал других подсистем: [Progress.md](../../Progress.md).

---

## Уже подтверждённые механизмы (fixtures / подсистемы)

Это **не** PASS T01–T18 и **не** заявление о готовности Release R.

| Область | Где проверено | Примечание |
| --- | --- | --- |
| Window schema A/B/C, rollback, embedded/backing | `apptraverse_window_schema_layers_matrix`, [window_schema_layers.md](../../tests/window_schema_layers.md) | 12 сценариев PASS (отчёты в Progress.md) |
| `OverlayDomainStorage` + lazy graph | `model_persistence` A–E, overlay tests | USER vs BACKING, без копирования v1 в USER |
| Дистилляция + `Node::base` / journal + замена BACKING | `RunDistilledLazyOverlayTest` | Stage F, `force_materialize` |
| Сохранение истории после полной компактации | `model_persistence` F, post-compact `Save(Node)` | До первого SaveRoot |
| Event A/B rollback / replay | `apptraverse_event_version_matrix` | v0/v1 same ClassId |
| Барьер компактации на неизвестном v1 событии | `CanCompactStoredState`, matrix overlay | A оставляет journal=2 |
| Частичная компактация известного префикса | `known-prefix-newer-event` в matrix | base_tally=10, journal=2 |
| Холодная загрузка после компактации | `journal_retention_test` `TestZeroRetentionFiveHundred` | Два Domain, replay |

SHA последних связанных коммитов на baseline: `19d7da6` (partial compact test), `1676efa` (compaction invariants), `d3bbe2e` (CanCompactStoredState), `d80223f` (overlay roundtrip), `57838c2` / `4454340` (distilled lazy / force_materialize).

---

## Ещё не реализовано (целевая архитектура)

- Единый **Release R** и `active-release`
- Каталоги **`state/0` … `state/5`** как поставки констант
- Официальные **бинарные дельты** констант (offline → `state/R`)
- Единый контракт **const vs dynamic** (не через scope Ptr)
- **Метаданные** встроенной версии / R в EXE (single source of truth)
- Полный **lifecycle** R0→R5→R0→R5 в одном matrix
- Независимые тесты **отката** по T08–T10
- **T17:** новый S у исторического пользовательского base (ObjId от старой поставки)

---

## Итерации 01–14

| ID | Status | SHA | Tests | Notes |
| --- | --- | --- | --- | --- |
| 01 | TODO | — | — | Аудит кода vs PLAN |
| 02 | TODO | — | — | Имена каталогов, layout |
| 03 | TODO | — | — | Release config + EXE metadata |
| 04 | TODO | — | — | Const/dynamic контракт |
| 05 | TODO | — | — | Distill + lazy в EXE |
| 06 | TODO | — | — | Пустой USER, старт из EXE |
| 07 | TODO | — | — | Бинарный пакет констант |
| 08 | TODO | — | — | active-release, resolver |
| 09 | TODO | — | — | Пакет ≡ полный EXE |
| 10 | TODO | — | — | Новый S при USER |
| 11 | TODO | — | — | Historical base, replay |
| 12 | TODO | — | — | Logic-only + constants R3/R4 |
| 13 | TODO | — | — | R0→R5→R0→R5 matrix |
| 14 | TODO | — | — | Cleanup + интеграция |

Статусы: **TODO** / **IN_PROGRESS** / **PASS** / **FAIL** / **BLOCKED**.

- **PASS** теста ≠ реализованная функциональность ≠ принятие пользователем.
- Статус **ACCEPTED** здесь **не используется**.

---

## Iteration history

Записи добавляются только после **фактически выполненной** итерации кода/тестов (или документа с явным ID).

Формат:

- дата;
- ID итерации;
- что изменено;
- SHA;
- Debug/Release;
- PASS/FAIL;
- известные ограничения;
- push.

*(Пусто — итерации 01–14 ещё не начинались; baseline tracking создан вместе с PLAN/PROGRESS.)*
