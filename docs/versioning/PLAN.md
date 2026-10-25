# AppTraverse — план архитектуры версионирования состояния и поставок

**Статус документа:** архитектурный контракт и roadmap. Это не описание уже реализованной сквозной release-модели.

**Архитектурная редакция:** **AppTraverse Libraries Architecture RU v0.3 (25.10.2026)** — приоритет roadmap после итерации **11**; подтверждённый код на **`66515d8`**.

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

**Production-hardening** (подписи, recovery, malformed files, sandbox plugins и т.д.) — **отдельная заключительная фаза** ([итерация 22](#22--production-hardening--отдельно-после-функционального-прототипа)); **не** является условием завершения функционального прототипа (итерации **12–21**).

---

## Текущее состояние (HEAD `66515d8`)

История итераций **01–11** не переписывается.

| Область | Статус |
| --- | --- |
| **01–09** | **PASS** в заявленном fixture scope (headless prefab, supply, active-release, equivalence). |
| **10A–10E** | **PASS** — R0–R2, USER S0/S1, WindowC, rollback, `ShouldSaveVersion`, root-save. |
| **11A** | **PASS** — полная compaction WindowC на R2. |
| **11B.1** | Обнаруженная и воспроизведённая регрессия (R1 compact без guard). |
| **11B.2** | **PASS** — guard компактации при неизвестных persisted S у Node и historical base. |
| **Итерация 11** | **IN_PROGRESS** (fixture compaction/replay; полный **T16/T17** — нет). |
| **T16/T17** | Частичное покрытие; **не** PASS. |
| **Полный R0–R5**, **T01–T18** | **Не** завершены. |

**Архитектурный пробел (v0.3):** **origin** сейчас только в тестовом **`PrefabPtr<T>`**; **Clone** частично вручную в fixture; **нет** универсального production **Clone/origin** и системного разрешения нового **S** для произвольного Node — следующий приоритет: итерации **12–14**.

Fixture **PASS** ≠ готовность библиотечного механизма в production API.

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

### 12 — Системный origin

- **Цель:** origin у каждого клонированного экземпляра (не только `PrefabPtr`); хранение в объектной системе **без** изменения опубликованных S0; Save/Load через стандартный Æther serializer; обычные `ObjPtr` — обычные ссылки.
- **T:** T16 *(фрагмент)* | **Зав.:** 11 | **Статус:** TODO

### 13 — Универсальный Clone

- **Цель:** Clone const → dynamic; Clone dynamic; цепочка P→A→B→C (новые ObjId, общий origin=P); Clone подграфа (remap, aliases, cycles, const refs); lazy сохраняется; **без** привязки к Window-классам fixture.
- **T:** T03, T09 *(фрагменты)* | **Зав.:** 12 | **Статус:** TODO

### 14 — Универсальные S и historical base

- **Цель:** перенести семантику `MaterializePrefabBackedWindow` / `EnsurePrefabVersionInHistoricalBase` на системный уровень; новый S из origin, если USER без слоя; старые USER-слои не перезаписываются; historical base независим от текущего объекта; завершить основные сценарии **T16/T17** (частичная компактация, replay); сохранить guard **11B.2**.
- **T:** T16, T17 | **Зав.:** 13 | **Статус:** TODO

### 15 — Единая модель поставок

- **Цель:** R / E / B / S; самодостаточный embedded initial graph; первый запуск с пустым USER **без** обязательной runtime-distillation; const-дельты, active-release; эквивалентность full EXE и EXE+package; основные **T11–T15**.
- **T:** T11–T15 | **Зав.:** 14 | **Статус:** TODO

### 16 — R3–R5 и полный lifecycle

- **Цель:** R3 logic-only, R4 constants, R5 dynamic S2; **R0→R5→R0→R5** на одном USER; изменения после отката; Load/replay; неизвестные старому EXE слои сохраняются; функциональная матрица **T01–T18** в поддерживаемом прототипом сценарии.
- **T:** T01–T10, T15, T18 *(фрагменты)* | **Зав.:** 15 | **Статус:** TODO

### 17 — Библиотечная интеграция и UI

- **Цель:** startup embedded B + selected R + USER; ModelObjectProxy, UI mirror, Presenters, Surfaces; native UI open/close/recreate; второй потребитель библиотек со своим root/Node/Event; **не** привязка к одной demo.
- **T:** — | **Зав.:** 16 | **Статус:** TODO

### 18 — Optional resources, localization и cache

- **Цель:** embedded EN; доп. языки/ресурсы как канонические компоненты; `requested_locale` / `effective_locale`; local file source; производный cache, per-layer LZMA; функциональные сценарии **без** production-hardening.
- **T:** — | **Зав.:** 17 | **Статус:** TODO

### 19 — Получение обновлений через Æther

- **Цель:** простой **ArtifactSource** (local/file, затем Æther adapter); control plane сообщения; получение пакетов и EXE; staging и применение совместимой поставки; end-to-end update **без** сложного recovery и проверок повреждений.
- **T:** T12–T14 *(расширение)* | **Зав.:** 15, 18 | **Статус:** TODO

### 20 — SharedNode integration

- **Цель:** snapshot/Event/ACK; репликация поддерживаемого подграфа; события с новыми объектами; системный Clone/origin; локальная работа **без** обязательной сети.
- **T:** — | **Зав.:** 13, 17 | **Статус:** TODO

### 21 — Plugins и платформы

- **Цель:** независимые R/E/B/S и USER плагина; простая загрузка совместимого DLL/SO; plugin-owned объекты; save после disconnect/reconnect; общая модель Windows/macOS/Linux/Android/iOS/Web — **постепенно**, не блокируя первую рабочую систему всеми платформами.
- **T:** — | **Зав.:** 17, 19 | **Статус:** TODO

### 22 — Production-hardening — отдельно, после функционального прототипа

- **Только будущий этап:** подписи, hashes, recovery, atomic update, crash/power-loss, лимиты, defensive parsing, malformed/incompatible files, сложные downgrade policies, plugin ABI/security/sandbox, дополнительные race/corner cases.
- **Не** ставить зависимостями для **12–21**, кроме необходимого для корректной объектной семантики.
- **T:** — | **Зав.:** 21 | **Статус:** TODO

---

## Контрольные точки A–F (v0.3)

| Точка | Итерации | Зависимости | Критерий PASS (функциональный E2E) |
| --- | --- | --- | --- |
| **A. Object Core** | 12–14 | 11 | Системный origin + Clone + S/historical base/replay/compaction guard на **библиотечном** API; не только prefab fixture. |
| **B. Local App** | 15–16 | A | Embedded startup, USER, полный **R0–R5** на одном USER; UI необязателен на этом шаге. |
| **C. Resource App** | 18 | B | Optional components, locale, cache — рабочий сценарий загрузки/переключения. |
| **D. Updatable App** | 19 | B, 15 | Получение и применение совместимой поставки (file/Æther), без hardening. |
| **E. Shared App** | 20 | A, 17 | SharedNode репликация подграфа с Clone/origin. |
| **F. Extensible App** | 21 | 17, 19 | Plugin load/save/reconnect на одной платформе; остальные платформы — инкрементально. |

**PASS** контрольной точки = рабочий end-to-end сценарий этапа, **не** наличие классов, заглушек или изолированных unit-тестов.

---

## Обязательный этап очистки (в рамках 16–22)

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

**Функциональный прототип (итерации 01–21, контрольные точки A–F):**

- **T01–T18** PASS в Debug/Release на согласованном CI-subset **в поддерживаемом прототипом сценарии** (полный lifecycle **T07–T08**, **T09–T10**, **T16–T17** — через **14** и **16**, не через hardening).
- Документированный release config; один способ выбора R; USER — пользовательская динамика; константы из `state/R` или EXE.
- Откат и повторное обновление без потери S и без ghost replay; guard компактации **11B.2** сохранён.

**Production-hardening (итерация 22)** — отдельное завершение, **не** блокирует объявление функционального прототипа.

Итерации **01–11** зафиксированы фактически; **12–21** — целевой roadmap v0.3 на базе **`66515d8`**.
