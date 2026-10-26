# AppTraverse — прогресс версионирования поставок

## Source of truth

1. Google Doc: [AppTraverse — версионирование состояния и жизненный цикл поставок](https://docs.google.com/document/d/196STRvU1QMbMu0ArGRLahPpgnTb6HQoldUV-7tOIcfo/edit) — **прямая сверка не выполнена** (403 при fetch).
2. Архитектурный контракт и roadmap: [PLAN.md](./PLAN.md) — **AppTraverse Libraries Architecture RU v0.3 (25.10.2026)** + Object Core **`01_Object_Core_Versioning_PLAN.md` (25.10.2026)** ([share](https://chatgpt.com/library/share/libfile_c97b08ed7af88191a916944ac06566f7?account_id=personal)).

**Branch:** `integration/serialization-surfaces` (параллельно: **`integration/application-versioning`** — UI/host; см. [PLAN §координация](./PLAN.md#координация-с-integrationapplication-versioning)).

**Подтверждённый HEAD (код):** **`df536e6`** (C0.2.2); roadmap docs — **`9e91f4d`** (C0.3 PASS).

**Baseline SHA (старт tracking fixtures):** `19d7da61954ddbab02ef098eec540ac17e936fcf`

**Порядок работ:** **C0.1** → **C0.2** → **C0.3** → **12R** → **12B** → **13** → **14** → **15** → **16**; **17–22** — roadmap ([PLAN §порядок](./PLAN.md#порядок-работ-после-11)).

**Текущий этап:** **12B IN_PROGRESS** (**12B.1 PASS**); **12B.2** — disk/process cold roundtrip **NEXT**. **12R PASS**. **C0 PASS**. **11** **IN_PROGRESS** (**11A**, **11B.2 PASS**; **11B.1** — исторический дефект). **01–10** **PASS** в ранее объявленном scope. **12A** — документ выполнен (`ed5eef9`/`4bda0ad`); lazy **`ObjPtr`** origin — **12R PASS** (`9981e0f`). **13–22** — **не** реализованы. **T16/T17** — фрагменты.

**OPEN:** динамический источник Clone **без** устойчивого const **P** — явное решение, не silent fallback ([PLAN §13](./PLAN.md#13--универсальный-clone)).

**Нормативные правила Object Core:** [PLAN §нормативные правила](./PLAN.md#нормативные-правила-object-core-v03--01-plan) (здесь не дублируются).

**Fixture vs библиотека:** PASS prefab/overlay matrices **не** означает системный origin/Clone/S — origin в fixture **`PrefabPtr`**, Clone вручную.

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

- Единый **Release R** и `active-release` *(имена layout — **02 PASS** doc; resolver — **08**)*
- Реализация на диске **`state/user`**, **`state/<R>/`**, **`state/active-release`** *(решение **02**; миграция Surfaces `--state-dir` — **06**+)*
- Официальные **бинарные дельты** констант (offline → `state/R`)
- **04 PASS:** storage `supply_constant_ids` + dev-time Event review [PLAN §04](./PLAN.md#решение-итерации-04--контракт-const-vs-dynamic); без runtime const checks; факты — [01C](#итерация-01c--константность-и-ссылки)
- **Метаданные** R в production Surfaces EXE и полный embedded graph *(probe + CMake generate — **03 PASS**; Surfaces wiring — **05+**)*
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

## Итерация 01C — константность и ссылки

**Область (код просмотрен на `1b0e9cb`):** [`object_link.h`](../../include/apptraverse/object_link.h), [`ui_string.h`](../../include/apptraverse/ui/ui_string.h), [`ui_localization_string.h`](../../include/apptraverse/ui/ui_localization_string.h) / [`.cpp`](../../src/ui/ui_localization_string.cpp), [`ui_locale_settings.h`](../../include/apptraverse/ui/ui_locale_settings.h) / [`.cpp`](../../src/ui/ui_locale_settings.cpp), [`surfaces_localization.*`](../../src/surfaces/surfaces_localization.cpp), поля [`Application`](../../include/apptraverse/surfaces/surfaces_model.h); **aether-objects** pin [`79283b6`](../../cmake/aether_version.cmake) — `obj/obj_id.h`, `obj/obj_ptr.h`, `obj/obj_ptr_base.cpp` (с project patches, без обновления deps). **Не входило:** SharedNode, publication/checkpoint, PartitionedModelDomainStorage. **Storage (01B):** overlay skip ≠ запрет записи констант — см. [01B](#итерация-01b--storage-и-каталоги). **T01–T18:** не PASS.

**Цепочка локализации:** [`InstallSurfacesDemoLocalization`](../../src/surfaces/surfaces_localization.cpp) создаёт `UiLocaleSettings` и четыре `UiLocalizationString` (`label_add`, `label_close`, `window_title_prefix`, `language_switch_label`) как **`ae::ObjPtr<…>`** на `Application` (Load/Save через `dnv`). `MakeLocalizedString` — `UiLocalizationString::Create` + `InitializeRuntimeNode(*localized, surfaces)`; два `UiString::Create` **без** `with_id()` → **`ObjId::GenerateUnique()`** на каждый объект; `utf8` задаётся в runtime. `Resolve` читает `strings[i]->utf8`. [`SetUiLocalizationSlotUtf8Event`](../../src/ui/ui_localization_string.cpp) меняет `strings[index]->utf8` и `NoteMaterializedChange()` — **изменяемая** модель текстов, не immutable-поставка. **`UiLocaleSettings`:** поле `language_index_` + `SetUiLanguageIndexEvent` / `SubmitLanguageIndexFromUi` — **динамика пользователя** (язык UI), отдельно от текстов перевода.

| Тип / функция | Фактическое поведение | KEEP / REPLACE / INVESTIGATE | Расхождение с целью «константа поставки» | Существующий тест |
| --- | --- | --- | --- | --- |
| `ae::ObjPtr<T>::operator->() const&` (aether-objects) | Возвращает **`T*`**, не `T const*`; const на ObjPtr не делает referent read-only | INVESTIGATE | Const на handle ≠ immutable объекта поставки | Прямого теста const-semantics **нет** |
| `ObjectLink::operator->() const` ([`object_link.h`](../../include/apptraverse/object_link.h)) | Делегирует в `ObjPtr`; **`T*`** | INVESTIGATE | То же; `SharedPtr`/`LocalPtr` — только **scope** сериализации (local vs network shared), не const/dynamic | Прямого теста **нет** |
| `ObjFlags::kUnloadedByDefault` (aether-objects `obj/obj_id.h`, pin [`79283b6`](../../cmake/aether_version.cmake)) | Lazy load; не блокирует Save/изменение после Load | KEEP | Не признак константности (PLAN **D**) | `TestLazyGraphPreservesUnloadedChildren` (overlay, 01B) |
| `Application` → `UiLocalizationString` / `UiString` | Обычные изменяемые узлы графа; SaveRoot сохраняет **загруженные** referent'ы через `Serializer<ObjectPtrBase>` | REPLACE (роль) | Тексты в demo — «поставка приложения», но технически те же mutable объекты в USER при Save | `surfaces_model_test` `TestInitialGraph`; distill save — косвенно |
| `MakeLocalizedString` / `ObjId` | Без `with_id()` — новые ID при каждой дистилляции | INVESTIGATE | Идентичность «одной и той же» константы между независимыми EXE/distill **не** зафиксирована | Прямого cross-distill ID теста **нет** |
| `SetUiLocalizationSlotUtf8Event` / `Apply` | Пишет в `UiString::utf8` | KEEP (как есть) | Доказывает **изменяемость** строк; не immutable contract | Прямого теста события **нет** |
| `UiLocaleSettings` + `SetUiLanguageIndexEvent` | Меняет `language_index_`, persist через Save | KEEP | Пользовательский выбор языка — dynamic state (PLAN) | `TestLocaleCommitAndPersistence`, `TestLanguageSwitchPublication` в [`surfaces_model_test.cpp`](../../tests/surfaces_model_test.cpp) |
| `Serializer<ObjectPtrBase>` / `ObjectPtrBase::Save` | Wire: **ObjId + flags**; если `cached_` — **`SaveRoot` referent**; unloaded без cache — referent не сериализуется; **нет** проверки immutable | KEEP (механизм) | Нет запрета записи «константного» referent в USER; mutable alias на тот же ObjId не рассматривается | [`presenter_graph_persistence_test.cpp`](../../tests/presenter_graph_persistence_test.cpp) (граф с locale/title); не тест «запрет USER» |
| `ObjectLink` Serializer (local persistent) | Как `ObjPtr`, кроме `kLocal` + `NetworkShared` → пустая ссылка на wire | KEEP | Не const/dynamic policy | Прямого ObjectLink persistence теста **нет** |
| Признак «константа поставки» в просмотренном пути | **Не найдено** (нет flag/API «не писать в USER», «только backing» на уровне Obj/UiString) | INVESTIGATE | Цель PLAN: объект не меняется пользователем, не в USER, обновляется поставкой — **контракт отсутствует** | Теста «запрет записи констант в USER» **нет** |

**Открытые решения (итерация 04, без выбора сейчас):** как помечать константный объект; поведение при mutable-алиасе на тот же `ObjId`; сохраняется ли ссылка в графе, если referent запрещено писать в USER; разделение текстов поставки vs `UiLocaleSettings`; стабильные `ObjId` / байтовая идентичность констант между поставками и EXE+`state/R`.

**Вывод:** **KEEP** — `ObjPtr`/serialization, scope `SharedPtr`/`LocalPtr`, изменяемые `UiString` там, где нужна runtime-локализация; **REPLACE/INVESTIGATE** — **роль** полей Application как «константы поставки» при текущем persist в USER. Immutable не выводить из overlay skip (01B).

---

## Итерация 01D — избыточность и порядок очистки

**Область (срез аудита `7dd187f`, опирается на [01A](#итерация-01a--аудит-surfaces-startupdistillation)–[01C](#итерация-01c--константность-и-ссылки)):** [`apptraverse.cpp`](../../src/apptraverse.cpp), [`surfaces_registration.*`](../../src/surfaces/surfaces_registration.cpp), [`surfaces_lifecycle.cpp`](../../examples/surfaces_demo/common/surfaces_lifecycle.cpp), [`surfaces_evolution_ops.cpp`](../../examples/surfaces_demo/common/surfaces_evolution_ops.cpp), [`surfaces_v1_package.cpp`](../../examples/surfaces_demo/common/surfaces_v1_package.cpp), [`overlay_domain_storage.cpp`](../../src/overlay_domain_storage.cpp). **T01–T18:** не PASS.

**Параллельные пути подготовки USER (Surfaces):** (1) runtime bootstrap — `BuildSurfacesGraph` + `SaveDistilledRoot` при `state_missing` ([01A](#итерация-01a--аудит-surfaces-startupdistillation)); (2) копирование bundled v1 — `SurfacesDemoInstallBundledV1Layers`; (3) синтез v1 — `SurfacesDemoEnsureMissingSurfaceV1Layers`; (4) evolution `install-bundled`; (5) fixtures — evolution `create` / `package` + matrix `prepare`. Dev-distill и fixture `package` **KEEP** как dev/offline; **REPLACE** использование (2–4) для начальных динамических слоёв в USER — после embedded/backing (**ит. 06**, **10**, **14**).

| Файл / символ | Доказательство | KEEP / REMOVE LATER / REPLACE / INVESTIGATE | Условие изменения | Защищающий тест |
| --- | --- | --- | --- | --- |
| `EnsureObjectRegistration` ([`apptraverse.cpp`](../../src/apptraverse.cpp)) | Линковка core/UI registrars | **KEEP** | Не удалять по имени | Много CTest entrypoints; `journal_retention_test` и др. |
| `EnsureSurfacesRegistration` | `RegisterLiveGraphEdgeAppender` + touch surfaces registrars + presenter regs | **KEEP** | Нужен для live-graph edges и surfaces classes | `surfaces_model_test`, smoke tests |
| `EnsureSurfacesModelRegistration` | Alias в header; **вызовов в repo не найдено** (кроме inline) | **INVESTIGATE** | Удалить alias после grep=0 и согласования demo/tests | Прямой проверки alias **нет** |
| `EnsureSurfacesRegistration` в `SurfacesDemoEnsureMissingSurfaceV1Layers` | Повтор после `RunSurfacesEvolutionOp` (стр. 101–102) или после `SurfacesModelSession::Run` (стр. 66) | **INVESTIGATE** | Убрать только если доказано, что appender/registrars уже инициализированы в **той же** цепочке; иначе риск отдельного entry | `apptraverse_surfaces_demo_version_matrix`; evolution ops |
| `EnsureSurfacesRegistration` в demo `main` + в `Run` / evolution | Независимые entry: `main` → Object+Surfaces, session только Surfaces | **KEEP** | Разные точки входа, не дубль в одном стеке | Platform smoke tests |
| `SurfacesDemoInstallBundledV1Layers` + lifecycle/evolution auto-call | Дублирует смысл offline v1 в USER ([01A](#итерация-01a--аудит-surfaces-startupdistillation)) | **REMOVE LATER** | После embedded v1/backing (**10**, **06**); matrix эквивалент | `surfaces_demo_version_matrix` `prepare` / `rollback` |
| `SurfacesDemoEnsureMissingSurfaceV1Layers` | Runtime `SaveVersion<1>` defaults в USER | **REMOVE LATER** | То же; замена embedded + overlay | matrix + B lifecycle second-start |
| `RunSurfacesEvolutionOp` — `install-bundled` | Обёртка install+ensure без session | **REMOVE LATER** | После embedded path (**14**) | Прямой CTest **нет**; matrix не вызывает |
| `BackingComparedStorageWriter::failed_` | Инициал `false`; проверки в `Write`/dtor; **присваивания `true` нет** | **INVESTIGATE** | Локально убрать поле/ветки после подтверждения в коде; не трогать compare logic | `TestUnchangedBackingLayerIsNotMaterialized` (overlay) |
| `FailIfWriteError` ([`overlay_domain_storage.cpp`](../../src/overlay_domain_storage.cpp)) | Один вызов из dtor `BackingComparedStorageWriter` | **INVESTIGATE** | Inline `abort` в dtor — косметика | Тот же overlay test |
| `ReadLoadedLayerBytes` | Вызывается только после `backing_load.result == kLoaded`; read error → `abort`; пустой vector = legit empty layer | **KEEP** | Не путать с ошибкой; Store не вызывает при non-kLoaded | `TestUnchangedBackingLayerIsNotMaterialized`, `TestDurableVersionedOverlay` |
| `BackingComparedStorageWriter` / overlay compare | Отдельно от Directory compare-on-close ([01B](#итерация-01b--storage-и-каталоги)) | **KEEP** | `force_materialize`, post-compact `Save(Node)` — **KEEP** ([01B](#итерация-01b--storage-и-каталоги), `model_persistence` F) | overlay + window/event matrices |

**Порядок очистки:** локально — `failed_` / `FailIfWriteError` (**INVESTIGATE**, без изменения контракта overlay). Архитектурно — v1 USER seeding (**REMOVE LATER** после **06/10/14**). Открыто в **02–04:** layout `state/R`, const/dynamic policy, stable ObjId (см. [01C](#итерация-01c--константность-и-ссылки)).

---

## Прототип-first (v0.3, кратко)

Happy path и семантические инварианты — сейчас; CRC/signatures/defensive parsing/malformed tests/recovery/managers — **не** в scope до **22**. Подробности: [PLAN §принцип v0.3](./PLAN.md#принцип-разработки-v03).

---

## C0.1 — аудит избыточности Object Core

**Срез кода:** **`b6dcdae`** (код **`66515d8`**). Doc-only; CTest не запускались. Источник требований: [PLAN §C0](./PLAN.md#c0--очистка-архитектуры-до-новой-функциональности), **`01_Object_Core_Versioning_PLAN.md` (25.10.2026)**.

**Итог по классам:** **KEEP 18** · **REMOVE 1** · **SIMPLIFY 2** · **DEFER 6** (см. таблицу).

| Файл / функция | Что сейчас делает | Почему избыточно или необходимо | Решение | Защищающий тест |
| --- | --- | --- | --- | --- |
| `tests/prefab_ptr.h` — `PrefabPtr<T>` + `Serializer` | Test-only wire: `prefab_id` + instance `ObjPtr`; origin fixture | Замена — системный origin (**12R–14**); в production **запрещён** | **DEFER** (удаление после библиотечной замены) | `apptraverse_prefab_release_matrix`, `prefab_dynamic_layer_roundtrip_matrix`, `prefab_late_instance_matrix` |
| `prefab_release_model.cpp` — `CloneWorkspaceFromPrefabs` | Ручная сборка `WorkspaceRoot`, фиксированные ObjId, `PrefabPtr::FromInstance`, дублирует defaults prefab | Не универсальный Clone; **нужен** до **13** | **DEFER** (замена **13**); до тогда **KEEP** | `apptraverse_prefab_release_matrix` (`CloneWorkspaceFromPrefabs(domain, false)`) |
| ~~`PlanCloneFromPrefabGraph` / `ClonePlan`~~ | *(удалено **C0.2.1**)* | — | **DONE (C0.2.1)** | prefab matrices 8/8 Debug+Release |
| `MaterializePrefabBackedWindow` | Load instance; при R≥2 и отсутствии USER S1 подставляет `posx` из BACKING | Явная **fixture**-семантика const prefab, не системный S (**14**); не маскирует required graph | **KEEP** (fixture); миграция **DEFER** → **14** | `prefab_dynamic_layer_roundtrip_matrix`, lifecycle matrix |
| `EnsurePrefabVersionInHistoricalBase` | Пишет S1 в historical `Node::base` из backing prefab | Дублирует будущий historical-base/**T16**; **нужен** для 06B/10E fixture | **DEFER** (системный уровень **14**); **KEEP** сейчас | `apptraverse_prefab_release_matrix`, compaction matrices |
| `CreateToolWindowInstanceFromBacking` | Ручной clone tool window + `PrefabPtr` + `Node::base` | Аналог **13** для late WindowC | **DEFER**; **KEEP** | `apptraverse_prefab_late_instance_matrix` |
| `ValidateWorkspaceCanonicalPrefabPtrs` | Проверка согласованности instance↔prefab на workspace | Контракт fixture origin без production registry | **KEEP** | `apptraverse_prefab_release_matrix` (internal check) |
| `EnsurePrefabReleaseRegistration` | `EnsureObjectRegistration` + touch prefab registrars | Реальные entrypoints без registrars падают | **KEEP** ([PLAN §C0.3](./PLAN.md#c0--очистка-архитектуры-до-новой-функциональности)) | Все `apptraverse_prefab_release_*` CTest |
| `PrefabWindow::ShouldSaveVersion(V1)` | Пропуск пустого USER S1 (**10E**) | Основание release fixture | **KEEP** | `prefab_dynamic_layer_roundtrip_matrix`, overlay 0-byte Event |
| `PrefabWindow` — `user_owned_s0/s1` default `false` | Явные persisted flags в S0/S1 | Не скрывают обязательные поля; runtime `width/posx` инициализируются в clone path | **KEEP** | Dynamic layer / rollback matrices |
| `include/apptraverse/object_link.h` — `ObjectLink`, `LocalPtr`, `SharedPtr` | Scope на рёбрах; Serializer: Local → empty при `NetworkShared`; visitor skip Local | Не дубль без семантики: **Node::base**, SharedNode sync, network exclusion | **KEEP** | `apptraverse_closed_event_graph_test`, `apptraverse_shared_node_foundation_test` |
| `object_link.h` — `HandlePointerSeri` path | OperationStorage remap/validate для publication | Отдельный от «просто ObjPtr» путь clone/remap | **KEEP** | `closed_event_graph_test`, presenter persistence |
| `distilled_supply_package.cpp` — wire + diff | `BinaryArchive`: `(ObjId, ClassId, S, bytes)` + `target_supply_constant_ids`; diff по storage map | Допустимый **индекс слоёв**, не семантические межобъектные ссылки; APSP удалён | **KEEP** | `apptraverse_prefab_release_lifecycle_matrix`, supply split / equivalence |
| `distilled_domain_export.cpp` — `WriteEmbeddedDistilledSupplyHeader` | Экспорт тех же ключей слоёв в embedded header | Параллельный `LayerKey` с package — косметический DRY, не второй wire | **KEEP**; опциональный DRY — **DEFER** (низкий приоритет) | `apptraverse_*_distilled_embedded_matrix` |
| `overlay_domain_storage.cpp` — `BackingComparedStorageWriter` | Skip USER write если байты = backing; `force_materialize` → прямой USER | USER-over-backing (**04**); легитимные 0-byte Event | **KEEP** | `apptraverse_overlay_domain_storage_test` |
| ~~`overlay_domain_storage.cpp` — `failed_`~~ | *(удалено **C0.2.2**)* | — | **DONE (C0.2.2)** | `apptraverse_overlay_domain_storage_test` + persistence + prefab regressions |
| `overlay_domain_storage.cpp` — `SupplyConstantDiscardWriter` | Discard Store для `supply_constant_ids` | Const не в USER | **KEEP** | `overlay_domain_storage_test` supply-constant cases |
| `node.h` — `CanCompactStoredObjectVersions` | Блок компактации при unknown newer S на объекте/base chain | Guard **11B.2** | **KEEP** | `apptraverse_prefab_old_exe_compaction_guard_matrix` |
| `node.h` — `Save(V0)` + `force_materialize` для historical `base` | Материализует base при LocalPersistent + непустой journal | Journal/base replay (**10E**, compaction) | **KEEP** | `model_persistence`, compaction matrices |
| `node.h` — `CompactJournalInto` guard + `force_materialize` | Компактация с проверкой stored S | **11B.2** | **KEEP** | guard + compaction roundtrip matrices |
| `event_for.h` — `CanCompactStoredState` | Делегирует в `CanCompactStoredObjectVersions<ConcreteEvent>` | Единый guard для Event-слоёв | **KEEP** | `apptraverse_prefab_old_exe_compaction_guard_matrix` |
| `cmake/patches/aether-objects-should-save-version.patch` | `ShouldSaveVersion` в `DomainGraph::Save` | **10E** root-save | **KEEP** | prefab + overlay tests (**10E**) |
| `cmake/patches/aether-objects-force-materialize.patch` | `DomainQuery::force_materialize`, graph flag | Overlay historical base / post-compact | **KEEP** | `model_persistence`, overlay tests |
| `cmake/patches/aether-objects-local-persistent-load.patch` | `LoadStoredLayers` per facet | Многослойный USER/BACKING | **KEEP** | window schema + prefab matrices |
| `cmake/patches/aether-objects-reject-unknown-stored-class.patch` | Fail-closed factory при unknown stored class | T18 fixture semantics | **KEEP** | `apptraverse_prefab_unknown_*`, journal unknown paths |
| `cmake/patches/aether-objects-domain-storage-access.patch` | `Domain::domain_storage()` | `EventFor` / compaction guard читает storage | **KEEP** | compaction guard matrix |

**Не найдено** доказанных безопасных **REMOVE** в production `src/`/`include/` (кроме test-only dead API выше). Массовая очистка fixture origin/clone **не** планируется до **12R–14**.

### C0.2 — упорядоченные минимальные изменения (**PASS**)

1. ~~**C0.2.1 PASS** — dead `ClonePlan` API~~ ([история](#iteration-history)).
2. ~~**C0.2.2 PASS** — `BackingComparedStorageWriter::failed_`~~ ([история](#iteration-history)).
3. **STOP:** дальнейшие C0.2 пункты **не** планировать без нового аудита после **12R**/**13** (origin/Clone заменят fixture API).

**Следующий шаг:** **12B.2** (disk/process cold roundtrip) — [PLAN §12B](./PLAN.md#12b--origin-roundtrip-12b1-pass-12b-todo-do-diskprocess).

---

## C0.3 — верификация сохранённых механизмов (**PASS**)

**Срез кода:** **`df536e6`**. Doc-only; **src/tests/CMake не менялись.**

**Регрессии (CTest, фактический прогон):**

| Конфигурация | Результат |
| --- | --- |
| **Debug** (`win64-ninja-msvc-debug`) | **17/17 PASS** — overlay, model_persistence×2, prefab×9, closed_event_graph, shared_node_foundation, lazy/surfaces distilled embedded |
| **Release** (`win64-ninja-msvc-release`) | **14/14 PASS** — prefab×9, model_persistence×2, lazy/surfaces distilled embedded, active-release |

**Примечание Release:** в дереве `win64-ninja-msvc-release` **не зарегистрированы** CTest `apptraverse_overlay_domain_storage_test`, `apptraverse_closed_event_graph_test`, `apptraverse_shared_node_foundation_test` (инвентарь сборки, не падение тестов). Инварианты overlay / ObjectLink / SharedNode для Release подтверждены тем же кодом **`df536e6`** через **Debug** CTest выше.

| Invariant | Implementation (библиотека / fixture) | Regression (CTest) | KEEP / DEFER |
| --- | --- | --- | --- |
| Регистрация классов | `EnsureObjectRegistration` ([`apptraverse.cpp`](../../src/apptraverse.cpp)); fixture `EnsurePrefabReleaseRegistration` ([`prefab_release_model.cpp`](../../tests/prefab_release_model.cpp)) | prefab release/lifecycle matrices | **KEEP** (lib + fixture) |
| Scoped links Local vs NetworkShared | [`object_link.h`](../../include/apptraverse/object_link.h) Serializer + `NodeVisitor<LocalPtr>` | `apptraverse_closed_event_graph_test`, `apptraverse_shared_node_foundation_test` (Debug) | **KEEP** |
| USER-over-BACKING, const discard, 0-byte Event | [`overlay_domain_storage.cpp`](../../src/overlay_domain_storage.cpp) `BackingComparedStorageWriter`, `SupplyConstantDiscardWriter` | `apptraverse_overlay_domain_storage_test` (Debug) | **KEEP** |
| `force_materialize` для historical base | [`node.h`](../../include/apptraverse/node.h) `Save(V0)`; patch `force-materialize`; overlay `Store` | `apptraverse_model_persistence_test`, compaction matrices | **KEEP** |
| `ShouldSaveVersion` до writer | CPM [`aether-objects-should-save-version.patch`](../../cmake/patches/aether-objects-should-save-version.patch); fixture `PrefabWindow::ShouldSaveVersion` | dynamic layer roundtrip, overlay (Debug) | **KEEP** (lib patch + fixture hook) |
| `Node::base`, journal, replay | [`node.h`](../../include/apptraverse/node.h) `ReplayFromBase` / `CompactJournalInto` | `apptraverse_model_persistence_test`, compaction roundtrip | **KEEP** |
| Compaction guard **11B.2** | `CanCompactStoredObjectVersions`, `EventFor::CanCompactStoredState` | `apptraverse_prefab_old_exe_compaction_guard_matrix` | **KEEP** |
| Штатный wire слоёв supply | [`distilled_supply_package.cpp`](../../src/distilled_supply_package.cpp), [`distilled_domain_export.cpp`](../../src/distilled_domain_export.cpp) | lifecycle matrix, supply split/equivalence, lazy/surfaces embedded matrices | **KEEP** |
| CPM patches Æther (5) | `cmake/patches/aether-objects-*.patch` | prefab + overlay + persistence suite | **KEEP** |
| Published ClassId / S0 / S1 / Events | Fixture `PrefabWindow` S0/S1, prefab Event classes (**без изменений в C0**) | prefab matrices Debug+Release | **KEEP** |
| Test-only origin / manual clone | `PrefabPtr`, `CloneWorkspaceFromPrefabs` | prefab matrices | **DEFER** → **12R–13** |

**C0.3 PASS** · **C0 PASS** · следующий этап **12R.2** (реализация).

---

## 12R.1 — техническая проверка origin (**PASS**, doc-only)

**Срез:** Æther Objects **`79283b6`**, AppTraverse **`df536e6`**, docs **`c00aafe`**. Код/tests **не** менялись. CTest **не** требовались.

**Решение:** persisted **`Obj::kClassId / S1`** + поле **`ae::ObjPtr<ae::Obj>`** (`origin_`), wire = штатный **`ObjectPtrBase`** (`obj_ptr_base.cpp`: `ObjId`+`flags`, conditional `SaveRoot`); assign **`ObjPtr<Obj>::Declare(..., kUnloadedByDefault|kUnloaded)`** без `cached_`.

**Доказательства по коду (pin):**

| Тема | Файл (Æther / AppTraverse) | Вывод |
| --- | --- | --- |
| Save без load P | `obj_ptr_base.cpp` `Seri` L133–136 | `SaveRoot` только при `cached_` |
| Deseri lazy | `obj_ptr_base.cpp` `Deseri` L146–151 | `LoadRoot` blocked by `kUnloaded*` |
| Visitor | `obj_ptr.h` `NodeVisitor<ObjPtr>` | deep walk только `cached()` |
| Отдельный ClassId S1 | `domain.h` `Save<T>` + `SaveVersion` | hook **`Obj::kClassId/1`** отдельно от **`T::kClassId`** |
| Load multi-facet | `domain.cpp` `LoadStoredLayers` | нужен **`IsExisting(Obj::kClassId)`** → patch registrar |
| Unknown layer guard | `reject-unknown-stored-class.patch` | **`Obj::kClassId`** whitelisted |
| Scoped links | `object_link.h` | **`SharedPtr`≈`ObjPtr`**; **`LocalPtr`** excluded NetworkShared |

**Blockers для автоматической safety:** materialized origin → USER write of **P** (discipline); **не** blocker для выбора типа.

**12R** / **12B:** **TODO** (implementation). **12R.2a PASS** — [§12R.2a](#12r2a--базовый-objs1-origin-facet-pass-code). Детали: [PLAN §12R.1](./PLAN.md#12r1--техническая-проверка-pass-doc-only).

---

## 12R.2a — базовый Obj/S1 origin facet (**PASS**, code)

**Patch:** [`aether-objects-origin-facet.patch`](../../cmake/patches/aether-objects-origin-facet.patch) (`obj.h` / `obj.cpp` / `domain.h`), подключён в `cmake/aether_version.cmake` + root `CMakeLists.txt`.

**Семантика API:**

| API | Поведение |
| --- | --- |
| `SetOriginId(P)` | `ObjPtr<Obj>::Declare` с **`kUnloadedByDefault \| kUnloaded`**, без `cached_`; invalid/`domain==nullptr` → `origin_.Reset()` |
| `origin_id()` | `origin_.id()` |
| `ShouldSaveOriginFacet(V1)` | `origin_.is_valid()` (materialized → **`Save(V1)`** **`abort()`**, не silent skip) |
| `Save(V1)` | `dnv(origin_)`; **`std::abort()`** если `origin_.is_loaded()` (fail-closed) |
| `Load(V1)` | `dnv(origin_)` — после load ссылка lazy |
| Ручной facet | `SaveVersion(V1, static_cast<Obj const&>(obj), id)` → ключ **`Obj::kClassId/1`**; **не** участвует в `Save<Derived>` без hook |

**Versioning:** protected `Obj::Load/Save(V1)` **не** попадают в `VersionSaveTrait<Derived,1>`; `domain.h` — явный путь **`T==Obj && V==1`**; **`ShouldSaveOriginFacet`** для **`Obj/S1`**; **`ShouldSaveVersion`** — для Derived (**10E**).

**12R.2b.1 PASS:** patches **`obj-facet-factory`**, **`domain-registered-objects`** (minimal lifecycle); **`origin-facet`** — **`ShouldSaveOriginFacet`** (fix регрессии **10E**); цепочка patches 1–8 на pin **79283b6** — **`git apply` OK**; **`apptraverse_object_origin_facet_test`** расширен.

**12R.2b.2 PASS:** **`origin-facet.patch`** — post-**`Save<T>`** hook: **`SaveVersion(V1, Obj, id)`** при **`LocalPersistent`**, **`T::kClassId == obj.GetClassId()`**, valid **`origin_id()`**; **`ShouldSaveOriginFacet`** = **`origin_.is_valid()`** only; тесты auto facet, Derived+Obj S1, repeat Save, cold load без ручного **`SaveVersion`**.

**12R.3 / 12R PASS:** cold-load helpers держат **`Domain`** живым; **`TestFinalOriginRoundtripV0/V1`** — два **`Domain`** на **`RamDomainStorage`**, **`SaveRoot`/`LoadRoot`**, тело **P** absent, **`ShouldSaveVersion`** ( **`OriginFacetTargetV1Gated`** ) + auto **Obj/S1**.

**Release SEGFAULT fix (12R.2b.1):** **`DomainRegisteredObjects`** на heap, unregister в **`~Obj`** через **`registered_objects_`**.

**Тесты (2026-10-26, incremental `out/release` / `out/debug`):** origin facet Debug/Release **PASS**; persistence Debug/Release **PASS**; prefab release/lifecycle/dynamic layer/compaction roundtrip/old-exe guard Release **PASS**; **`posx_a=10`**, лишний **`WindowA/S1`** **нет**.

---

## Итерации 01–22

| ID | Status | SHA | Tests | Notes |
| --- | --- | --- | --- | --- |
| 01 | PASS | — | — | Аудит **01A–01D** (doc only); код не менялся |
| 02 | PASS | — | — | Layout `state/`, `user/`, `<R>/`, `active-release` ([PLAN §02](./PLAN.md#решение-итерации-02--каталоги-и-номера-поставок)); код не менялся |
| 03 | PASS | — | `ctest -R release_metadata` Debug + Release (9/9 each) | `examples/surfaces_demo/release.json` + probe; не Surfaces GUI |
| 04 | PASS | — | `apptraverse_overlay_domain_storage_test` + `apptraverse_model_persistence_test` Debug/Release | Storage discard-only; без runtime const checks |
| 05 | PASS | `b3134f4` | `apptraverse_*_distilled_embedded_matrix`, overlay + model_persistence Debug/Release | Exporter→generated header→consumer |
| 06 | PASS | `ce46b80` | `apptraverse_prefab_release_matrix` Debug/Release | Headless prefab R0→R2 + **06B** delta-first base S1, v30 BACKING replay, lazy B, journal unknown; Surfaces **не** менялись |
| 07 | PASS | `ae495b6` | `apptraverse_prefab_release_lifecycle_matrix`, supply split, prefab matrix Debug/Release | Prototype `supply.bin` (BinaryArchive upsert delta); R0 `--supply-package`; semantic matrix; T02/T12 — фрагменты |
| 08 | PASS | `95968bd` | `apptraverse_prefab_active_release_matrix`, prefab/lifecycle/supply split Debug/Release | `active-release` BinaryArchive; `state/<R>/supply.bin`; embedded wins when `active <= embedded`; T13/T14 фрагменты |
| 09 | PASS | `1ce8901` | `prefab_release_r1_supply_equivalence_test`, `prefab_release_r1_user_equivalence_matrix` + prefab matrices Debug/Release | **09A:** R1 BIN ≡ R1 embed; один USER; не clean install; T12 фрагмент |
| 10 | PASS | `ce4f1a3` | `prefab_late_instance_matrix`, `prefab_release_matrix`, `prefab_dynamic_layer_roundtrip_matrix`, `overlay_domain_storage_test`, R1 supply + user equivalence Debug/Release | **10A–10E:** dynamic S0/S1, late WindowC, root-save без лишнего USER S1 (`ShouldSaveVersion`), historical base S1, 0-byte Event; T03/T09 фрагменты; T07–T10 не PASS |
| 11 | IN_PROGRESS | `5653a9b` | compaction + old-exe guard + prefab/journal matrices Debug/Release | **11A+11B.2 PASS**; **11B.1** исторически обнаружен и исправлен; T16/T17 фрагменты |
| C0 | PASS | `df536e6` | C0.3 suite Debug **17/17**, Release **14/14** CTest | **C0.1–C0.3 PASS** — [§C0.3](#c03--верификация-сохранённых-механизмов-pass) |
| C0.1 | PASS (doc) | `b6dcdae` | — | Аудит — [§C0.1](#c01--аудит-избыточности-object-core) |
| C0.2 | PASS | `f36fc1d`/`df536e6` | prefab + overlay regressions | **C0.2.1** ClonePlan; **C0.2.2** `failed_` |
| C0.3 | PASS (doc) | `9e91f4d` | см. [§C0.3](#c03--верификация-сохранённых-механизмов-pass) | Верификация KEEP/DEFER; код не менялся |
| 12A | PASS (doc) | `ed5eef9` / `4bda0ad` | — | Слой **`Obj/S1`** + отдельный `SaveVersion`; payload **`ObjId`** — **устаревший черновик** → **12R** |
| 12R | PASS | `9981e0f` | origin facet + persistence + prefab/compaction Debug/Release (2026-10-26) | **12R.3** final roundtrip; **12B** next |
| 12R.2b.2 | PASS | `4ced1fa` | см. **12R** | Auto **`Obj/S1`** hook |
| 12R.3 | PASS | — | **`object_origin_facet_test`** Debug/Release + prefab matrices Release | Domain lifetime fix; **`SaveRoot`** roundtrip |
| 12R.1 | PASS (doc) | `c00aafe`+ | — | Техпроверка pin **`79283b6`** — [§12R.1](#12r1--техническая-проверка-origin-pass-doc-only) |
| 12R.2a | PASS | `ead8e61` | origin facet Debug+Release; persistence/prefab split Debug | [§12R.2a](#12r2a--базовый-objs1-origin-facet-pass-code) |
| 12B | IN_PROGRESS | — | **12B.1 PASS**; **12B.2** disk/process TODO | P→A overlay origin; не полный **12B PASS** |
| 12B.1 | PASS | `c1c8a12` | `object_origin_facet_test`, `model_persistence_test`, `overlay_domain_storage_test` Debug+Release; prefab compaction/guard/dynamic-layer matrices Debug+Release | **`TestConstPrefabDynamicInstanceRoundtrip`** (`OriginFacetTargetV1`, USER/BACKING overlay) |
| 13 | TODO | — | — | **Универсальный Clone** (после **12B**) |
| 14 | TODO | — | — | **S + historical base**; завершение основных **T16/T17** |
| 15 | TODO | — | — | Единая модель поставок R/E/B/S; **T11–T15** |
| 16 | TODO | — | — | **R0–R5** lifecycle; **T01–T18** subset |
| 17 | TODO | — | — | Application/UI integration; часть параллельно в **application-versioning** |
| 18 | TODO | — | — | Optional resources, locale, cache |
| 19 | TODO | — | — | Æther updates (ArtifactSource) |
| 20 | TODO | — | — | SharedNode |
| 21 | TODO | — | — | Plugins + платформы (постепенно) |
| 22 | TODO | — | — | **Production-hardening** (после функционального прототипа) |

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
- **2026-10-23** — **01C:** аудит const/dynamic и ссылок (Surfaces localization, aether-objects pin **`79283b6`**); код просмотрен на **`1b0e9cb`**; doc-only; тесты не запускались.
- **2026-10-23** — **01D:** избыточность/очистка; **01 PASS** (аудит 01A–01D завершён, production не менялся); срез **`7dd187f`**; тесты не запускались (SHA — commit этого изменения).
- **2026-10-23** — **02:** layout каталогов и различие R/S/EXE в [PLAN.md](./PLAN.md); **02 PASS** (doc only); срез документов **`f27617a`**; код/миграция/`--state-dir` не менялись; тесты не запускались (SHA — commit этого изменения).
- **2026-10-23** — **03:** `release.json`, CMake generate → `surfaces_embedded_release.h`, `apptraverse_release_metadata_probe` (+ alt/dual/isolated + invalid-config WILL_FAIL); Surfaces startup не менялся; **03 PASS**; Debug + Release `ctest -R release_metadata` 9/9; push — **`c139267`**.
- **2026-10-23** — **04 (doc):** нормативный контракт в [PLAN §04](./PLAN.md#решение-итерации-04--контракт-const-vs-dynamic); срез **`b5d6f5a`**; **`6969ad2`** link fix (SHA — отдельные commits).
- **2026-10-23** — **04 (storage):** `OverlayDomainStorage` supply constants + overlay tests; push — `3cf56cd`.
- **2026-10-23** — **04 (policy):** runtime const-mutation checks **отменены**; storage discard-only для const USER; Cursor rule Events; **04 PASS**; Debug/Release overlay + model_persistence; push — SHA commit ниже.
- **2026-10-24** — **05:** build-time `BuildSurfacesGraph`/lazy distill → `WriteEmbeddedDistilledSupplyHeader`; `InstallEmbeddedDistilledBacking` + `OverlayDomainStorage` supply ids; load-only consumers + Python matrix (пустой cwd); headless distill embed (`surfaces_demo_distill_embed`); **05 PASS**; Debug/Release 4/4 ctest; push — **`b3134f4`**.
- **2026-10-24** — **07:** prototype `distilled_supply_package` (BinaryArchive: supply ids + upsert layers); split embed R0/R2; R1 delta `main-title-A→B`; R0 `--supply-package`; semantic lifecycle matrix (без corrupt-package tests); **07 PASS** (push SHA — commit упрощения).
- **2026-10-24** — **07 (prototype wire):** убран APSP/CRC/corrupt validation; BinaryArchive-only package; правило в `.cursor/rules/apptraverse-coding-agent.mdc`; **07 PASS**; push — **`ad022fe`**.
- **2026-10-24** — **08:** `active-release` + fixture backing chain `embedded → packages`; matrix A–E + R2/T13; `--supply-package` matrix сохранена; **08 PASS**; push — **`045761a`**.
- **2026-10-24** — **09A:** `apptraverse_prefab_release_r1`; layer oracle R0+BIN vs R1 embed; user equivalence matrix (existing USER); **09 PASS** (09A scope); push — **`1ce8901`**.
- **2026-10-24** — **10A:** `SetWidthEvent` + `set-width-a`; `prefab_dynamic_layer_roundtrip_matrix` (R0→R1→R2→R1→R2); **10 IN_PROGRESS** (10A PASS); Debug/Release 7/7 prefab subset; push — **`02ca209`**.
- **2026-10-24** — **10B:** `WindowCAddedEvent` + `create-tool-window`; `prefab_late_instance_matrix`; **10 IN_PROGRESS** (10A+10B PASS); push — **`60a0f4f`**.
- **2026-10-24** — **10C:** `delta-posx-c`, `replay-window-c`; late WindowC USER S1 + R1 rollback + replay; **10 PASS** (10A–10C scope); push — **`2941722`**.
- **2026-10-24** — **10D:** `delta-posx-c` → `SaveRoot(WindowC)` only; matrix: no USER S1 on WindowA/B; push — **`913b3ff`**.
- **2026-10-24** — **10E:** Æther CPM `aether-objects-should-save-version.patch`; `PrefabWindow::ShouldSaveVersion(V1)`; overlay 0-byte Event; historical base via explicit `SaveVersion`; **10 PASS** (10A–10E); Debug/Release prefab + overlay + R1 equivalence; push — **`ce4f1a3`**.
- **2026-10-24** — **11A:** `compact-window-c`; `prefab_compaction_roundtrip_matrix`; штатный `Node::CompactJournal`/`CaptureBaseStateInto`; **11A PASS**; Debug/Release; push — **`237a5bb`**.
- **2026-10-25** — **11B.1:** `prefab_old_exe_compaction_guard_matrix.py`; R1 compact после R2 delta — journal=0, replay 27→20; регрессия; push — **`4f9389f`**.
- **2026-10-25** — **11B.2:** `CanCompactStoredObjectVersions` + guard в `CompactJournalInto`; guard matrix CTest PASS; **11B.2 PASS**; Debug/Release; push — **`5653a9b`**.
- **2026-10-25** — **doc v0.3:** roadmap **12–22**, контрольные точки A–F, prototype-first; история **01–11** сохранена; код baseline **`66515d8`**; тесты не запускались; push — **`3eb6f15`**.
- **2026-10-25** — **12A (doc):** системный origin — persisted **`ae::Obj` S1** (`ObjId/Obj::kClassId/1`), отдельный `SaveVersion` в `DomainGraph::Save`; черновик payload **`ObjId`**; код не менялся; push — **`ed5eef9`**, roadmap tip **`4bda0ad`**.
- **2026-10-25** — **doc sync (01 plan):** PLAN/PROGRESS ↔ **`01_Object_Core_Versioning_PLAN.md` (25.10.2026)**; **C0** перед **12R/12B**; **12R** lazy origin ref; координация с **application-versioning**; код baseline **`66515d8`**; тесты не запускались; push — **`b6dcdae`**.
- **2026-10-25** — **C0.1 (doc):** аудит избыточности Object Core → [§C0.1](#c01--аудит-избыточности-object-core); **C0.1 PASS**; **C0 IN_PROGRESS**; код не менялся; тесты не запускались; push — **`4a51d5c`**.
- **2026-10-25** — **C0.2.1:** удалены `ClonePlan`, `PlanCloneFromPrefabGraph`; `CloneWorkspaceFromPrefabs(ae::Domain&, bool)`; Debug + Release prefab CTest **8/8 PASS**; **C0.2.1 PASS**; push — **`f36fc1d`**.
- **2026-10-25** — **C0.2.2:** `BackingComparedStorageWriter::failed_`; Debug **11/11**, Release prefab+persistence **10/10** CTest; **C0.2 PASS**; push — **`df536e6`**, docs **`7322b9c`**.
- **2026-10-25** — **C0.3 (doc):** верификация оснований → [§C0.3](#c03--верификация-сохранённых-механизмов-pass); Debug **17/17**, Release **14/14** CTest; **C0 PASS**; push — **`c00aafe`**.
- **2026-10-25** — **12R.1 (doc):** техпроверка origin — [§12R.1](#12r1--техническая-проверка-origin-pass-doc-only); **12R.1 PASS**; **12R/12B TODO**; push — SHA commit ниже.
- **2026-10-25** — **12R.2a:** CPM origin facet + `apptraverse_object_origin_facet_test`; versioning fix в `domain.h`; Debug origin + model_persistence + prefab supply split; Release origin + model_persistence; **12R.2a PASS**; **12R TODO**; push — **`ead8e61`**.
- **2026-10-24** — **06B:** fixture `EnsurePrefabVersionInHistoricalBase` (S1 в instance `Node::base` при первом delta-Event); embed BACKING v30 (`posx=30`) replay остаётся 15; delta-first eager/lazy; `FutureUnknownJournalEvent` fail-closed на old EXE; matrix runner (отдельные state dirs, exit=0, manifest); **06B PASS**; Debug/Release `apptraverse_prefab_release_matrix`; push — **`ce46b80`**.

- **2026-10-26** — **12B.1:** `TestConstPrefabDynamicInstanceRoundtrip` in `object_origin_facet_test.cpp` — const **P** in BACKING, dynamic **A** in USER with **`SetOriginId(P)`** via **`OverlayDomainStorage`**; cold **`LoadRoot(A)`** preserves fields/origin without materializing **P**; **12B.1 PASS**; **12B IN_PROGRESS**. Verify: `apptraverse_object_origin_facet_test`, `apptraverse_model_persistence_test`, `apptraverse_overlay_domain_storage_test` Debug+Release; prefab compaction/guard/dynamic-layer matrices Debug+Release. Baseline **`b5f050c`**. Land SHA — commit below.

*(Baseline fixtures: `19d7da6`; **03** — build-time R; **04** — контракт; **05** — embedded graph; **06+** Surfaces GUI / USER на диске.)*

---

## Итерация 04 — контракт const/dynamic (doc)

**Область:** только [PLAN.md](./PLAN.md) §04 и этот раздел. **Срез:** `c139267`. **Enforcement:** не реализован. **Не дублировать:** [01C](#итерация-01c--константность-и-ссылки) (аудит фактов).

**Итог (doc):** идентичность по `ObjId`, Load/Save, ссылки/lazy, `force_materialize` — [PLAN §04](./PLAN.md#решение-итерации-04--контракт-const-vs-dynamic).

**Итог (код):** [`OverlayDomainStorage`](../../include/apptraverse/overlay_domain_storage.h) — `supply_constant_ids`; const Load/Enumerate с BACKING; Store discard (USER не для тел const); тесты в [`overlay_domain_storage_test.cpp`](../../tests/overlay_domain_storage_test.cpp). **Runtime const-mutation checks:** не делаются (архитектурное решение); **Cursor** проверяет Events при изменении кода. **Не сделано:** Surfaces GUI startup из embedded (**06**). **T01–T18:** не PASS. **T16/T17:** не решены.
