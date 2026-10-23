# AppTraverse — прогресс версионирования поставок

## Source of truth

1. Google Doc: [AppTraverse — версионирование состояния и жизненный цикл поставок](https://docs.google.com/document/d/196STRvU1QMbMu0ArGRLahPpgnTb6HQoldUV-7tOIcfo/edit) — **прямая сверка не выполнена** (403 при fetch).
2. Архитектурный контракт и roadmap: [PLAN.md](./PLAN.md).

**Branch:** `integration/serialization-surfaces`

**Baseline SHA (верифицированные fixtures / старт tracking):** `19d7da61954ddbab02ef098eec540ac17e936fcf`

**Текущий статус:** **PLANNED** — сквозная release-архитектура R0–R5, каталоги `state/R`, единый Release config и полный lifecycle **не реализованы**. Итерация **01** (аудит кода vs PLAN) **IN_PROGRESS** — завершена только часть **01A** (startup/distillation Surfaces Demo); части **01B** (storage/каталоги), **01C** (const/dynamic), **01D** (дубли) — позже.

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
| Частичная компактация известного префикса | `known-prefix-newer-event` в matrix | Фрагмент **T17**; base_tally=10, journal=2 |
| Холодная загрузка после компактации | `journal_retention_test` `TestZeroRetentionFiveHundred` | Фрагмент **T17**; два Domain, replay |

SHA последних связанных коммитов на baseline: `19d7da6` (partial compact test), `1676efa` (compaction invariants), `d3bbe2e` (CanCompactStoredState), `d80223f` (overlay roundtrip), `57838c2` / `4454340` (distilled lazy / force_materialize).

---

## Ещё не реализовано (целевая архитектура)

- Единый **Release R** и `active-release`
- Каталоги **`state/0` … `state/5`** как поставки констант
- Официальные **бинарные дельты** констант (offline → `state/R`)
- Единый контракт **const vs dynamic** (не через scope Ptr)
- **Метаданные** встроенной версии / R в EXE (single source of truth)
- Полный **lifecycle** R0→R5→R0→R5 в одном matrix (**T01–T08**, **T15** и связанные)
- Пошаговый **откат** и повторное обновление (**T07**, **T08**)
- Изменения пользователя после отката (**T09**, **T10**)
- **T11–T15:** чистые установки, эквивалентность EXE/пакетов, правила активной поставки, цикл констант A→B→A
- **T16:** новый динамический S у исторического пользовательского `Node::base` (ObjId от старой поставки)
- **T17:** полная матрица компактации журнала через откаты (fixtures покрывают только фрагменты)
- **T18:** несовместимый EXE/пакет или отсутствие обязательного слоя — явный отказ без порчи USER

---

## Итерация 01A — аудит Surfaces startup/distillation

**Область:** [`surfaces_lifecycle.cpp`](../../examples/surfaces_demo/common/surfaces_lifecycle.cpp), [`surfaces_distill.*`](../../examples/surfaces_demo/common/surfaces_distill.cpp), [`surfaces_v1_package.*`](../../examples/surfaces_demo/common/surfaces_v1_package.cpp), [`surfaces_evolution_ops.cpp`](../../examples/surfaces_demo/common/surfaces_evolution_ops.cpp), [`distill.h`](../../include/apptraverse/distill.h), Win32 targets в [`examples/surfaces_demo/windows/CMakeLists.txt`](../../examples/surfaces_demo/windows/CMakeLists.txt). **HEAD аудита:** `d782666`. **T01–T18:** не PASS.

**Факт (кратко):** начальный граф создаётся в USER через runtime `BuildSurfacesGraph` + `SaveDistilledRoot` при `state_missing` (если включена `APPTRAVERSE_ENABLE_DISTILLATION`); каталог — `--state-dir` / `session_.state_dir`. При уже существующем USER (revision B) перед `LoadApplication` в USER дописывают Surface v1 (`InstallBundledV1Layers`, затем `EnsureMissingSurfaceV1Layers`). Основной цикл и evolution ops используют только `DirectoryDomainStorage`; `OverlayDomainStorage` в Surfaces Demo **не** подключается. `win32_surfaces_demo_load_only` собран **без** `APPTRAVERSE_ENABLE_DISTILLATION` — bootstrap в lifecycle отсутствует.

| Файл / функция | Текущее поведение | Соответствие PLAN | Что делать позже | Защищающий тест |
| --- | --- | --- | --- | --- |
| `SurfacesModelSession::Run` — `state_missing` | `true`, если каталога нет или в нём нет перечисления классов у Application | INVESTIGATE | Согласовать с «пустой USER + embedded EXE» (ит. **06**); не путать отсутствие каталога с отсутствием поставки | — |
| `Run` — блок `APPTRAVERSE_ENABLE_DISTILLATION` | При `state_missing`: `BuildSurfacesGraph` → `FinalizeDistilledGraph` → `SaveDistilledRoot` в `state_dir` | REPLACE | Целевой старт: дистиллированный граф из EXE/backing, USER пуст или только пользовательские слои; dev-distill оставить за флагом/отдельной целью (ит. **05–06**) | `apptraverse_surfaces_demo_version_matrix` (`create` через EXE A — тот же distill-путь) |
| `FinalizeDistilledGraph` / `SaveDistilledRoot` ([`distill.h`](../../include/apptraverse/distill.h)) | **KEEP** (библиотека): `FinalizeDistilledGraph` инициализирует runtime `Node::base` без журнала; `SaveDistilledRoot` вызывает `Save()` через storage **Domain** объекта (диск только если Domain на `DirectoryDomainStorage`). **REPLACE** — bootstrap в Surfaces, который пишет начальное состояние прямо в пользовательский `state_dir`; полнота исходного графа — отдельная проверка, не гарантия `SaveDistilledRoot` | Offline/embedded distill + lazy load; не equate runtime bootstrap save с поставкой EXE | matrix `create`; GUI bootstrap с `APPTRAVERSE_ENABLE_DISTILLATION` |
| `BuildSurfacesGraph` ([`surfaces_distill.cpp`](../../examples/surfaces_demo/common/surfaces_distill.cpp)) | In-memory граф Application/Surface v0, presenters, localization | KEEP (dev) | Источник для embedded/backing; комментарий «production load never calls» уже верен | matrix `create` |
| `Run` — v1 при `!state_missing` (rev B) | После bootstrap **не** вызывается; при повторном запуске: install + ensure, затем load | REPLACE | Начальный S1 не должен материализоваться в USER из EXE-adjunct; см. PLAN «динамика из EXE» (ит. **10**) | matrix не покрывает auto second-start; `install-bundled` / lifecycle B — отдельно |
| `SurfacesDemoInstallBundledV1Layers` | Копирует файл слоя `Surface/1` из `surface_v1_package` (рядом с EXE или `APPTRAVERSE_SURFACE_V1_PACKAGE`) в `state_dir`, только если есть v0 и нет v1 | REPLACE | Пакет рядом с EXE — зачаток offline-поставки; не копировать начальные динамические слои в USER (ит. **07–08**) | matrix `prepare` (ручное копирование после `package` — эквивалент проверки v1 bytes); до удаления нужен embedded lifecycle |
| `SurfacesDemoEnsureMissingSurfaceV1Layers` | `LoadRoot` v0, задаёт `window_title_suffix` из runtime (` [demo-v1]` / ` [sN]`), `SaveVersion<1>` в USER | REPLACE | Противоречит правилу «начальные динамические слои в EXE, не synthesize в USER»; заменить embedded/backing + overlay skip (ит. **10**) | matrix `supplied-v1-layer`, `rollback` (suffix через реальный v1 на диске); evolution `set-suffix` / B load path |
| `RunSurfacesEvolutionOp` — `create` / `bootstrap` | Пустой каталог → тот же distill + save, JSON inspect | REPLACE (prod) / KEEP (fixture) | Matrix опирается на EXE A `create`; позже — empty USER + load from EXE | `apptraverse_surfaces_demo_version_matrix` |
| `RunSurfacesEvolutionOp` — `package` (EXE B) | Пишет только synthetic v1 layer в каталог (без v0) | KEEP | Аналог offline-генерации слоя до `state/R`; не путать с USER seed | matrix `prepare` (`package` + digest v1) |
| `RunSurfacesEvolutionOp` — `install-bundled` | Install + ensure без load session | REMOVE LATER | Дублирует lifecycle v1 seeding; удалить после embedded path | Нет в matrix; заменить сценарием embedded/T11 до удаления |
| `RunSurfacesEvolutionOp` — load path (B, не `inspect`) | Перед `LoadApplication`: install + ensure | REMOVE LATER | То же, что lifecycle B; убрать когда v1 только из backing | matrix `rollback` / `move` / `set-suffix` (ожидают v1 в USER после prepare) |
| `Run` — `DirectoryDomainStorage` + `LoadApplication` + shutdown `application.Save()` | Единственный storage в GUI lifecycle; save session на exit | KEEP | Позже overlay/backing для констант и lazy (PLAN fixtures; ит. **04**, **08**) | matrix + ручные demo runs |
| `OverlayDomainStorage` | Не используется в Surfaces Demo | KEEP (библиотека) | Подключить по PLAN для EXE/backing vs USER (не в scope 01A) | `model_persistence`, window/event matrices (не Surfaces) |
| `win32_surfaces_demo` vs `win32_surfaces_demo_load_only` | Полный demo: distill включён; load-only: v0 libs, **без** distill define | KEEP | Load-only — эталон «только load» до embedded bootstrap | INVESTIGATE — отдельный CTest при необходимости |
| POST_BUILD `surface_v1_package` → output dir | Копирует bundled v1 рядом с EXE B (если каталог есть в tree) | REMOVE LATER | Заменить embedded ресурсом EXE; в tree каталог может отсутствовать → install no-op, срабатывает ensure | Lifecycle B second start; `install-bundled` |

**Тесты vs старый v1-механизм ([`surfaces_demo_version_matrix.py`](../../tests/surfaces_demo_version_matrix.py)):** `prepare` создаёт v0 через EXE A `create` (distill), v1 — через EXE B `package` и **явное** `shutil.copyfile` в USER (не `install-bundled`). Проверяются отказ B без v1 (`MISSING_STATE_LAYER`), неизменность v0 при установке v1, rollback с раздельными v0/v1. **Нельзя** снять matrix без эквивалента: cross-process A/B, сохранение suffix в v1, move только в v0, отказ B без слоя. Операции `install-bundled` и auto install+ensure в lifecycle **не** покрыты matrix напрямую — при REPLACE нужны новые checks embedded/overlay.

---

## Итерация 01B — storage и каталоги

**Область (просмотрено на `0f46dec`):** [`directory_domain_storage.h`](../../include/apptraverse/directory_domain_storage.h), [`directory_domain_storage.cpp`](../../src/directory_domain_storage.cpp), [`overlay_domain_storage.h`](../../include/apptraverse/overlay_domain_storage.h), [`overlay_domain_storage.cpp`](../../src/overlay_domain_storage.cpp), [`directory_domain_storage_test.cpp`](../../tests/directory_domain_storage_test.cpp), [`overlay_domain_storage_test.cpp`](../../tests/overlay_domain_storage_test.cpp). **Не входило:** `PartitionedModelDomainStorage`, checkpoint pipeline, SharedNode, delivery, Surfaces lifecycle (01A). Сопоставление с [window_schema_layers.md](../../tests/window_schema_layers.md) — только по уже описанному там overlay/backing-покрытию (новый прогон не выполнялся).

**Файловый ключ:** [`DirectoryDomainStorageLayerPath`](../../src/directory_domain_storage.cpp) → `root / ObjId / ClassId / S` (имя файла — строка **S**, версия сериализации класса, **не** Release R; расширение не требуется). **`root`** задаёт вызывающий код (USER, temp, backing-каталог); один и тот же класс storage можно направить на разные корни. **`DirectoryDomainStorage` не выбирает активную поставку** и не обходит `state/0…5` — это будущий release-resolver (ит. **08**), не повод удалять низкоуровневый storage.

| Файл / метод | Фактическое поведение | KEEP / REPLACE / INVESTIGATE | Расхождение или недостающий механизм | Существующий тест |
| --- | --- | --- | --- | --- |
| `DirectoryDomainStorageLayerPath` | Ключ слоя: `root/ObjId/ClassId/S` | KEEP | Верхний `state/R` в PLAN — отдельные **root**-каталоги, не другой формат пути | Косвенно: все directory-тесты через `DirectoryDomainStorageLayerPath` |
| `DirectoryDomainStorage::Load` | Нет каталога объекта → `kEmpty`; **пустой** каталог объекта → `kRemoved`; нет/не открывается файл слоя → `kEmpty`; открытый файл (в т.ч. **нулевой размер**) → `kLoaded` | KEEP | Пустой **файл** слоя ≠ пустой **каталог** объекта (`kRemoved`) | Прямого теста `kRemoved` / пустого obj-dir в просмотренном наборе **нет** |
| `DirectoryDomainStorage::Enumerate` | ClassId под `root/ObjId/` (имена подкаталогов) | KEEP | Не Release R, не merge поставок | Косвенно roundtrip; overlay-тесты union |
| `CompareOnCloseDirectoryStorageWriter` | При закрытии writer сравнивает буфер с файлом по тому же ключу; при равенстве — skip write | KEEP | Не знает embedded/backing; только свой `root` | `TestUnchangedNonEmptyLayerSkipped` |
| `OverlayDomainStorage::Load` | Сначала writable; при `kLoaded` или **`kRemoved`** — stop (**без** fallback на backing); иначе backing | KEEP | `kRemoved` в overlay = tombstone пользователя | `TestOverlayPrefersOverlayAndFallsBackToBacking`; precedence — `TestSameClassVersionPrecedence` |
| `OverlayDomainStorage::Enumerate` | Union ClassId writable + backing | KEEP | Не выбор версии поставки R | `TestOverlayPrefersOverlayAndFallsBackToBacking` |
| `OverlayDomainStorage::Store` — нет USER-слоя | `BackingComparedStorageWriter`: если сериализованные байты = backing, **writable не создаётся** | KEEP | Не «в USER только diff от EXE» для уже материализованного override | `TestUnchangedBackingLayerIsNotMaterialized` |
| `OverlayDomainStorage::Store` — USER-слой уже есть | `Load` writable ≠ `kEmpty` → запись только в writable; совпадение с backing **не удаляет** USER-файл | KEEP | Возврат значения к backing: USER-слой **остаётся** (контракт теста, не Reset to Default) | `TestUnchangedBackingLayerIsNotMaterialized` (override остаётся после `{10,20}`) |
| `OverlayDomainStorage::Store` + `query.force_materialize` | Пропуск сравнения с backing; прямой `writable_overlay_.Store` | KEEP | Обязательная материализация (journal/base); в просмотренных `*_test.cpp` **нет** | [window_schema_layers.md](../../tests/window_schema_layers.md) / `model_persistence` (вне 01B) |
| `OverlayDomainStorage` (конструкторы) | Один **writable** + один **backing** (`RamDomainStorage` или `DirectoryDomainStorage` + backing) | KEEP | Нет цепочки `state/R`, active-release, фильтра «будущих» пакетов | `TestDurableVersionedOverlay`, `TestLazyGraphPreservesUnloadedChildren` |
| `DirectoryDomainStorage::Remove` / `CleanUp` | `Remove`: очистить содержимое obj-dir (или создать пустой → `kRemoved`); `CleanUp`: `remove_all(root)` + recreate | KEEP | Writable overlay; Directory **не** read-only — есть Store/Remove/CleanUp | Прямого теста Remove/CleanUp в просмотренном наборе **нет** |
| `OverlayDomainStorage::Remove` / `CleanUp` | Только **writable**; backing не мутируется | KEEP | «Overlay не пишет в backing» — да; backing может быть mutable `DirectoryDomainStorage` у вызывающего | `TestOverlayPrefersOverlayAndFallsBackToBacking` (`CleanUp` → fallback на backing) |

**Вывод:** **KEEP** — `DirectoryDomainStorage` + `OverlayDomainStorage` как пара «диск/RAM USER + backing», skip unchanged (directory compare-on-close; overlay backing-compare), приоритет load, `force_materialize`. **Ещё нет** для PLAN `state/R`: resolver активного R, `active-release`, граница EXE-embedded vs `state/R`, игнорирование неактивных/будущих пакетов (**T13–T14**). **Открыто (01C+):** const/dynamic политика в Store (методы не принимают отдельную политику). **Fixtures:** перечисленные C++ tests — проверки в исходнике; PASS в CI по [Progress.md](../../Progress.md) / window matrix — **новый прогон в 01B не выполнялся**.

---

## Итерации 01–14

| ID | Status | SHA | Tests | Notes |
| --- | --- | --- | --- | --- |
| 01 | IN_PROGRESS | — | — | **01A**, **01B** audit (doc); **01C**, **01D** TODO |
| 02 | TODO | — | — | Имена каталогов, layout |
| 03 | TODO | — | — | Release config + EXE metadata |
| 04 | TODO | — | — | Const/dynamic контракт |
| 05 | TODO | — | — | Distill + lazy в EXE |
| 06 | TODO | — | — | Пустой USER, старт из EXE |
| 07 | TODO | — | — | Бинарный пакет констант |
| 08 | TODO | — | — | active-release, resolver |
| 09 | TODO | — | — | Пакет ≡ полный EXE |
| 10 | TODO | — | — | Новый S при USER |
| 11 | TODO | — | — | **T16** historical base; **T17** compaction/replay |
| 12 | TODO | — | — | Logic-only + constants R3/R4 |
| 13 | TODO | — | — | **T01–T10**, **T15** lifecycle matrix |
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

- **2026-10-23** — doc-only: нумерация **T01–T18** в [PLAN.md](./PLAN.md) / PROGRESS синхронизирована с исходным архитектурным Doc; итерации 01–14 **TODO**; статус **PLANNED**; тесты не запускались (SHA — commit этого изменения).
- **2026-10-23** — **01A:** аудит Surfaces startup/distillation → раздел в PROGRESS; итерация **01 IN_PROGRESS**; код не менялся; тесты не запускались (SHA — commit этого изменения).
- **2026-10-23** — **01B:** аудит `DirectoryDomainStorage` / `OverlayDomainStorage`; код просмотрен на **`0f46dec`**; doc-only commit отдельно; тесты не запускались.

*(Baseline tracking: PLAN/PROGRESS на `19d7da6`; итерация 01 код/тестов не менялась — только 01A doc audit.)*
