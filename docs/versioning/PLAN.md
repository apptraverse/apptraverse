# AppTraverse — план архитектуры версионирования состояния и поставок

**Статус документа:** архитектурный контракт и roadmap. Это не описание уже реализованной сквозной release-модели.

**Рабочая ветка (код + docs):** **`integration/unified-application`** — объединение Object Core и Windows Application ([merge **`4848402`**](https://github.com/apptraverse/apptraverse/commit/4848402) ← `integration/serialization-surfaces` + `integration/application-versioning`). Исходные ветки — **исторические baseline**, не параллельная разработка.

**Связанные планы:** прогресс — [PROGRESS.md](./PROGRESS.md); application merge — [application-versioning.md](../integration/application-versioning.md); WindowCatalog demo — [window_catalog_demo.md](../window_catalog_demo.md).

**Архитектурная редакция:** **AppTraverse Libraries Architecture RU v0.3 (25.10.2026)**; детализация Object Core — **`01_Object_Core_Versioning_PLAN.md` (25.10.2026)** ([share](https://chatgpt.com/library/share/libfile_c97b08ed7af88191a916944ac06566f7?account_id=personal)).

**Подтверждённый HEAD Object Core (исторический срез до merge):** **`df536e6`** (C0.2.2); roadmap docs — **C0.3** (базовый Object Core **`66515d8`**). На **`integration/unified-application`** верификация Object Core regressions **не** переобъявляется без прогона на unified (**U6**).

**Сверка с Google Doc:** прямой доступ к документу «AppTraverse — версионирование состояния и жизненный цикл поставок» ([ссылка](https://docs.google.com/document/d/196STRvU1QMbMu0ArGRLahPpgnTb6HQoldUV-7tOIcfo/edit)) недоступен (403). Нумерация **T01–T18** приведена в соответствие с текстом исходного Doc (ручная сверка по постановке); остальной контракт — из аудита репозитория на baseline `19d7da6`.

**Связанные материалы:** корневые [plan.md](../../plan.md), [Progress.md](../../Progress.md); инвентарь схем [serialization_schema_v0_inventory.md](../serialization_schema_v0_inventory.md); проверенные fixtures [window_schema_layers.md](../../tests/window_schema_layers.md), [event_version_replay.md](../../tests/event_version_replay.md); правила агента [.cursor/rules/apptraverse-coding-agent.mdc](../../.cursor/rules/apptraverse-coding-agent.mdc).

---

## Принцип разработки (v0.3)

**Сначала полностью работающий прототип всех основных архитектурных механизмов; только потом corner cases, hardening и production readiness.**

На текущем этапе:

- Happy path и проверки **основных семантических инвариантов** (ObjId, origin, aliases, lazy, USER/const, версии S, Event, historical base, replay, Clone, отсутствие потери пользовательских изменений).
- **Не** добавлять: CRC, hashes, signatures, defensive parsing, лимиты, retry/fallback, crash recovery, тесты повреждённых файлов, security preflight, доверенные издатели, сложные политики совместимости, атомарные обновления «из коробки».
- **Не** создавать managers, wrappers и дополнительные слои инфраструктуры без реальной потребности.
- Использовать штатную сериализацию Æther **`BinaryArchive`** и существующую объектную систему; не усложнять API ради гипотетических сценариев.
- То же относится к будущим **Download Manager**, **Update Manager**, plugins и package loading: на этапе прототипа артефакты считаются **корректными**, созданными нашими средствами и совместимыми по **заранее выбранному** сценарию.

**Production-hardening** (подписи, recovery, malformed files, sandbox plugins и т.д.) — **отдельная заключительная фаза** ([итерация 22](#22--production-hardening--отдельно-после-функционального-прототипа)); **не** является условием завершения функционального прототипа (итерации **C0**, **12–21**).

---

## Нормативные правила Object Core (v0.3 / 01 plan)

1. Только штатные **`DomainGraph`**, **`Load`/`Save(Version<S>)`**, **`BinaryArchive`**.
2. Связи внутри графа — **`ObjPtr`**, **`LocalPtr`**, **`SharedPtr`** по семантике scope. Голый **`ObjId`** — identity, ключи storage, индексы, границы Domain; **не** вместо живой объектной связи в persisted-полях.
3. Обязательные persisted-поля **без** неявных defaults из constructors, member initializers или fallback.
4. Required graph — all-or-nothing; **lazy unloaded** — нормальное состояние.
5. Артефакты прототипа считаются корректными: **не** добавлять CRC, hashes, signatures, malformed/corrupt-file tests, recovery, дополнительные лимиты (см. **22**).
6. Семантические инварианты обязательны: USER, origin, history, replay, unknown layers, compaction guard (**11B.2**).
7. **Не** создавать дополнительные managers, registries, wrappers и параллельные runtime paths.
8. Маленькие итерации, существующие регрессии, Google C++ Style, C++20, без RTTI/exceptions.

---

## Порядок работ (после 11)

**Object Core (roadmap на unified, без повторного PASS без тестов):** **13 IN_PROGRESS** — **13.1–13.2 PASS** → **13.3–13.6 TODO** → **14–16 TODO**. **12R** / **12B PASS**. **C0 PASS**. **11** — частичный PASS (**11A**, **11B.2**; полный **T16/T17** — нет). Этапы **17–22** — roadmap.

**Unified application (пользовательский порядок после merge):**

| Шаг | ID | Статус |
| --- | --- | --- |
| Инкрементальная сборка + catalog model test | **U1** | **PASS** (`8835d26`) |
| Ручной Windows WindowCatalog | **U2** | **manual PASS** (не полный regression) |
| Синхронизация PLAN/PROGRESS | **U3** | **PASS** (doc-only) |
| Аудит объединённой архитектуры | **U4** | TODO |
| Минимальная очистка по аудиту | **U5** | TODO |
| Итоговая проверка unified (Object Core + apps, MinimalApp) | **U6** | TODO |
| Embedded startup **9D/9E**, R/E/B/S в приложениях | после **U6** | TODO |
| **13.3+**, Clone графов, release lifecycle | после стабилизации app | TODO |

**Принято:** сначала работающее Windows-приложение и ручная проверка (**U2**), затем архитектурный порядок (**U4–U6**), без mass-refactor и без новой параллельной application-ветки.

**Открыто (не менять в U3):** `DirectoryDomainStorage` может фиксировать ошибки записи; часть production callers **не** проверяет результат `SavePersistentModelSnapshotToStorage` — отдельная задача (явная ошибка вызывающему коду).

---

## Текущее состояние (код `df536e6` — Object Core baseline; unified см. [PROGRESS §Unified](./PROGRESS.md#unified-integration-integrationunified-application))

История итераций **01–11** не переписывается.

| Область | Статус |
| --- | --- |
| **01–09** | **PASS** в заявленном fixture scope (headless prefab, supply, active-release, equivalence). |
| **10A–10E** | **PASS** — R0–R2, USER S0/S1, WindowC, rollback, `ShouldSaveVersion`, root-save. |
| **11A** | **PASS** — полная compaction WindowC на R2. |
| **11B.1** | Обнаруженная и воспроизведённая регрессия (R1 compact без guard). |
| **11B.2** | **PASS** — guard компактации при неизвестных persisted S у Node и historical base. |
| **C0** | **PASS** — **C0.1** doc, **C0.2.1**/`C0.2.2` код, **C0.3** верификация ([PROGRESS §C0.3](./PROGRESS.md#c03--верификация-сохранённых-механизмов-pass)). |
| **Итерация 11** | **IN_PROGRESS** (fixture compaction/replay; полный **T16/T17** — нет). |
| **T16/T17** | Частичное покрытие; **не** PASS. |
| **Полный R0–R5**, **T01–T18** | **Не** завершены. |

**12A (doc, `ed5eef9`):** выполнен; payload **`ObjId`** в **12A** — **исторический черновик**, пересматривается в **12R** (lazy **`ObjPtr`**). **12R PASS**; **12B PASS** (overlay RAM + disk/process matrices).

**Архитектурный пробел (остаётся):** **Clone** вручную в fixtures; нет универсального Clone/S-evolution (**13+**). Origin на штатном **`Obj::SetOriginId` / `origin_id()`** + **`Obj/S1`** — **12R/12B PASS**; отдельный **`object_origin.h`** для **12B** **не** введён (API достаточен).

**Следующий активный шаг Object Core:** **13.3** — graph closure + remap (**13.1–13.2 PASS**). **Следующий шаг unified:** **U4** — архитектурный аудит (**U1–U3 PASS**).

---

## Целевая модель (кратко)

Приложение разделяет **номер поставки Release R**, **версии сериализации класса S** (слои на диске) и **EXE** с полным начальным дистиллированным графом. Пользовательское состояние живёт в **USER**; константы поставок — в каталогах **state/R** без записи в USER. Старый EXE не уничтожает неизвестные слои; компактация журнала не удаляет события с более новыми сохранёнными слоями, чем понимает текущий процесс.

---

## Решение: headless prefab proof (Surfaces 06 отменён)

**Статус:** принято в документе; проверяется fixture **до** полного R0–R5 runner.

- **Release-lifecycle** (R0→R5, USER/backing, schema S) проверяется **отдельным headless-приложением** с **NullPresenter**, не Surfaces Demo GUI/Win32 lifecycle.
- **Surfaces Demo** остаётся demo/evolution; **не** oracle для R0–R5 и **не** получает переключение startup на embedded/backing в этой ветке (итерация **06** Surfaces **отменена**).
- **Константы** (Localization / label strings) живут **в графе рядом с местом использования** (Presenter → Localization → `strings[]`), а не в отдельном `SupplyCatalog`.
- **Динамические экземпляры** создаются из **prefab-графов** (два prefab одного класса с разными defaults); **origin** `instance ObjId → prefab ObjId` сериализуется только в каноническом test-only **`PrefabPtr<T>`** (`prefab_id` + instance `ObjPtr`; lazy — flags `ObjPtr`).
- **Aliases** (Preferences → WindowA, Presenter → Window) — обычные `ObjPtr` / ссылки; **не** второй `PrefabPtr` на тот же instance.
- **Unknown Event (T18 fixture):** `prefab_release_unknown_*` — (1) flat Event root: fail-closed **`LoadRoot`**; (2) **`write-journal-unknown` / `load-journal-target`**: fail-closed load при Event в **`Node::journal`**. Полный **T18** PASS — не заявляется.
- **T18:** отдельная ветка fixture — Event с **неизвестным ClassId** для старого EXE → **явный отказ до изменения USER**; новый EXE повторно загружает payload и журнал.

Механизм build-time embed/distill (**итерация 05**) **сохраняется** и переиспользуется для prefab BACKING в fixture (`apptraverse_prefab_release_*`).

---

## Три независимые сущности

### Release R

- Последовательные поставки: R0, R1, R2, …
- Используется для обновлений, откатов и выбора активной константной поставки.
- **Не** совпадает с номером схемы класса S.

### Class schema S

- Для данного `ClassId`: слои S0, S1, S2 … в каталоге `ObjId/ClassId/S`.
- Новый слой — отдельный файл; старые `Load`/`Save` продолжают работать.
- Новые поля не переписывают байты старых слоёв.

### EXE

- Содержит полное начальное дистиллированное состояние поставляемого графа (включая lazy-узлы и все поддерживаемые слои классов).
- Каждая поставка выпускает **полный** EXE для чистой установки.
- Совместимый старый EXE может читать последующие **бинарные** поставки констант (offline-доставка в `state/R` — вне scope реализации).

---

## Каталоги (целевая структура)

```
state/
  active-release          # проектное имя; формат — отдельная итерация
  user/
    ObjId/
      ClassId/
        0
        1
        2
  0/                      # R0 — константные данные поставки
    ObjId/
      ClassId/
        S
  1/
  2/
  3/
  4/
  5/
```

- Верхние числовые каталоги — **поставки константных** данных (Release R).
- **USER** — только пользовательское динамическое состояние.
- Имена и layout ниже **зафиксированы решением итерации 02** (документ); в коде и на диске у пользователей **ещё не внедрены**. Текущий код: `DirectoryDomainStorage` на произвольном `root`, Surfaces `--state-dir` как один USER-каталог, overlay/backing в fixtures.

---

## Решение итерации 02 — каталоги и номера поставок

**Статус:** принято в документе (итерация **02 PASS**); реализация, миграция и resolver — последующие итерации.

### Имена и роли

| Путь | Назначение |
| --- | --- |
| `state/` | Корень состояния приложения (один экземпляр на профиль/установку). |
| `state/user/` | **Единый USER** для всех обновлений и откатов Release R; ключ слоя без изменений: `ObjId/ClassId/S`. |
| `state/<R>/` | Константные данные **поставки R** (`R` = `0`, `1`, `2`, … десятичное неотрицательное). Не SHA, не `ClassId`, не имя Debug/Release. |
| `state/active-release` | Указание **активной** поставки R для чтения констант/backing; **не** версия схемы класса S. |

Внутри `state/<R>/` допускается тот же логический ключ `ObjId/ClassId/S`, что и в USER (физическая форма — см. **07**). **Не** объявлять дерево отдельных файлов единственным будущим форматом бинарного пакета (**07**).

### Release R, schema S и EXE

- **R** — номер **поставки**; увеличивается и при изменении **только логики** (logic-only), и при изменении констант/схем.
- **S** — версия сериализации **конкретного класса** (`…/ClassId/S`); **не** повышается автоматически при каждом R.
- **EXE** содержит полный начальный дистиллированный граф **своей** поставки R.
- **USER** (`state/user/`) — **один** каталог на профиль; **не** отдельная копия USER на каждый R.
- Бинарные поставки констант в `state/<R>/` **не** подменяют и **не** перезаписывают пользовательские динамические слои в USER.
- **Чистая установка** полного EXE **не** требует наличия каталогов всех предыдущих R (исторические R могут оставаться на диске для отката/offline, но не обязательны для first run).
- **Logic-only** поставка **не** требует искусственного пустого пакета констант в `state/<R>/`.
- Наличие каталога `state/5` **само по себе не активирует** R5. **Запрещено** правило «взять максимальный числовой каталог» как выбор активной поставки — выбор только через **08** (`active-release` и согласованный алгоритм).

### Согласование с `DirectoryDomainStorage`

- [`DirectoryDomainStorage`](../../include/apptraverse/directory_domain_storage.h) получает уже выбранный **`root`** и обслуживает `ObjId/ClassId/S`; **выбор R не входит** в его текущие обязанности (см. аудит **01B**).
- Будущий **`state/user/`** — тот же механизм, другой корень; **`state/<R>/`** — отдельные корни (или backing) для констант поставки.
- **Сейчас:** Surfaces Demo передаёт `--state-dir` **напрямую** как корень USER (эквивалент будущего `state/user/`, но **без** обёртки `state/` и **без** соседних `state/<R>/`). Семантику `--state-dir`, существующие профили и автоматическую миграцию **не менять** в итерации 02.

### Отложено (не часть 02)

| Итерация | Тема |
| --- | --- |
| **03** | Release config и метаданные R в EXE (single source of truth). |
| **07** | Формат и генерация **бинарного** пакета констант в `state/<R>/`. |
| **08** | Формат файла `active-release` и алгоритм выбора backing / активной R. |

Resolver, новые storage API и политики const/dynamic в 02 **не** проектируются.

---

## Решение итерации 03 — release config и встроенные метаданные EXE

**Статус:** build-time источник R и probe **реализованы** (итерация **03 PASS**); полный дистиллированный граф в EXE — **05**; бинарный пакет — **07**; `state/active-release` / resolver — **08**. **Не** путать с runtime `state/active-release` и **не** выводить R из `APPTRAVERSE_SURFACE_DEMO_REVISION`, schema S, Git SHA или имени конфигурации CMake.

| Элемент | Значение |
| --- | --- |
| Config | [`examples/surfaces_demo/release.json`](../../examples/surfaces_demo/release.json) |
| Поле | **`release`** — обязательное JSON-число, неотрицательное **целое**, диапазон **`0` … `4294967295`** (`std::uint32_t`) |
| Генерация | [`cmake/generate_surfaces_release_metadata.cmake`](../../cmake/generate_surfaces_release_metadata.cmake) → build-tree header `surfaces_embedded_release.h` с `apptraverse::surfaces_demo::kEmbeddedRelease` (`unsigned`, диапазон проверяется как `uint32_t`) |
| Подключение | [`cmake/apptraverse_surfaces_release_metadata.cmake`](../../cmake/apptraverse_surfaces_release_metadata.cmake) — `apptraverse_embed_surfaces_release_metadata(target, config_json)`; зависимость от config → инкрементальная пересборка target |
| Проверка | `apptraverse_release_metadata_probe` + CTest ([`release_metadata_probe_test.py`](../../tests/release_metadata_probe_test.py)); runner читает JSON отдельно от EXE |
| Surfaces Demo EXE | Config **не** подключён к GUI startup в 03; `--state-dir` без изменений |

Ошибки config (нет поля, не число, дробь, отрицательное, переполнение, отсутствующий файл) → **FATAL** на этапе generate, **без** fallback `R=0`.

---

## Решение итерации 04 — контракт const vs dynamic

**Статус:** контракт **зафиксирован**; **storage** в [`OverlayDomainStorage`](../../include/apptraverse/overlay_domain_storage.h) — **реализован** (`supply_constant_ids`: Load/Enumerate с BACKING, тела const **не** пишутся в USER, в т.ч. `force_materialize`; **без** сравнения сериализованных байтов с BACKING и **без** runtime-отказа Event/Apply/Commit). **Контроль корректности Events**, меняющих объекты поставки, — **обязанность разработки**: статический разбор Cursor ([`.cursor/rules/apptraverse-coding-agent.mdc`](../../.cursor/rules/apptraverse-coding-agent.mdc)) и code review; **не** формальное доказательство неизменяемости. Итерация **04 PASS** *(storage + dev review)*; источник ролей в продукте — **05+** (manifest/embedded graph). Пакет метаданных — **07**; resolver R — **08**. Аудит фактов — **01C** ([PROGRESS §01C](./PROGRESS.md#итерация-01c--константность-и-ссылки)).

### Идентичность и классификация

| Правило | Смысл |
| --- | --- |
| Объект, не ссылка | Константность относится к **объекту поставки** (`ObjId` + его слои), а не к типу или const-qualifier на `ObjPtr` / `ObjectLink`. |
| Один ObjId — одна политика | Все aliases (mutable `T*`, `const ObjPtr`, разные поля графа) на **тот же** `ObjId` подчиняются **одной** роли const/dynamic. |
| Один C++ тип — разные роли | Один класс (напр. [`UiString`](../../include/apptraverse/ui/ui_string.h)) может описывать и **константу поставки**, и **динамический** пользовательский текст — роль задаётся **экземпляром** в описании графа, не именем типа. |
| Не признаки роли | `SharedPtr` / `LocalPtr` ([`object_link.h`](../../include/apptraverse/object_link.h)) — только **scope** сериализации; `ObjFlags::kUnloadedByDefault` — lazy load; **BACKING** хранит и константы, и **начальные** значения динамических объектов. |
| Логический источник | Классификация берётся из **описания поставляемого графа** (distill / embedded graph / manifest — конкретный носитель метаданных выбирается в **05–07**, не в 04). |

### Load / Save / изменение (нормативная таблица)

| Операция | Константа поставки | Динамический объект |
| --- | --- | --- |
| **Load** | Слои читаются из **backing** выбранной поставки R (resolver — **08**). Отсутствие обязательного слоя константы в backing — **ошибка загрузки**, не подстановка runtime-default. | Сначала **USER** (writable overlay); при отсутствии — fallback на начальное состояние в **BACKING** ([`OverlayDomainStorage::Load`](../../src/overlay_domain_storage.cpp)). |
| **Save (обычный)** | **Собственные** сериализованные слои константы **не** записываются в USER. **Ссылки** (`ObjId` + flags) на константу в USER **допустимы** — referent не копируется. | Пользовательские изменения **пишутся в USER**. USER имеет **приоритет** над backing при Load. |
| **Save + совпадение с backing** | Нет USER-слоя для константы (не создавать «тихий» skip вместо политики). Текущий overlay skip unchanged ([`BackingComparedStorageWriter`](../../src/overlay_domain_storage.cpp)) — механизм **динамики**, не определение const. | Если USER-слой уже есть и байты снова равны backing — **USER override не удаляется** (01B). |
| **Изменение в runtime** | Модель меняется **только через Events**; Events **не должны** менять const-объекты — проверяется при разработке (Cursor/review), **не** runtime-валидацией в Node/Commit/Apply. Save **не** материализует тело const в USER даже при in-memory мутации. | Изменения через модель/events допустимы; persist в USER. |
| **Граница «неизменяемости»** | Storage не пишет тела const в USER; это **не** полная runtime-immutability и **не** доказательство корректности Events. C++ const на указателе **не** считается enforcement. | — |

### Ссылки и обход графа

- **Ссылка ≠ referent:** запрет USER-записи **тела** константы **не** удаляет сохранение **ссылки** на неё в USER.
- **Константный контейнер:** константность **собственных** полей объекта **не** распространяется автоматически на все referent'ы; динамический объект, достижимый через константный узел или const-поле, при Save **не** теряется — сериализация обходит граф по правилам `ObjPtr` / [`ObjectLink`](../../include/apptraverse/object_link.h) и роли каждого `ObjId`.
- **Lazy:** не выполнять Load referent **только** ради классификации или Save; классификация по **метаданным графа** / уже известному `ObjId`, не по побочному чтению lazy-дерева.
- **Не в scope 04:** новый вид указателя, отдельный GraphManager, изменение wire-формата `ObjectLink` (wire = `ObjPtr`, см. комментарии в `object_link.h`).

### force_materialize и историческое состояние

- `ae::DomainQuery::force_materialize` ([`OverlayDomainStorage::Store`](../../src/overlay_domain_storage.cpp), также `DomainGraph::force_materialize` в [`node.h`](../../include/apptraverse/node.h)) **не** даёт права записывать **константы** в USER; назначение для **04** — по-прежнему **динамика**: `Node::base`, журналы, историческое состояние, обязательная материализация при Save/replay/compaction (как сегодня в overlay — см. **01B**).
- **`Node::base` нельзя** классифицировать как константу только потому, что байты совпали с поставкой; historical base остаётся **динамическим** контекстом (**T16** не PASS).
- Правила replay/compaction (**T17**) **не** меняются в 04 и **не** объявляются решёнными.

### Привязка к текущему коду (точки будущей реализации)

| Зона | Файлы | Будущая ответственность |
| --- | --- | --- |
| **Роль объекта** | Описание поставляемого графа (**05+**); до появления manifest — таблица ролей по `ObjId` при bootstrap/load. Demo: тексты [`UiLocalizationString`](../../include/apptraverse/ui/ui_localization_string.h) / вложенные [`UiString`](../../include/apptraverse/ui/ui_string.h) — **кандидаты** на константы поставки; [`UiLocaleSettings::language_index_`](../../include/apptraverse/ui/ui_locale_settings.h) — **динамика** пользователя. **Не** объявлять все `UiString` константными по типу. |
| **Контроль изменения** | **Dev-time:** Cursor/review при изменении Event/Apply (см. coding-agent rules); **не** добавлять runtime CanApply/Env/abort для const. |
| **Запрет USER-записи слоёв** | **Реализовано:** `Store` для const → discard writer, USER не создаётся (**до** ветки `force_materialize` для dynamic). |
| **Ссылки и обход** | **Без изменений Seri:** штатный SaveRoot; политика storage на **собственные** слои ObjId; dynamic-потомки через const-контейнер сохраняются ([`overlay_domain_storage_test.cpp`](../../tests/overlay_domain_storage_test.cpp)). |
| **Load / Enumerate** | **Реализовано:** const — только backing; USER/tombstone не перекрывает. Dynamic — прежний overlay-приоритет. |

Wire-формат и новый GraphManager **не** добавлялись.

### Проверки storage-enforcement

Реализовано в [`overlay_domain_storage_test.cpp`](../../tests/overlay_domain_storage_test.cpp) (SaveRoot/LoadRoot, Debug + Release):

1. Save динамического корня с ссылкой на константу: в USER — **ссылка**, **нет** файлов слоёв тела константы; BACKING неизменён.
2. In-memory mutation / Event на const `ObjId` + SaveRoot — **нет USER-слоя** тела const (BACKING неизменён); корректность Event — dev review, не runtime abort.
3. Lazy const referent: Save **без** предварительного Load тела константы (ссылка сохраняется).
4. Два `UiString` с разными `ObjId`: один const, один dynamic — обе роли на одном типе.
5. `force_materialize`: **не** создаёт USER-слой для const body; **по-прежнему** материализует dynamic historical `Node::base` / journal сценарии (расширение overlay/fixture tests).
6. USER override dynamic: значение снова равно backing — файл USER **остаётся**.

**Не заявлять:** транзакционный «атомарный SaveRoot всего графа»; неизменность констант ≠ атомарность сохранения смешанного графа.

### Отложено (не часть 04)

| Итерация | Тема |
| --- | --- |
| **05** | Embedded/distilled граф как носитель описания ролей. |
| **07** | Бинарный пакет и on-disk метаданные const-слоёв. |
| **08** | Resolver R и выбор backing. |

---

## Константные объекты

Сводка целевого поведения; **нормативный контракт** — [§04](#решение-итерации-04--контракт-const-vs-dynamic).

- Обновляются внешними бинарными поставками (offline-инструмент кладёт файлы в `state/<R>/`).
- Пользователь их не меняет; **слои тела** не сохраняются в USER; **ссылки** — да.
- Новый EXE уже содержит актуальные константы своей поставки (**05+**).
- Совместимый старый EXE читает более новые бинарные поставки через overlay/backing (частично в fixtures, не единый Release R).

**Важно:** `SharedPtr` / `LocalPtr` **не** являются автоматическим признаком константности (см. §04).

---

## Динамические объекты

Сводка; **нормативный контракт** — [§04](#решение-итерации-04--контракт-const-vs-dynamic).

- Новые слои S появляются с новым EXE (и пользовательскими изменениями).
- Бинарные поставки констант **не** обновляют существующие динамические слои в USER.
- Изменения пользователя — только в USER; неизменённые **dynamic** слои не материализуются (overlay skip unchanged vs BACKING).
- Lazy-объекты не материализуются в USER, пока не загружены и не изменены (const — отдельное правило «не Load ради Save», §04).
- Старый EXE не удаляет неизвестные ему новые слои (`CanCompactStoredState`, сохранение v1 при A/B — см. PROGRESS).

---

## Журнал и base

Сохраняемый контракт (частично подтверждён fixtures, не полный T01–T18):

| Требование | Статус |
| --- | --- |
| Исторический `Node::base` доступен после смены EXE / BACKING | Проверено: distilled lazy overlay, `force_materialize`, post-compact `Save(Node)` |
| Журнал переживает обновление и откат | Event A/B matrix, overlay roundtrip |
| Старый EXE не сворачивает события с неизвестными слоями | `CanCompactStoredState` + partial prefix test |
| После полной компактации base и ссылка владельца восстановимы | `model_persistence` A–F, journal cold reload |
| `Load` и `TryReplayFromBase` согласованы | Event matrix, replay fault tests |
| Компактация через откаты: неизвестный v1, известный префикс, до первого `SaveRoot` | Частично: **T17** — `CanCompactStoredState`, matrix overlay, `model_persistence` F |

**Открытый вопрос (не решён):** новый динамический слой S у **исторического** пользовательского `Node::base`, сохранённого старой поставкой со **своим** `ObjId`; согласованность `Load` и replay (**T16**). Не считать решённым до отдельной итерации.

---

## Как выпустить новую версию

Три типовые процедуры для разработчика (метаданные Release и генерация EXE — **итерация 03**, пока только предложения).

### A. Изменились только константы

1. Увеличить **Release R** (единый release config — TBD).
2. **Не** менять S классов.
3. Обновить исходные данные дистилляции.
4. Сформировать бинарную дельту констант относительно эффективной предыдущей поставки → `state/R`.
5. Собрать новый **полный EXE** для чистой установки.
6. Проверить: старый совместимый EXE + пакет vs новый EXE (эквивалентность констант).

### B. Изменилась только бизнес-логика

1. Увеличить Release R.
2. Собрать новый EXE; **не** менять S без изменения формата.
3. **Не** создавать лишних бинарных дельт констант.
4. Проверить неизменность исходного дистиллированного графа (байты слоёв).
5. Произвольное изменение `Apply` без отдельной проверки **не** считается безопасным.

### C. Добавилось сериализуемое поле

1. Увеличить Release R.
2. Увеличить **S только** затронутого класса; **ClassId** сохранить.
3. Не менять формат существующих слоёв; добавить `Load`/`Save` новой версии.
4. Обновить дистиллированный граф; собрать полный EXE.
5. Проверить: старый USER, чистая установка, откат, повторное обновление.

**Где хранить R:** предложение — один release config рядом с `state/` (JSON или аналог) + встроенные метаданные в EXE (итерация 03). Не размножать номера R по CMake/исходникам без single source of truth.

---

## Тестовая история R0–R5 и T01–T18

Последовательность поставок (целевая матрица; **не** все шаги реализованы как один runner):

| Release | Содержание |
| --- | --- |
| **R0** | Первый EXE. Window S0: `width`. Localization S0. Один активный и один lazy Window. Пользователь меняет `width`. |
| **R1** | Только новые константы; бинарный пакет для старого EXE + полный новый EXE. |
| **R2** | Новый EXE; Window S1: `posx`; сохранены `width` и пользовательские изменения. |
| **R3** | Только логика; дистиллированные данные без изменений; новый EXE. |
| **R4** | Снова константы; пакет для совместимого EXE + полный EXE. |
| **R5** | Новый EXE; Window S2: `border_width`; сохранены все предшествующие динамические слои и USER. |

**Первый проход R0→R5** — это **T01–T06** (пошаговая подготовка и проверка каждой поставки), а не отдельный T07.

**Откат и повтор на том же USER:** **T07** — пошаговый откат R5→R4→R3→R2→R1→R0 без повторения пользовательских действий; **T08** — повторное обновление R0→…→R5; **T09** / **T10** — изменение `width` / `posx` после отката на R1 / R2 с сохранением при возврате в R5.

**Сквозной цикл** R0→R5→R0→R5 остаётся обязательной проверкой, объединяющей T01–T08, T09–T10 (по матрице), T15 и остальные применимые ID.

**Что проверять:** файлы USER; неизменность пакетов; `ObjId`/`ClassId`/S; разные процессы; lazy не материализован без нужды; новые слои при откате; журнал и replay; нет повторного применения событий.

**Маппинг T01–T18** (исходный архитектурный документ):

| ID | Фокус |
| --- | --- |
| T01 | R0: первая установка, динамический `width`, lazy Window |
| T02 | R1: обновление только констант, бинарный пакет и новый полный EXE |
| T03 | R2: добавление Window S1 (`posx`) |
| T04 | R3: изменение только бизнес-логики |
| T05 | R4: ещё одно обновление констант |
| T06 | R5: добавление Window S2 (`border_width`) |
| T07 | Пошаговый откат R5→R4→R3→R2→R1→R0 без повторения пользовательских действий |
| T08 | Повторное обновление R0→R1→R2→R3→R4→R5 на том же USER |
| T09 | После отката на R1 пользователь меняет `width`; возврат в R5 сохраняет новый `width` и прежние S1/S2 |
| T10 | После отката на R2 пользователь меняет `posx`; возврат в R5 сохраняет новый `posx` и прежние S0/S2 |
| T11 | Чистая установка каждого EXE R0–R5 |
| T12 | Эквивалентность нового EXE и старого совместимого EXE с бинарной поставкой констант |
| T13 | Новый EXE со встроенными актуальными константами не перекрывается оставшимся старым пакетом |
| T14 | Заранее размещённый пакет будущей поставки не применяется до её активации |
| T15 | Константа A→B→A: последняя поставка содержит A, даже если значение совпадает с R0 |
| T16 | Новый динамический слой у исторического пользовательского `Node::base`; согласованные `Load` / replay |
| T17 | Компактация журнала через откаты: неизвестный v1, известный префикс, полная компактация до первого `SaveRoot` |
| T18 | Несовместимый EXE/пакет или отсутствие обязательного слоя; явный отказ без изменения USER |

Существующие тесты (`window_schema_layers_matrix`, `event_version_matrix`, `model_persistence`, overlay/distilled) покрывают **фрагменты** (в т.ч. части **T16**, **T17**), но **не** заменяют сквозной lifecycle **T01–T08**, **T11–T15** и **T18**.

---

## Roadmap: итерации 01–14

Статус выполнения — в [PROGRESS.md](./PROGRESS.md). Здесь только цели и границы.

### 01 — Аудит кода vs архитектура

- **Цель:** список противоречий (distill→USER, дубли load, release numbering, const/dynamic).
- **Область:** `src/`, `include/apptraverse/`, tests fixtures, CMake distill hooks.
- **Можно:** документы, таблицы расхождений.
- **Нельзя:** менять поведение production.
- **Результат:** отчёт в [PROGRESS.md](./PROGRESS.md) (**01A** startup/distillation, **01B** storage, **01C** const/links, **01D** redundancy/cleanup order); приоритет для 02–04.
- **T:** — | **Зависимости:** — | **Статус:** PASS *(завершён аудит; production-код не менялся)*

### 02 — Имена каталогов и номера поставок

- **Цель:** зафиксировать `state/user`, `state/R`, `active-release`.
- **Область:** docs, черновик layout; согласование с `DirectoryDomainStorage`.
- **Нельзя:** массовая миграция USER без 06.
- **Результат:** раздел [Решение итерации 02](#решение-итерации-02--каталоги-и-номера-поставок); подготовка T11.
- **T:** T11 | **Зав.:** 01 | **Статус:** PASS *(layout зафиксирован в документе; код и миграция не менялись)*

### 03 — Release config и метаданные EXE

- **Цель:** single source of truth для R на этапе сборки; встроенное значение в probe/EXE.
- **Область:** `release.json`, CMake generate, `apptraverse_release_metadata_probe`, CTest.
- **Нельзя:** разброс R по CMake defines; runtime parser; изменение Surfaces startup в 03.
- **Результат:** раздел [Решение итерации 03](#решение-итерации-03--release-config-и-встроенные-метаданные-exe); probe PASS Debug/Release.
- **T:** T12, T13 *(подготовка; полный EXE/пакет — позже)* | **Зав.:** 02 | **Статус:** PASS *(embedded R + probe; не полный lifecycle)*

### 04 — Константность и правила хранения

- **Цель:** явный контракт const vs dynamic (не только Ptr scope).
- **Область:** docs + `OverlayDomainStorage` supply constants + overlay tests.
- **Результат:** раздел [Решение итерации 04](#решение-итерации-04--контракт-const-vs-dynamic); storage PASS; dev-time Event review (Cursor rule); **без** runtime const checks в Node/Event.
- **T:** T02, T05 *(подготовка)* | **Зав.:** 01, 02, 03 | **Статус:** PASS *(storage + dev review; manifest/Surfaces wiring — 05+)*

### 05 — Полная дистилляция с lazy

- **Цель:** EXE содержит все слои и lazy-ссылки согласно R.
- **Область:** distill pipeline, `InitializeRuntimeNode`, graph export.
- **T:** T01, T06 | **Зав.:** 03, 04 | **Статус:** PASS *(build-time export + load-only consumers)*

### 06 — Headless prefab-aware proof (бывш. Surfaces startup)

- **Цель:** минимальный headless R0→R2 + prefab clone + lazy + historical base/replay + T18 unknown Event; **без** Surfaces GUI.
- **Область:** `PrefabPtr`, prefab fixture EXE, embed BACKING (05), `apptraverse_prefab_release_matrix`.
- **T:** T01, T03, T16, T18 *(фрагменты)* | **Зав.:** 05 | **Статус:** PASS *(headless prefab matrix; не Surfaces GUI)*

### 06 (отменено) — Surfaces startup из EXE

- Surfaces Demo **не** переводится на release-lifecycle / embedded startup; пункт заменён headless proof выше.

### 07 — Один бинарный пакет констант

- **Цель:** offline `state/R` + старый EXE читает через backing.
- **Область:** [`distilled_supply_package`](../../include/apptraverse/distilled_supply_package.h) — **prototype wire**: Æther `BinaryArchive` (target `supply_constant_ids` + upsert layers); fixture `export_prefab_release_r1_package` + `apptraverse_prefab_release_lifecycle_matrix`; явный `--supply-package` (без resolver / `active-release`). Файл считается корректным выходом нашего exporter; magic/CRC/corruption validation **намеренно нет**; malformed package **вне scope**; **T18** — несовместимость объектной модели/EXE, не случайное повреждение файла.
- **T:** T02, T12 *(фрагменты)* | **Зав.:** 04, 06 | **Статус:** PASS *(один R1 package; полный T12 — нет)*

### 08 — Выбор активной поставки

- **Цель:** `active-release` + `state/<R>/supply.bin` → effective RAM backing (headless fixture).
- **Область:** `tests/prefab_fixture_state.*` (`active-release` = один `uint32_t` через `BinaryArchive`); последовательное применение packages при `active > embedded`; `OverlayDomainStorage`; **без** resolver-класса, сканирования max-R, auto-write `active-release`; Surfaces не подключён; malformed files вне scope.
- **T:** T13, T14 *(фрагменты)* | **Зав.:** 03, 07 | **Статус:** PASS *(R0/R1 switch + R2 embedded vs active=1)*

### 09 — Эквивалентность пакета и полного EXE

- **Цель:** oracle: RAM backing A (R0 embed + R1 BIN) ≡ B (R1 embed); один USER — одинаковое read-only состояние через R0+active-release и R1 EXE.
- **Область (09A PASS):** `apptraverse_prefab_release_r1`; `prefab_release_r1_supply_equivalence_test`; `prefab_release_r1_user_equivalence_matrix`; op `read-user-equivalence`. **Не** чистая установка (R0 `r0-init` нужен); **не** полный T11/T12.
- **T:** T12 *(фрагмент)* | **Зав.:** 07, 08 | **Статус:** PASS *(09A scope)*

### 10 — Новый динамический слой при существующем USER

- **Цель:** S+1 без потери USER v0/v1; откат EXE не стирает USER S0/S1.
- **Область (10A PASS):** `SetWidthEvent` (S0); op `set-width-a`; `prefab_dynamic_layer_roundtrip_matrix` — цепочка **R0→R1→R2→R1→R2** на одном USER (`width` S0 + `posx` S1 + `ReplayFromBase`).
- **Область (10B PASS):** `WindowCAddedEvent`; op `create-tool-window` (`ObjId::GenerateUnique`, S0 из backing prefab 60002, historical base); поздний WindowC, R2 подставляет S1 `posx` без USER S1.
- **Область (10C PASS):** ops `delta-posx-c`, `replay-window-c`; расширенный `inspect-window-c`; `prefab_late_instance_matrix` — USER S1 на позднем WindowC, откат R1, replay. **Не** полный graph clone, **не** T07–T10, **не** R0–R5 chain.
- **Область (10D PASS):** `delta-posx-c` без materialize WindowA; матрица: USER S1 только на затронутых WindowC/base.
- **Область (10E PASS):** CPM-патч Æther `ShouldSaveVersion` — opt-in hook в `DomainGraph::Save()` **до** `SaveVersion` (прямой `SaveVersion` без фильтра). Fixture `PrefabWindow`: `Save(V1)` всегда полный слой; `ShouldSaveVersion(V1)` → `user_owned_s1`; prefab supply с S1; historical base S1 через явный `SaveVersion` (`EnsurePrefabVersionInHistoricalBase`, `user_owned_s1=false`). `OverlayDomainStorage`: легитимные **0-byte** Event base layer сохраняются; `SaveRoot(Workspace)` / `touch-save-workspace` не пишет USER S1 без пользовательского Event; `set-posx-c-prefab-default` — USER S1 при значении = prefab default, если `user_owned_s1`.
- **T:** T03, T09 *(фрагменты)*; T06, T07–T08, T10 — **не** PASS | **Зав.:** 06, 09 | **Статус:** PASS *(10A–10E scope)*

### 11 — Исторические base, компактация, replay

- **Цель:** **T16** (исторический `Node::base` + новый S); **T17** (компактация журнала через откаты и до первого `SaveRoot`).
- **Область (11A PASS):** op `compact-window-c` (`JournalRetentionPolicy.max_events=0`, `CompactJournal`, `SaveRoot` по instance WindowC); `prefab_compaction_roundtrip_matrix` — delta S1 → полная компактация (journal=0, base S1=27), cold load, откат R1 `touch-save-workspace`, `replay-window-c` ×2; Generation/ObjId/base chain и width=480; WindowA/B без лишнего USER S1; const не в USER. **Не** удаление orphaned Event-файлов; **не** изменение production `Node`/Æther.
- **Область (11B.1):** регрессия до fix — R1 `CompactJournal` сворачивал S1-journal, т.к. `CanCompactStoredState` проверял только Event ObjId.
- **Область (11B.2 PASS):** `detail::CanCompactStoredObjectVersions` в `node.h`; `CompactJournalInto` — проверка instance + historical base **до** изменения journal/base; `EventFor` использует тот же helper. `apptraverse_prefab_old_exe_compaction_guard_matrix` — R1 не компактит (journal=1), R2 replay=27; R2 full compact (11A) без регрессии. **Правило:** нельзя сворачивать journal, если на Node или base есть persisted версии выше `kCurrentVersion` текущего EXE.
- **T:** T16, T17 *(фрагменты)* | **Зав.:** 10 | **Статус:** IN_PROGRESS *(11A+11B.2 PASS; полный T16/T17 — нет)*

### C0 — Очистка архитектуры (до новой функциональности)

- **Цель:** убрать доказанно лишние обходы **до** **12R**; сохранить основания **10E**, **11B.2**, journal/base, Æther serialization.
- **C0.1 — аудит:** **PASS (doc, `b6dcdae`)** — таблица в [PROGRESS §C0.1](./PROGRESS.md#c01--аудит-избыточности-object-core) (KEEP/REMOVE/SIMPLIFY/DEFER).
- **C0.2 — минимальная очистка:** **PASS** — **C0.2.1** dead `ClonePlan` API; **C0.2.2** `BackingComparedStorageWriter::failed_`. Дальнейшие удаления в C0.2 **не** планируются (см. [PROGRESS §C0.2](./PROGRESS.md#c02--упорядоченные-минимальные-изменения)).
- **C0.3 — верификация оснований:** **PASS (doc)** — таблица инвариантов [PROGRESS §C0.3](./PROGRESS.md#c03--верификация-сохранённых-механизмов-pass); код **не** менялся.
- **Сохранено без удаления (контракт C0.3):** `EnsureObjectRegistration`; `force_materialize`; `ShouldSaveVersion`; guard **11B.2**; ключ `ObjId/ClassId/S`; unknown newer S; historical base + journal; штатный Æther; опубликованные ClassId/S0/S1 fixture Events.
- **PASS:** **C0.2** регрессии + **C0.3** верификация | **Статус:** **PASS**

### 12 — Системный origin

- **Цель:** origin у каждого клонированного экземпляра; **не** `PrefabPtr`; **без** изменения опубликованных S0 и **PrefabWindow/S1**; штатный **`BinaryArchive`**; обычные ссылки на другие объекты — как сейчас.
- **T:** T16 *(фрагмент)* | **Зав.:** 11, **C0 PASS** | **Статус:** **TODO** *(12A doc — см. ниже; код — 12R→12B)*

#### 12A — документ (выполнено, payload устарел)

**Исторический факт (`ed5eef9`):** выбран persisted-слой **`ae::Obj` S1** (`ObjId / Obj::kClassId / 1`) и отдельный `SaveVersion` в `DomainGraph::Save` — **не** перехватывает `PrefabWindow::Save(V1)`.

**Устаревшая часть 12A:** payload = голый **`ae::ObjId`**. **Не** является финальным решением; **не** реализовано в коде.

#### 12R — уточнение origin (**PASS**; **12R.1** doc; **12R.2a–12R.3** code)

**Целевой контракт (согласованный plan):** persisted-слой **`(instance ObjId, Obj::kClassId, S=1)`** с полем **штатной lazy object reference**; origin **не** Window/Node; **нет** external registry; **нет** `PrefabPtr` в production.

**12R PASS** — **12R.2b.2** (auto Save hook) + **12R.3** (финальный **`SaveRoot`/`LoadRoot`** roundtrip, lifetime-safe tests). **12B** — следующий шаг.

#### 12R.2a — базовый Obj/S1 origin facet (**PASS**, code)

**Сделано:** CPM [`aether-objects-origin-facet.patch`](../../cmake/patches/aether-objects-origin-facet.patch) на pin **`79283b6`**: `Obj::SetOriginId` / `origin_id()` / **`ShouldSaveOriginFacet(V1)`** / protected `Load|Save(V1)` + `friend DomainGraph`; **`Obj::CurrentVersion` остаётся V0**; **без** post-`Save<T>` hook и **без** Obj factory.

**Versioning fix (тот же patch, `domain.h`):** `LoadVersion`/`SaveVersion` вызывают protected **`Obj` V1** через `(T==Obj && V==1)` — иначе `HasAnyVersionedSave<Obj>` ложно false; **`ShouldSaveOriginFacet`** проверяется **отдельно** для **`T==Obj`**; штатный **`ShouldSaveVersion`** из patch **`should-save-version`** остаётся для **Derived** (**10E**, `PrefabWindow::ShouldSaveVersion`).

**Тест:** `apptraverse_object_origin_facet_test` (`RamDomainStorage`, ручной `SaveVersion`/`LoadVersion`).

**Не в scope 12R.2a:** автоматический facet save, `LoadStoredLayers` factory, `object_origin.h`, **12B**.

#### 12R.3 — финальный origin roundtrip (**PASS**)

**Тест:** **`apptraverse_object_origin_facet_test`** — cold load только при живом **`Domain`** (`WithColdLoadV0/V1`); **`TestFinalOriginRoundtripV0/V1`**: два независимых **`Domain`**, общий **`RamDomainStorage`**, **`SaveRoot`/`LoadRoot`**, **P** не в storage и не materialized, repeat **`SaveRoot`**, **`OriginFacetTargetV1Gated`** + **`ShouldSaveVersion`**.

**Не в scope:** **`object_origin.h`**, **12B**, Clone.

#### 12R.2b.2 — auto Save hook Obj/S1 (**PASS**)

**Patch:** [`aether-objects-origin-facet.patch`](../../cmake/patches/aether-objects-origin-facet.patch) (обновлён на pin **`79283b6`**):

- После успешного **`Save<T>`** ( **`LocalPersistent`**, most-derived **`T::kClassId`**, valid **`origin_id()`** ) → **`SaveVersion(V1, static_cast<Obj const&>(obj), obj_id)`**; ошибка facet пробрасывается.
- **`ShouldSaveOriginFacet(V1)`** → **`origin_.is_valid()`**; materialized origin не skip — **`Obj::Save(V1)`** **`abort()`**.
- **`ShouldSaveVersion`** для Derived без изменений (**10E**).

**Тест:** **`apptraverse_object_origin_facet_test`** — auto facet на **`Save`**, Derived S0/S1 + Obj S1, без origin, cold load, repeat Save, ручной facet roundtrip.

**Не в scope:** **`object_origin.h`**, wire **`ObjectPtrBase`**, **12B**.

#### 12R.2b.1 — Obj factory + cold LoadRoot (**PASS**)

**Patch:** [`aether-objects-obj-facet-factory.patch`](../../cmake/patches/aether-objects-obj-facet-factory.patch) (после origin-facet):

- **`Registry::RegisterClass`:** `relations[base]` для всех `cls_id != base_id` (прямые наследники **`Obj`** попадают в **`relations[Obj]`** → корректный порядок **`KnownStoredClasses`**).
- **`RegisterObjOriginFacetFactory(Registry&)`** из **`obj.cpp`:** factory **`Obj::kClassId`** — **`load`** → **`LoadVersion(V1, Obj&, id)`** (missing **V1** → OK); **`create`** → `{}`; **`save`** no-op. Регистрация при первом **`Registry::GetRegistry()`** (не static global — иначе MSVC **Release** ломает **`MakePtr`** при **`EnsureObjectRegistration`**).
- **`GetMostRelatedFactory`:** если единственный stored class — **`Obj`**, **`nullptr`** (Obj/S1-only не root).
- **`KnownStoredClasses`:** стабильный tie-break **`left < right`** при неоднозначной **`GenerationDistance`** (Release-safe sort).

**Тест:** расширен **`apptraverse_object_origin_facet_test`** — cold **`LoadRoot`**, Obj/S0 без S1, Derived+Obj S1, reject Obj-only.

**Не в scope:** auto Save hook, **`object_origin.h`**, wire **`ObjectPtrBase`**, **12B**.

#### 12R.1 — техническая проверка (PASS, doc-only)

**Срез Æther Objects:** pin **`79283b667cabf689da1059d618fe2a22abf51db4`** (+ существующие AppTraverse CPM patches). Код **не** менялся.

**Выбранный тип ссылки:** **`ae::ObjPtr<ae::Obj>`** с **`ObjFlags::kUnloadedByDefault | ObjFlags::kUnloaded`**, assign через **`ObjPtr<Obj>::Declare(CreateWith{domain}.with_id(P).with_flags(...))`** — **без** `cached_` / **без** `Load()` до Save.

| Invariant | Механизм (pin + patches) | Ограничение |
| --- | --- | --- |
| Wire без `SaveRoot(P)` | `ObjectPtrBase::Seri` пишет `ObjId`+`flags`; **`SaveRoot` только если `cached_`** (`obj_ptr_base.cpp`) | Если origin **materialized** (`cached_` set), Save **запишет** subgraph **P** в USER — **discipline API**, не автоматический запрет |
| Cold load без `Load(P)` | `Deseri`: `LoadRoot` **только** если valid и **нет** `kUnloadedByDefault` **и** **нет** `kUnloaded` | Lazy после load facet **Obj/S1** сохраняется |
| Deep Save не тянет P | `NodeVisitor<ObjPtr>` обходит **`cached()`** only (`obj_ptr.h`); unloaded → visitor skip | Сериализация поля идёт через **Dnv → ObjPtr Seri**, не через deep walk unloaded |
| Network / SharedNode | **`SharedPtr`/`ObjectLink`** — тот же wire, что **`ObjPtr`** (`object_link.h`); **`LocalPtr`** в **NetworkShared** → empty wire | Origin — **Shared** семантика; **`LocalPtr` не использовать** для origin |
| Отдельный **Obj/S1** vs **PrefabWindow/S1** | `DomainGraph::Save<T>`: `version_iterator` вызывает **`T::Save(V*)`** с ключом **`T::kClassId`** (`domain.h`); **`PrefabWindow::Save(V1)`** → `…/PrefabWindow/1`; facet origin → **`SaveVersion(V1, static_cast<Obj const&>(obj), obj_id)`** с **`Obj::kClassId/1`** | **`Obj::Save(V1)` на базе** **не** участвует в обходе **`Save<PrefabWindow>`** (отдельный метод производного класса); **не** добавлять origin в **`PrefabWindow` S1** |
| **`kCurrentVersion` Obj** | Остаётся **`Version<0>`**; **S0** прикладных классов без изменений | **Obj/1** — optional facet, как в **12A** |
| Load facet **Obj/1** | `LoadStoredLayers` → `FindClassFactory(class_id)` для каждого **`KnownStoredClasses`** (`domain.cpp` + patch **local-persistent-load**); **`KnownStoredClasses` отфильтровывает `!IsExisting`** | **Требуется** минимальная **`RegisterClass(Obj::kClassId, …)`** с `Load` → **`LoadVersion<1>(static_cast<Obj&>(*obj), storage_id)`** (новый фрагмент patch; **не** отдельный wire) |
| Unknown **Obj/1** на диске | Patch **reject-unknown-stored-class**: **`Obj::kClassId`** **исключён** из fail-closed (`class_id != Obj::kClassId`) | Старый EXE без **Load(V1)** всё ещё риск для USER с **Obj/1** — scope **12B** / **22**, не blocker **12R.1** |

**ShouldSaveOriginFacet / ShouldSaveVersion:** patch **`should-save-version`** — **`ShouldSaveVersion`** для прикладных классов (**10E**); origin facet — **`Obj::ShouldSaveOriginFacet(V1)`** (`origin_.is_valid()`; loaded → **`Save(V1)`** **`abort()`**), отдельно от Derived hooks; auto hook — **12R.2b.2**.

**Минимальные точки изменений (12R.2, только CPM + тонкий API):**

1. **`obj.h` / `obj.cpp` (Æther patch):** поле **`ObjPtr<Obj> origin_`**; **`Load`/`Save` `Version<1>`** через Dnv; **`ShouldSaveOriginFacet(V1)`**; accessors или friend **`object_origin.h`**.
2. **`domain.h` (patch, рядом с ShouldSaveVersion):** после успешного **`Save<T>`**, если `T` наследует **`Obj`**, **`SaveVersion(V1, static_cast<Obj const&>(obj), obj_id)`** (отдельный файл **`Obj::kClassId/1`**).
3. **`registry` / `LoadStoredLayers` (patch):** зарегистрировать **`Obj::kClassId`** factory с **Load** = single-facet **`LoadVersion<1>`** на **`Obj&`** subobject ( **Create/Save** factory могут быть no-op / unused для facet-only).
4. **AppTraverse `object_origin.h`:** set/get origin через **`Declare` + flags**; **запрет** document/API: не вызывать **`Load()`** на origin перед **`SaveRoot(instance)`**.
5. **Без изменений wire:** **`ObjectPtrBase`**, **`BinaryArchive`**, новых managers/registries.

**Blockers:** **нет** противоречия «невозможно на штатном wire»; **есть** **операционный** риск **`cached_` → SaveRoot(P)** и **обязательность** Obj factory для load **Obj/1** (минимальный patch, не новый формат).

**Отвергнуто для origin:** голый **`ObjId`** (12A черновик); **`LocalPtr`** (network); **`PrefabPtr`**; materialized **`ObjPtr`** без lazy flags.

#### 12B — origin roundtrip (**PASS**)

Первый новый код после **C0**/**12R** (не универсальный Clone): **P(const) → A(dynamic)**, `origin(A)→P`; новый ObjId у **A**; штатный Save/Load; origin без load **P**; тело **P** не в USER.

**Решение (12B):** отдельный **`object_origin.h`** **не** добавлялся — для roundtrip достаточно **`ae::Obj::SetOriginId` / `origin_id()`** и auto **`Obj/S1`** из **12R.2b.2**.

#### 12B.1 — P(const) → A(dynamic) overlay roundtrip (**PASS**)

**Тест:** `TestConstPrefabDynamicInstanceRoundtrip` в **`apptraverse_object_origin_facet_test`**: **P** в **BACKING**; **`OverlayDomainStorage`** + **`supply_constant_ids={P}`**; **A** с новым ObjId, поля как fixture prep (без Clone), **`SetOriginId(P)`**, **`SaveRoot(A)`** → **Derived S0** + **Obj/S1** в USER (без **Derived/S1**); cold **`LoadRoot(A)`** без materialize **P**; repeat **`SaveRoot(A)`** на **новом** **`DomainGraph`** после load; **P** не в USER, **BACKING** неизменен.

#### 12B.2 — disk/process cold roundtrip (**PASS**)

**Тест:** тот же **`apptraverse_object_origin_facet_test`** (`OriginFacetTargetV1`, **P=81001**, **A=81002**, value **150**, tag **251**), режимы **`--write <state-root>`** / **`--read <state-root>`** в **двух отдельных процессах** (`apptraverse_object_origin_disk_roundtrip_test` + `object_origin_disk_roundtrip_run.cmake`).

**Process 1 (`--write`):** **`DirectoryDomainStorage`** **`backing/`** + **`user/`**; **`SaveRoot(P)`** в BACKING; **`OverlayDomainStorage(USER, BACKING, {P})`**; **`SaveRoot(A)`** с **`SetOriginId(P)`** без **`LoadRoot(P)`**. **Process 2 (`--read`):** cold overlay → **`LoadRoot(A)`** → repeat **`SaveRoot(A)`**.

**Диск:** BACKING **P** — **Derived/S0**, **Derived/S1**, штатный **Obj/S0**, нет **Obj/S1**; USER **A** — **Derived/S0**, **Derived/S1**, **Obj/S0**, **Obj/S1**; USER **P** — пусто; BACKING **P** byte-for-byte неизменен после repeat save в Process 2.

**CMake:** `apptraverse_directory_domain_storage_test` и `apptraverse_overlay_domain_storage_test` вынесены из **`APPTRAVERSE_BUILD_EXAMPLES`** (регрессия storage доступна при **`APPTRAVERSE_BUILD_EXAMPLES=OFF`**).

#### 12A — справка (слой и отвергнутые альтернативы)

**Почему не другие варианты (кратко):**

| Отвергнуто | Причина |
| --- | --- |
| `PrefabPtr<T>` / поле в Window | origin не прикладной; не для любого `ae::Obj`. |
| Параллельный реестр / map instance→origin | вне штатной сериализации. |
| `PrefabWindow` S2 / новый S прикладного класса | смешивает origin с posx S1; другой `ClassId`; ломает семантику fixture S1. |
| Промежуточный `AppTraverseObj` только под Node | не покрывает «любой `ae::Obj`» единообразно; лишний `ClassId` на каждом типе. |
| Расширение существующего **S0** прикладного класса | нарушает «не менять опубликованные S0». |

**Почему отдельный `Obj::kClassId/1`:** `DomainGraph::Save<T>` для `PrefabWindow` пишет только **`PrefabWindow::kClassId`** версии; facet origin — **отдельный** `SaveVersion` для **`Obj::kClassId/1`** (см. **12R**).

**Совместимость слоёв (целевая):** S0 прикладных классов и **PrefabWindow/S1** без изменений; **Obj/S1** — только при заданном origin; wire origin — **lazy reference**, не materialize **P** в USER (**12R**).

### 13 — Универсальный Clone (**IN_PROGRESS**)

- **Цель:** **P→A→B→C**; Clone const и dynamic; общий устойчивый **origin=P** на каждом новом экземпляре; remap, aliases, cycles, const referents; lazy; Clone копирует **текущее каноническое** состояние источника (включая несохранённые in-memory изменения); **без** Window-only fixture API.
- **Зав.:** **12B PASS** | **T:** T03, T09 *(фрагменты)* | **Не смешивать** с **14** (авто-эволюция **S** / historical base).

#### 13.1 — Native Clone audit and implementation contract

Doc-only аудит на pin **`79283b667cabf689da1059d618fe2a22abf51db4`** с применёнными CPM patches (**origin facet**, **Obj facet factory**, **domain registered objects**, и др.). Код AppTraverse **не** менялся.

##### Проверенные факты

| Область | Факт | Где |
| --- | --- | --- |
| **`ObjPtr<T>::Clone`** | Вызывает **`DomainGraph::LoadCopy<T>(id(), obj_id)`**; новый **`ObjPtr`** с **`ObjId::GenerateUnique()`** (или caller-supplied id); снимает **`kUnloaded`** у клона | `obj_ptr.h` (`Clone` / `Clone(ObjId)`) |
| **`LoadCopyImpl`** | **`ConstructObj` + `LoadStoredLayers(ptr, ref_id)`** — читает **persisted** слои **`ref_id`**, **не** тело **`domain->Find(ref_id)`** | `domain.cpp` |
| **`LoadRoot` / `SaveRoot`** | **`LoadRoot`**: factory + **`LoadStoredLayers`**; **`SaveRoot`**: **`factory->save`** (обход версий + auto **`Obj/S1`** при origin — patch **origin-facet**) | `domain.cpp`, `domain.h` |
| **`Obj::SetOriginId` / `origin_id()`** | Lazy **`ObjPtr<Obj>`** + **`Obj/S1`**; **`Save(V1)`** abort если **`origin_.is_loaded()`** | `obj.h`, `obj.cpp`, patch |
| **`ObjectPtrBase` wire** | **`Seri`**: id + flags + optional embedded **`SaveRoot(cached)`**; **`Deseri`**: id + flags, lazy load по flags | `obj_ptr_base.cpp` |
| **AppTraverse scratch save** | **`SaveObjectGraphToScratch`**: **`Find` + `factory->save`** → **`RamDomainStorage`** (живой граф, **`LocalPersistent`**) | `object_serialization.cpp` |
| **Fixture «Clone»** | **`CloneWorkspaceFromPrefabs`** — **ручная** сборка графа + **`PrefabPtr`**; **`CreateToolWindowInstanceFromBacking`** — ручное копирование полей; **нет** вызовов **`ObjPtr::Clone`** в репозитории | `prefab_release_model.cpp`, `prefab_ptr.h` |
| **Origin roundtrip** | **12B**: **`SetOriginId(P)`** + **`SaveRoot`**, без **`LoadRoot(P)`**; не доказывает универсальный Clone | `object_origin_facet_test.cpp` |

##### Контракт Clone (целевые свойства **P→A→B→C**)

1. Каждый клон получает **новый ObjId** (кроме const **P** и внешних boundary id).
2. Копируется **текущее каноническое** состояние источника, **включая** in-memory изменения **без** предварительного **`SaveRoot`** на USER/BACKING.
3. У **A**, **B**, **C** устойчивый **`origin_id() == P`** (const prefab / supply constant).
4. **`Clone(B)` из A** не меняет origin на **A**; **B** и **C** по-прежнему указывают на **P**.
5. Источник **не** изменяется.
6. Const **P** остаётся в **BACKING**; тело **P** **не** записывается в **USER**.
7. Ссылки на **внешние** const-объекты (boundary) сохраняют **исходные ObjId**.
8. Ссылки **внутри** клонируемого closure **remap** на новые экземпляры.
9. **Aliases**, **cycles**, **lazy** refs сохраняют семантику (один new id на old id в closure).
10. Результат проходит **`SaveRoot` → cold `LoadRoot`** (как **12B**).
11. Только штатные **`ObjPtr`**, **`DomainGraph`**, **`BinaryArchive`**, persisted **`Obj/S1`** — **без** ручных origin-таблиц, production **`PrefabPtr`**, отдельного serializer.
12. **Dynamic source без устойчивого P:** **явная ошибка** — **OPEN** детали policy (см. ниже); **не** silent fallback на source ObjId как «origin».

##### Dynamic source без устойчивого **P** (отдельно)

Если источник — dynamic экземпляр **без** valid **`origin_id()`** и caller **не** передал const **`stable_origin P`**: **отказ** (**`MissingStableOrigin`** / аналог). **Не** использовать ObjId источника как origin. **OPEN:** разрешён ли clone такого источника **только** при явном **`CloneOptions::stable_origin`** от caller (const id из overlay boundary) — **да**; без этого — **fail**.

##### Gap analysis: `ObjPtr<T>::Clone` vs контракт

| # | Свойство контракта | Текущее поведение **`ObjPtr::Clone`** | Функция / файл | Статус | Минимальное изменение для **13** |
| --- | --- | --- | --- | --- | --- |
| 1 | Новый ObjId | **Да** — **`GenerateUnique()`** или аргумент | `obj_ptr.h` | Реализовано | Переиспользовать allocator в AppTraverse clone API |
| 2 | Canonical in-memory state | **Нет** — только **`LoadStoredLayers(..., ref_id)`** из storage | `domain.cpp` **`LoadCopyImpl`** | **Отсутствует** | **Не** расширять **`LoadCopy`**; snapshot через **`SaveObjectGraphToScratch`** (**13.2+**) |
| 3 | **`origin == P`** на цепочке | **Частично** — если у source есть **`Obj/S1`** в storage, facet загрузится на клон; const **P** без **`Obj/S1`** → **нет** origin на клоне | `LoadCopyImpl`, **12R** facet | **Не доказано** для **P→A** | После materialize: **`SetOriginId(P)`** + **`SaveRoot`** (как **12B**) |
| 4 | **B** из **A**, origin остаётся **P** | Копирует **`Obj/S1`** source **A** → lazy ref **P** (если слой есть) | `LoadCopyImpl` | **Не доказано** E2E | Тест **13.5**; при scratch-clone — явный **`SetOriginId(P)`** на каждом корне |
| 5 | Источник не меняется | **Да** — read-only load из storage | `LoadCopyImpl` | Реализовано | — |
| 6 | **P** не в USER | **Нет** — **`LoadCopy`** не знает overlay/boundary; может materialize const тела при save walk | — | **Отсутствует** | **`CloneBoundary`** + persist только closure в USER (**13.2+**) |
| 7 | Внешние const refs | **Нет** — вложенные **`ObjPtr`** в слоях десериализуются с **теми же ObjId**; remapping **отсутствует** | `ObjectPtrBase` **`Deseri`**, `LoadVersion` | **Отсутствует** | Remap-pass по scratch buffers (**13.3+**) |
| 8 | Internal refs remap | **Нет** — duplicate subgraph shares **old** ids | wire **`ObjPtr`** | **Отсутствует** | Remap closure **I** (**13.3+**) |
| 9 | Aliases / cycles | **Нет** отдельной логики; load/save **cycle_detector** на одном id | `DomainGraph::Save`/`Load` | **Не доказано** для clone | Единый **old→new** map (**13.4**) |
| 10 | Lazy semantics | Клон получает flags из **`Clone()`** (clears **`kUnloaded`**); nested lazy — как в wire source storage, **не** как live object | `obj_ptr.h`, `obj_ptr_base.cpp` | **Частично** | Scratch save с live domain + не грузить **P** (**13.4**) |
| 11 | **`SaveRoot` → cold `LoadRoot`** | **Не проверено** для **`Clone`** (в repo **0** call sites) | — | **Не доказано** | CTest **13.2** / **13.6** |
| 12 | Без **`PrefabPtr`/ручных таблиц** | **`Clone`** не использует **`PrefabPtr`**; fixtures — вне scope | tests | N/A (fixtures) | Library **`object_clone.*`**, не fixture graph builder |

**Вывод:** имя **`Clone`** у **`ObjPtr`** — **storage duplicate** **`ref_id → copy_id`**, **не** универсальный Clone **13**. Менять **`ObjPtr::Clone`** в Æther **не** требуется для **13**; достаточно **не использовать** его как API и ввести AppTraverse **canonical scratch clone** на **`DomainGraph` + scratch + remap**.

##### Почему не `DomainGraph::LoadCopy` как основа **13**

**`LoadCopyImpl`** не читает **`domain->Find(ref_id)`**; unsaved поля источника **теряются**. После **`SaveRoot`** + mutate без save **`Clone`** вернёт **устаревший** persisted snapshot. Доказательства эквivalenz in-memory ↔ **`LoadCopy`** **нет**.

Штатный путь canonical snapshot: **`SaveObjectGraphToScratch`** (`object_serialization.cpp`) — **`factory->save`** на **`DomainGraph`** (**`LocalPersistent`**), тот же обход что **`SaveRoot`**, включая auto **`Obj/S1`** (**12R** patch).

##### Минимальный предлагаемый API (**13.2+**)

```cpp
// include/apptraverse/object_clone.h (новый; не object_origin.h)
struct CloneOptions {
  ae::ObjId stable_origin;           // required P for P→A→B→C
  std::set<ae::ObjId> boundary_ids;  // const / supply_constant_ids ∪ {P}
};
// Returns new root id or error (MissingStableOrigin, …)
Result<ae::ObjId> CloneCanonicalRoot(
    ae::Domain& source, ae::Obj& source_root, ae::Domain& target,
    CloneOptions const& options);
```

**13.2** может реализовать **degenerate** case: closure **I** = `{source_root}` only, **без** remap pass (нет **`ObjPtr`**-рёбер), **`boundary_ids = {P}`**, overlay USER persist + cold load.

##### Границы ответственности

| Компонент | Ответственность |
| --- | --- |
| **`ObjPtr<T>`** | Живые ссылки, lazy load, **legacy** storage **`Clone`** — **не** расширять для **13** |
| **`DomainGraph`** | **`Save`/`Load`/`SaveRoot`/`LoadRoot`**, **`cycle_detector`**, **`Obj/S1`** hook (patch) — **использовать**, не дублировать |
| **`Obj`** | **`SetOriginId` / `origin_id()`**, facet **S1** — origin на клоне после materialize |
| **AppTraverse `object_clone`** | Snapshot, closure, remap, boundary, orchestration **`LoadRoot`/`SaveRoot`**, ошибки policy |

**`graph_walk.h` / live walk:** только UI/presentation; closure Clone = **Save-walk** (**scratch**), не **`CollectLiveReachableObjects`**.

##### Алгоритм **Canonical scratch clone** (выбранный для **13.2+**)

Единая сериализация Æther; **вторая** ad-hoc система полей **не** создаётся.

1. **Вход:** **`Domain&` source**, **`Obj&` source_root**, **`CloneOptions`**: **`stable_origin P`**, **`CloneBoundary boundary_ids`**, **`IdAllocator`**, целевой **`Domain&` target** (тот же или новый **`IDomainStorage`**).
2. **Проверка origin-policy:** если источник dynamic и политика требует **P**, но **`P`** invalid / не const-stable → **fail** (п.10).
3. **Snapshot (in-memory):** **`RamDomainStorage scratch`**, **`SaveObjectGraphToScratch(source_root, scratch, LocalPersistent)`**.
4. **Closure **I**:** объекты с ненулевыми слоями в **`scratch.state`**, которые входят в копируемый подграф. **Корень Clone всегда в **I****, даже если его **ObjId** ∈ **`boundary_ids`** (типично **P→A**: копируется **P**, **P** остаётся const в BACKING). **`boundary_ids`** исключает **внешние** referents из **I**, **не** сам root. Объекты только в ссылках без собственных слоёв в scratch **не** становятся узлами **I** (**13.3+** remap). **13.2:** **I** = `{source_root}` единственный; иначе **`UnsupportedGraph`**.
5. **Remap:** для каждого **`id ∈ I`** выделить **`new_id = allocator()`**, **`remap[id]=new_id`**. Id ∉ **I** (const/external) → **identity** в remap.
6. **Переписать scratch под новые id:** для каждого объекта **`id ∈ I`**: (a) перенести map **`scratch.state[id]` → `scratch.state[remap[id]]`**; (b) в каждом layer buffer выполнить **remap ObjId references** (штатный **`BinaryArchive`** / domain buffer visitor — деталь реализации **13.3**). Aliases: один **`new_id`**. Cycles: без спец. case.
7. **Materialize в target `Domain`:** временный **`Domain scratch_domain(scratch)`** **или** inject layers в storage target; **`DomainGraph load_graph(&target_domain)`**; для каждого **`new_root_id`** (образ **`source_root`**) **`LoadRoot(new_root_id)`** — порядок: топологически по зависимостям (как при обычном load graph). **Lazy:** не вызывать **`LoadRoot`** для **P** / boundary.
8. **Origin на клоне:** **`clone_root.SetOriginId(P)`** (lazy **`ObjPtr`**, без **`origin_.Load()`** до save — как **12R**).
9. **Persist:** **новый** **`DomainGraph save_graph(&target_domain)`** (избегать save-no-op на том же graph после load — урок **12B**); **`SaveRoot(clone_root, new_root_id)`** в USER storage.

**Const/dynamic split:** **`CloneBoundary`** = union(caller const set, overlay **`supply_constant_ids`**, stable **P** для origin). В **I** попадают клонируемые узлы closure (root **всегда**); const **P** как **origin** не дублируется в USER (**12B**), но **P→A** копирует **тело P** в **A** под новым id.

**P→A→B→C:** **`Clone(A)→B`**, **`Clone(B)→C`**; **`P`** один и тот же; **`origin_id`** на **B** и **C** = **P** (не **A**).

#### Разбиение реализации

| Этап | Scope | Критерий PASS |
| --- | --- | --- |
| **13.1** | Audit + контракт (этот раздел) | Doc-only (audit) |
| **13.2** | Один объект, без **`ObjPtr`**-рёбер (**PASS**) | **`CloneCanonicalRoot`** + **`apptraverse_object_clone_test`**: memory **1601/252** vs disk **150/251**; USER **A** + origin **P**; cold **`LoadRoot`** |
| **13.3** | Граф + **remap** | Два+ объекта, internal refs remapped; const ref unchanged |
| **13.4** | Aliases, cycles, lazy | Shared **`ObjPtr`**, cycle A↔B, lazy ref без load const |
| **13.5** | Dynamic + **P→A→B→C** | Origin chain; **MissingStableOrigin** при отсутствии **P** |
| **13.6** | Disk / process matrix | Как **12B.2**: subprocess, BACKING **P**, USER только клоны |

**OPEN (зафиксировано):** dynamic источник без устойчивого **P** → **ошибка**, не silent fallback.

**Файлы (план):** `include/apptraverse/object_clone.h` (+ `.cpp`); тесты/fixtures по этапам **13.2–13.6**; **без** изменений Æther wire / **ObjectPtrBase** в **13.1**.

### 14 — Автоматическая эволюция S и history

- **Цель:** новый **S** только из **origin**, если нет USER-override; historical base расширяется **отдельно**; старые **S0** не заменяются; replay/compaction + guard **11B.2**; основные **T16/T17**.
- **T:** T16, T17 | **Зав.:** 13 | **Статус:** TODO

### 15 — Единый Release R/E/B/S

- **Цель:** самодостаточный embedded EXE; первый запуск с пустым USER; const-дельты; active-release; эквивалентность binary package и full EXE; **T11–T15**.
- **T:** T11–T15 | **Зав.:** 14 | **Статус:** TODO

### 16 — Полный R0–R5→R0→R5

- **Цель:** **R3** logic-only, **R4** constants, **R5** новый dynamic **S2**; откаты и изменения после отката; сохранение USER/lazy/history; **T01–T18** в prototype scope.
- **T:** T01–T10, T15, T18 *(фрагменты)* | **Зав.:** 15 | **Статус:** TODO

### 17 — Application / UI integration

- **Цель:** общий runtime; UI mirror; platform presenters; второй независимый consumer; **не** дублировать уже существующий integration framework в application-ветке.
- **T:** — | **Зав.:** 16 | **Статус:** TODO *(частично параллельно — см. координацию)*

### 18 — Optional resources и cache

- **Цель:** EN embedded; языки по требованию; канонические пакеты; удаляемый cache; optional per-layer LZMA.
- **T:** — | **Зав.:** 17 | **Статус:** TODO

### 19 — Æther updates

- **Цель:** простой **ArtifactSource**; штатно сериализованные control messages; потоковая передача файлов; **FileSource** / **ÆtherSource**; basic resume через `.partial`; end-to-end activation; **без** production recovery/security.
- **T:** T12–T14 *(расширение)* | **Зав.:** 15, 18 | **Статус:** TODO

### 20 — SharedNode

- **Цель:** опциональная репликация; snapshot, Events, ACK; новые объекты + origin; **без** больших file/media payload в journal.
- **T:** — | **Зав.:** 13, 17 | **Статус:** TODO

### 21 — Plugins

- **Цель:** desktop DLL/SO; независимые версии и USER; совместимая load/restart; save plugin state; остальные платформы постепенно.
- **T:** — | **Зав.:** 17, 19 | **Статус:** TODO

### 22 — Production-hardening — отдельно, после функционального прототипа

- **Только будущий этап:** подписи, hashes, recovery, atomic update, crash/power-loss, лимиты, defensive parsing, malformed/incompatible files, сложные downgrade policies, plugin ABI/security/sandbox, дополнительные race/corner cases.
- **Не** ставить зависимостями для **12–21**, кроме необходимого для корректной объектной семантики.
- **T:** — | **Зав.:** 21 | **Статус:** TODO

---

## Unified integration (историческая координация → merge)

| Ветка | Роль сейчас |
| --- | --- |
| **`integration/unified-application`** | **Единственная рабочая** ветка: Object Core + Windows Application + SharedNode restore + объединённый CMake/тесты. |
| **`integration/serialization-surfaces`** | **Исторический baseline** Object Core (origin, Clone slice **13.2**, headless **R0–R5**, **T01–T18** fixtures). |
| **`integration/application-versioning`** | **Исторический baseline** application framework (**I0**, **9A–9C**, `WinApplicationHost`, runtime distill WindowCatalog). |

**Merge:** **`4848402`**. **U1** (`8835d26`) — catalog model test; **U2** — manual **`win32_window_catalog_demo`**.

**Правила (на unified):** **не** дублировать реализации; **не** переносить **`PrefabPtr`** в production; полная интеграция R/E/B/S в EXE (**9D–9H**, **14–16**) — **ещё не выполнена**; origin/Clone/S в production apps — после **U6** и regressions. **U4–U5** — аудит и точечная очистка, **без** новой архитектуры приложения.

---

## Контрольные точки A–F (v0.3)

| Точка | Итерации | Зависимости | Критерий PASS (функциональный E2E) |
| --- | --- | --- | --- |
| **A. Object Core** | **C0**, **12R–14** | 11 | Origin (lazy ref), Clone, S/history/replay/compaction guard на **библиотечном** API. |
| **B. Local App** | 15–16 | A | Embedded startup, USER, полный **R0–R5** на одном USER; UI необязателен на этом шаге. |
| **C. Resource App** | 18 | B | Optional components, locale, cache — рабочий сценарий загрузки/переключения. |
| **D. Updatable App** | 19 | B, 15 | Получение и применение совместимой поставки (file/Æther), без hardening. |
| **E. Shared App** | 20 | A, 17 | SharedNode репликация подграфа с Clone/origin. |
| **F. Extensible App** | 21 | 17, 19 | Plugin load/save/reconnect на одной платформе; остальные платформы — инкрементально. |

**PASS** контрольной точки = рабочий end-to-end сценарий этапа, **не** наличие классов, заглушек или изолированных unit-тестов.

---

## Обязательный этап очистки (историческая заметка → **C0**)

Нормативный порядок очистки — **[C0](#c0--очистка-архитектуры-до-новой-функциональности)** (**C0.1** аудит → **C0.2** → **C0.3**). Ниже — ранний чеклист (01D); не дублирует таблицу **C0.1**.

Проверить и **после анализа** убрать дубли, не «Ensure» слепо:

| Область | Вопрос |
| --- | --- |
| `EnsureObjectRegistration` и аналоги | Регистрация нужна (реальное падение без неё); дубли на уровнях demo/test — лишние? |
| Runtime distillation → USER | Начальные dynamic файлы в USER против «только EXE + lazy» |
| Параллельные пути load | Checkpoint vs Directory vs overlay vs embedded |
| Wrappers/managers | Не несут ли скрытый fallback |
| Fallback / тихие no-op | Маскируют нарушение инвариантов |
| Дубли version/config | R vs S vs EXE revision |
| Временные обходы fixtures | `force_materialize`, post-compact save — production или только контракт? |
| Единое понятие Release R | Сейчас: revision EXE в tests, packages, embedded — не R |

**Правило:** `EnsureObjectRegistration` не удалять без замены. Удаление только с тестом, доказывающим эквивалент.

Не создавать совместимость со старыми experimental development-форматами без требования новой схемы.

---

## Критерии завершения программы

**Функциональный прототип (итерации 01–11, C0, 12–21; контрольные точки A–F):**

- **T01–T18** PASS в Debug/Release на согласованном CI-subset **в поддерживаемом прототипом сценарии** (полный lifecycle **T07–T08**, **T09–T10**, **T16–T17** — через **14** и **16**, не через hardening).
- Документированный release config; один способ выбора R; USER — пользовательская динамика; константы из `state/R` или EXE.
- Откат и повторное обновление без потери S и без ghost replay; guard компактации **11B.2** сохранён.

**Production-hardening (итерация 22)** — отдельное завершение, **не** блокирует объявление функционального прототипа.

Итерации **01–11** зафиксированы фактически; **12–21** — целевой roadmap v0.3 на базе **`66515d8`**.
