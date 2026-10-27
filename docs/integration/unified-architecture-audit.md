# U4 — Аудит объединённой архитектуры (`integration/unified-application`)

**Статус:** U4 PASS (audit-only). **База:** `53245f0` (после U3). Production-код, тесты, CMake и форматы не менялись; сборка не запускалась.

**Источники:** только исходники ветки (файлы/функции указаны в каждом пункте). Номера строк — на момент `53245f0`.

**Связанные документы:** [PLAN §Unified](../versioning/PLAN.md#unified-integration-историческая-координация--merge), [PROGRESS §Unified](../versioning/PROGRESS.md#unified-integration-integrationunified-application), [I0 ledger](./I0_cleanup_ledger.md), [application-versioning](./application-versioning.md).

---

## A. Current Architecture

### A.1 Object Core

| Компонент | Файлы | Роль |
| --- | --- | --- |
| Объектная модель Æther | `aether-objects` @ `79283b6` + 9 patches (`cmake/patches/`) | `Domain`, `DomainGraph`, `ObjPtr`, `IDomainStorage`, `Version<S>` Load/Save, `GraphSerializationScope::{LocalPersistent, MaterializedPublication}`, `force_materialize`, origin facet |
| Макросы классов | `include/apptraverse/object_macros.h` | `APPTRAVERSE_NAMED_OBJECT` / `APPTRAVERSE_OBJECT` (ClassId = CRC32 имени), `APPTRAVERSE_REGISTER` в `.cpp`, `EnsureObjectRegistration()` |
| Node | `include/apptraverse/node.h`, `node_for.h` | `base` (исторический snapshot, `SharedPtr<Node>`), `journal` (`EventRecord`), `Commit` → `CommitInto` → `InsertEvent` → `TryInsertEvent` (требует `base.is_valid() && base.is_loaded()`), `CaptureBaseStateInto`, `CompactJournalInto` с guard `CanCompactStoredObjectVersions` + `Event::CanCompactStoredState` (**11B.2**), `AdoptMaterializedPersistentCheckpointJournal` |
| Runtime Node | `include/apptraverse/runtime_node.h`, `distill.h` | `InitializeRuntimeNode` (создаёт base), `FinalizeDistilledGraph`, `StabilizeDistilledPersistentSnapshot` (build-time нормализация) |
| Сериализация | `src/object_serialization.cpp`, `src/publication_serialization_scope.cpp` | publication (initial/structural), persistent snapshot / checkpoint |
| Clone (13.2) | `include/apptraverse/object_clone.h`, `src/object_clone.cpp` | `CloneCanonicalRoot`: один объект без `ObjPtr`, origin через `Obj::SetOriginId`; только совместимость **13.2** |
| SharedNode | `src/shared_node.cpp`, `shared_sync_runtime.cpp`, `link.cpp`, `sync_frame.cpp`, `memory_transport.cpp`, `shared_network_graph.cpp`; `#if APPTRAVERSE_ENABLE_SHARED_NODE` в `node.h:216,552,606`, `node_for.h:24,60` | компилируется в core; тесты — только под `APPTRAVERSE_BUILD_LEGACY_EXAMPLES` (`tests/CMakeLists.txt:703+`) |

### A.2 Persistence

| Механизм | Файлы / функции | Что хранит |
| --- | --- | --- |
| Domain layers | `src/directory_domain_storage.cpp` (`DirectoryDomainStorage`, `CompareOnCloseDirectoryStorageWriter::CommitIfNeeded`, `ReplaceLayerFile` tmp+rename) | по файлу на `(ObjId, ClassId, S)`; ошибки commit → `failed_` + `RecordLayerCommit(kFailed)`; видимы только через `BeginSaveSession` / `SaveSessionHadFailure` |
| Overlay | `src/overlay_domain_storage.cpp` | USER поверх BACKING; запись только если байты отличаются от BACKING; supply-constant слои не пишутся; ошибки записи → `std::abort` |
| Partitioned | `src/partitioned_model_domain_storage.cpp` | используется только тестами (`model_persistence_test`, `partitioned_model_domain_storage_test`) |
| Checkpoint | `SerializePersistentModelSnapshot` (`object_serialization.cpp:1407`), `LoadPersistentModelSnapshot` (`:1467`), `SavePersistentModelSnapshotToStorage` (`:1676`), `TryLoadPersistedModelFromStorageSidecar` (`:1688`, только `PartitionedModelDomainStorage&`) | `persistent_model_checkpoint.bin`: root id, все достижимые слои, generations Node, привязки Node→base |
| Lazy-ремонт ссылок | `SanitizeLoadedPersistentObjPtrs` (`src/model_persistence.cpp`) | очистка устаревших `ObjPtr` после загрузки |

### A.3 Application Runtime (model thread)

`ApplicationModelRuntime::Run` (`src/application_model_runtime.cpp:30`): `BindReachableNodesMaterializedChangeNotifier` → `SerializeInitialPublication` → цикл работ (`Post`) → `SerializeStructuralNodePublication` по `PendingDirtyNodes` → `ClearLiveReachableNodesMaterializedChangeNotifier` (`:107`). Приложения: `WindowCatalogModelSession::Run` (`examples/surfaces_demo/common/window_catalog_lifecycle.cpp`), `RunMinimalApplicationModel` (`examples/minimal_app/common/minimal_app_lifecycle.cpp`).

### A.4 UI Runtime

`ApplicationUiSession` (`src/application_ui_session.cpp`): собственный UI-mirror Domain, `ApplyStructural` → `ApplyStructuralPublicationAndUpdatePresenters`. Команды GUI → `ModelObjectProxy::Invoke` → `ApplicationModelRuntime::Post` (model thread). UI-mirror не пишет persistent model.

### A.5 Platform Host (Windows)

`WinApplicationHost` (`src/platform/windows/win_application_host.cpp`): loading window, message host, model thread; `OnInitialPublished` / `OnIncrementalPublished`; `Run()` (`:60`) завершается `JoinModelThread()` (`:75`) — shutdown-save модели гарантированно завершён до возврата из `host.Run()`. Callbacks приложения: `window_catalog_main.cpp` (`run_model`, `request_stop`, `on_desktop_window_close`, `take_incremental_sidecar`, `after_incremental`), `minimal_app_main.cpp`.

### A.6 Build-time distillation (временный bootstrap до 9D/9E)

Под `APPTRAVERSE_ENABLE_DISTILLATION` (задаётся в `examples/surfaces_demo/windows/CMakeLists.txt:10,74`, `examples/minimal_app/windows/CMakeLists.txt:28`): при отсутствии профиля — `Build*Graph` → `FinalizeDistilledGraph` → `SaveDistilledRoot` в `DirectoryDomainStorage`. Отдельно — export tools (`minimal_app_export.cpp:72`, `export_surfaces_distilled_graph.cpp:48`) с `StabilizeDistilledPersistentSnapshot`.

### A.7 Future Release R/E/B/S

В приложениях не интегрировано. Механизмы есть в Object Core и fixtures: `OverlayDomainStorage` (USER/BACKING), embedded/supply (`embedded_distilled_domain.cpp`, `distilled_supply_package.cpp`, `distilled_domain_export.cpp`), `ShouldSaveVersion`, origin facet, compaction guard. Перевод Windows apps на embedded BACKING + USER — **9D/9E**.

### A.8 Цепочка Storage → … → Persistence (WindowCatalog; MinimalApp аналогично)

1. **Storage:** `DirectoryDomainStorage durable_storage{state_dir}`; если профиля нет — distill bootstrap (A.6).
2. **Domain:** `ae::Domain domain{durable_storage}`; `LoadApplication<WindowCatalog>(domain, CatalogRootId())`.
3. **Checkpoint replay:** inline-чтение `persistent_model_checkpoint.bin` (`window_catalog_lifecycle.cpp:65-87`) → `LoadPersistentModelSnapshot`: inject слоёв в storage, hydrate, `AdoptPublishedGeneration`, `RestorePersistedNodeBaseBindings`, `SanitizeLoadedPersistentObjPtrs`, journal, `AdoptMaterializedPersistentCheckpointJournal`.
4. **Model (обязательные ссылки):** `ui::LoadPresentedWindow(main_window)`, `list/locale/presenters/buttons/texts.Load()` с fail-fast (I0.3A–I0.3B2c2); `MaterializeCatalogStrings`, `MaterializeHolderListText`, `RefreshChromeButtonEnabledState`, `RestoreLoadedCatalogWindows`.
5. **Events:** GUI → `ModelObjectProxy` → `WindowCatalog::{AddWindow, DeleteSelected, OpenHolderWindow, CloseHolderWindow, UserCloseWindow}` → `Node::Commit` (`AddWindowCatalogEntryEvent`, selection, open events) → `Apply`.
6. **Publication:** `ApplicationModelRuntime::Run` → structural publication по dirty Node.
7. **UI:** `ApplicationUiSession::ApplyStructural` → presenters → Win32.
8. **Shutdown:** `RequestStop` → выход из `runtime.Run`; `SaveWindowCatalogPersistentSnapshot` (`window_catalog_model.cpp:90-106`: `SavePresentedWindow`, `primary_text.Save()/Reset()`, `catalog.Save()`).
9. **Persistence:** `SavePersistentModelSnapshotToStorage(*catalog, durable_storage)` (`window_catalog_lifecycle.cpp:157`): serialize → sidecar (`WritePersistentModelCheckpointSidecar`, `ofstream trunc`) → inject тех же слоёв в storage. **Результат не проверяется.**

MinimalApp отличается шагом 8: `StabilizeDistilledPersistentSnapshot(*application)` → `SavePresentedWindow` → `Save()` (`minimal_app_lifecycle.cpp:89-92`) — см. **U4-02**.

### A.9 Ответы на вопросы persistence/checkpoint

1. **Зачем `persistent_model_checkpoint.bin`.** Node-слои пишутся в storage **без** `base`: `SerializePersistentModelSnapshot` вызывает `ClearReachableNodeBaseForCheckpointSave` перед `SaveObjectGraphToScratch` (введено в `9093467`). Привязка Node→base, generations и журнал с признаком материализации существуют только в envelope (`WritePersistentCheckpointNodeBaseBindings`, `WritePersistentCheckpointScratchLayers`). Checkpoint — единственный носитель этих runtime-инвариантов Node между запусками.
2. **Что где.** Domain layers: состояние объектов по `(ObjId, ClassId, S)`; Node-слои после checkpoint-save — без base. Checkpoint: копия всех достижимых слоёв (loaded + `AppendUnloadedCheckpointTargets`), generations, base bindings. Base-объекты: `EnsurePersistentCheckpointBaseObjectsInScratch` + `CaptureBaseStateInto` (пишет base прямо в persistent storage через scratch).
3. **Почему `LoadApplication` недостаточно перед `Commit`.** После `LoadApplication` у Node нет валидного `base` → `TryInsertEvent` возвращает false → `InsertEvent` assert (`node.h:519-523`); в NDEBUG Event молча теряется. Base восстанавливает только `RestorePersistedNodeBaseBindings` из checkpoint (подтверждено U1: assert `InsertEvent invariant broken` при reload без checkpoint).
4. **Дублирование данных.** Да: все достижимые слои лежат и в storage, и в sidecar; при load sidecar-слои повторно inject-ятся в storage (`object_serialization.cpp:1490`). Перед checkpoint WindowCatalog делает `catalog.Save()` — те же слои пишутся дважды (второй раз — без base). Это не лишняя запись: `Save()`/`Reset()` нужен для unloaded целей (`primary_text`, presented windows), которые checkpoint читает из storage (см. C.2).
5. **Дублирование кода загрузки.** Да, 4 копии inline-чтения sidecar: `window_catalog_lifecycle.cpp:65-87`, `minimal_app_lifecycle.cpp:61-83`, `tests/window_catalog_model_test.cpp` (`LoadPersistentCheckpointIntoCatalog`), `tests/minimal_app_win32_acceptance_test.cpp:397-415`. Библиотечный `TryLoadPersistedModelFromStorageSidecar` с проверками есть, но только для `PartitionedModelDomainStorage&` и используется только тестом (`model_persistence_test.cpp:412`) — **U4-05**.
6. **Ошибки.** Запись слоёв: `DirectoryDomainStorage` фиксирует сбои commit в save session; WindowCatalog/MinimalApp session не открывают и не проверяют. Запись sidecar: bool, игнорируется. Чтение sidecar в apps: `exists` error игнорируется, `ifstream` не проверяется (`tellg()==-1` → гигантская аллокация). `LoadPersistentModelSnapshot`: формат проверяется только `assert` (`:1471-1557`); отсутствующие объекты и сбой `LoadExistingObject` — fatal + abort.
7. **Несогласованность после неудачного save.** Возможна: `catalog.Save()` уже записал новые слои → запись sidecar не удалась → остаётся **старый** sidecar → при следующем старте `LoadPersistentModelSnapshot` inject-ит старые слои поверх новых и восстанавливает старые bindings/journal; последняя сессия молча откатывается (частично — объекты, которых нет в старом sidecar, остаются новыми). Частично записанный sidecar (`ofstream trunc`) → Debug assert / Release UB при следующем старте — **U4-01, U4-03, U4-04**.
8. **Временное vs конечное.** Временно до 9D/9E: distill bootstrap в lifecycle, `DirectoryDomainStorage` как единственное хранилище приложения. Конечная судьба checkpoint — решение 9D (I0 ledger, строка «Persistent checkpoint»: «embedded startup (9D) must define whether checkpoint remains»). Конечное: Node base/journal-контракт, fail-fast обязательных ссылок, lazy-materialization строк/окон, publication pipeline.

---

## B. Architectural Issues

| ID | Severity | File / Function | Problem | Evidence | Proposed Action |
| --- | --- | --- | --- | --- | --- |
| U4-01 | **Critical** | `window_catalog_lifecycle.cpp:156-157` `WindowCatalogModelSession::Run`; `minimal_app_lifecycle.cpp:89-92` `RunMinimalApplicationModel`; `tests/window_catalog_model_test.cpp:67` | Shutdown-save не проверяет ни результат `SavePersistentModelSnapshotToStorage`, ни сбои commit слоёв; при сбое состояние может молча откатиться к старому sidecar (A.9 п.7) | `SavePersistentModelSnapshotToStorageImpl` (`object_serialization.cpp:1657-1672`) возвращает false; commit-сбои `DirectoryDomainStorage` видны только через `SaveSessionHadFailure` (`directory_domain_storage.cpp:239-301, 417`), а `surfaces_lifecycle.cpp:255-263` этим уже пользуется | **FIX U5.1**: явная ошибка вызывающему коду (stderr + ненулевой exit code), без транзакций/recovery |
| U4-02 | **Critical** | `minimal_app_lifecycle.cpp:89` → `StabilizeDistilledPersistentSnapshot` (`distill.h:34-44`) | Runtime-shutdown вызывает build-time нормализацию: `journal.clear()` + `CaptureBaseState()` + generation=1 в обход guard компактации **11B.2** (`CompactJournalInto`: `CanCompactStoredState` `node.h:401`, `CanCompactStoredObjectVersions` `node.h:417-422`) | Frame-события desktop window журналируются в runtime (`ui_desktop_window.cpp:53,69`); WindowCatalog так не делает; функция предназначена для export tools (`minimal_app_export.cpp:72`, `export_surfaces_distilled_graph.cpp:48`); введено в `09dd220` (шаблон MinimalApp) | **FIX U5.2** (после подтверждения пользователя): убрать вызов из runtime-shutdown |
| U4-03 | Major | `LoadPersistentModelSnapshot` (`object_serialization.cpp:1467-1557`) | Валидация формата checkpoint только `assert`; в NDEBUG усечённый файл → чтение за границей буфера (`in.data + in.pos, size` на `:1490`); слои inject-ятся в storage до полной валидации | `assert(in.ok && in.pos + size <= in.size)` `:1489`; валидирующий вариант уже есть — `InjectPersistentModelSnapshotToStorage` (`:1585-1633`, bool) | **FIX U5.3**: заменить assert на fail-fast (`WriteFatalStderr` + `abort`, как `:1509-1514`); recovery — этап 22 |
| U4-04 | Major | `WritePersistentModelCheckpointSidecar` (`object_serialization.cpp:1645-1655`) | Sidecar пишется `ofstream trunc` напрямую; слои — через tmp+rename (`ReplaceLayerFile`, `directory_domain_storage.cpp:103-165`). Сбой посередине → усечённый sidecar | код | **Вопрос пользователю**: per-file tmp+rename для sidecar в U5 или этап 22 (не транзакция между файлами) |
| U4-05 | Major | 4 inline-копии чтения sidecar (A.9 п.5); `TryLoadPersistedModelFromStorageSidecar` (`:1688`) только для `PartitionedModelDomainStorage&` | Дублирование механизма загрузки; копии без проверок ошибок; библиотечная версия возвращает false и при «нет файла», и при «не читается» | grep | **FIX U5.4**: одна библиотечная функция чтения sidecar по каталогу (overload для `DirectoryDomainStorage&`, по образцу Save-overloads в `object_serialization.h`), замена 4 копий |
| U4-06 | Major | `build/win64-ninja-msvc-release/CMakeCache.txt`; `cmake/apptraverse_compile_policy.cmake`; `node.h:519-523` | Release-каталог собран без `/O2 /DNDEBUG` (`CMAKE_CXX_FLAGS_RELEASE` пуст): U1/U2 фактически проверены с активными assert; в настоящем Release сбой `InsertEvent` молча теряет Event, а assert-only проверки (U4-03) исчезают | `CMAKE_BUILD_TYPE=Release`, `CMAKE_CXX_FLAGS_RELEASE:STRING=` (пусто); причина пустых флагов не установлена (вероятно, обход toolchain в ранней сессии) | **U6**: проверка в каталоге с корректными Release-флагами (нужно разрешение пользователя; без clean существующего) |
| U4-07 | Major | `CMakeLists.txt:64-81` (CPM `PATCHES`), `cmake/patches/*.patch` | 9 патчей образуют упорядоченный стек: `domain.h` трогают 6, `domain.cpp` — 5; CPM применяет патчи только при populate. Fresh populate после правки заголовков (U1) не проверен; ранее патчи вручную накладывались в cache | `+++`-заголовки патчей; история U1 | **U6**: fresh configure в отдельном каталоге/`CPM_SOURCE_CACHE` (без удаления существующего); порядок в `CMakeLists.txt` не менять |
| U4-08 | Minor | `CMakeLists.txt:98-99, 141-146, 161-162` | `APPTRAVERSE_ENABLE_SHARED_NODE ON` безусловно и SharedNode-исходники в core, хотя I0 ledger (B, строка SharedNode: «`.cpp` removed I0.2; `=0`») и `AGENTS.md` («SharedNode is out of scope») говорят обратное; комментарий «part of the object core and its tests» неточен — тесты только в legacy (`tests/CMakeLists.txt:703+`) | merge `4848402` | **Вопрос пользователю** (варианты в D.2); PUBLIC для этого define корректен — он меняет vtable `Node` |
| U4-09 | Minor | `CMakeLists.txt:161-162` | `AE_DISTILLATION=1 AE_FILTRATION=1` PUBLIC на core `apptraverse`, но используются только aether-client демо | `aether_presence_monitor/main.cpp:245`, `chat_aether_runtime.cpp:86`, `chat_ui_runtime_demo/windows/aether_runtime.cpp:106`; в aether-objects не используются | DEFER (затрагивает только legacy/aether builds) |
| U4-10 | Minor | `CMakeLists.txt:174` | `NOMINMAX WIN32_LEAN_AND_MEAN` PUBLIC на platform-neutral `apptraverse` вместо `apptraverse_ui_windows` | код | DEFER (безвредно; перенос может вскрыть неявные `windows.h` include) |
| U4-11 | Minor | `src/model_persistence.cpp:106-143` `PersistentSnapshotPatchSession`; `include/apptraverse/model_persistence.h:49-66` `PersistentSnapshotPatchGuard` | Записи патчей в `SanitizeLoadedPersistentObjPtrs` не используются; `PersistentSnapshotPatchGuard` нигде не используется | grep | FIX U5.5 (опционально) или DEFER |
| U4-12 | Minor | `CMakeLists.txt:268, 276`; `examples/surfaces_demo/CMakeLists.txt:114` | `BUILD_TESTING` читается до `include(CTest)`; на первом configure не определён (маскируется `APPTRAVERSE_BUILD_EXAMPLES=ON` по умолчанию) | код | DEFER |
| U4-13 | Minor | `object_clone.cpp:14-34` `InjectLayerBytes`/`InjectClassData`; `object_serialization.cpp` `InjectObjectBytes`; `node.h:317` `CaptureBaseStateInto` | Несколько копий raw-inject слоёв (scratch save + `Store`/`Write`) | код | DEFER до **13.3** (Clone графов затронет этот путь) |
| U4-14 | Deferred | `window_catalog_lifecycle.cpp:39-59`, `minimal_app_lifecycle.cpp:33-53` | Дублирующиеся distill bootstrap блоки | `#ifdef APPTRAVERSE_ENABLE_DISTILLATION` | DEFER **9D/9E** (заменяется embedded startup) |
| U4-15 | Deferred | `AppendUnloadedCheckpointTargets` (`object_serialization.cpp:1377-1405`) | Checkpoint не самодостаточен: планируются только unloaded-рёбра root и их замыкание; unloaded-рёбра внутри loaded-подграфа (`holder->primary_text` после `Reset()`, `window_catalog_model.cpp:100`) берутся из storage; отсутствующие цели пропускаются молча (`:1399`). Сейчас корректно, т.к. storage тот же | код | DEFER **9D** (при BACKING+USER проверить тестом) |
| U4-16 | Deferred | `WindowCatalogModelSession::Run` | Журналы Node растут между сессиями: `CompactJournal` в apps не вызывается (только `main_window_runtime_demo/common/main_window_lifecycle.cpp:161`) | grep | DEFER (retention/compaction policy — **11/14**) |
| U4-17 | Deferred | `SerializePersistentModelSnapshot` / `LoadPersistentModelSnapshot` | Envelope без magic/версии формата (начинается с root id) | `:1469-1472` | DEFER (изменение формата запрещено в U4/U5; **9D**/этап 22) |

**Итого:** Critical — 2, Major — 5, Minor — 6, Deferred — 4.

---

## C. KEEP / FIX U5 / DEFER

### C.1 KEEP

- **Node base/journal-контракт** и `TryInsertEvent`-инвариант (`node.h`); guard компактации **11B.2**.
- **Fail-fast обязательных ссылок** в lifecycle (I0.3A–I0.3B3b) — сознательное решение, не дублирование.
- **Publication pipeline**: `ApplicationModelRuntime` → `ApplicationUiSession` → presenters; `ModelObjectProxy` как единственный путь GUI → model.
- **`DirectoryDomainStorage`** (per-layer tmp+rename, compare-on-close, save session stats) — политика ошибок не меняется до отдельного задания.
- **`OverlayDomainStorage`** (12B, versioning tests) — не дублируется, используется fixtures/embedded.
- **`CloneCanonicalRoot`** — только 13.2-совместимость.
- **Разделение CMake**: core / `apptraverse_ui_windows` / `*_extended` (EXCLUDE_FROM_ALL) / legacy и aether опции; порядок 9 патчей.
- **Различия MinimalApp vs WindowCatalog** в составе графа, обязательных ссылках и callbacks host — по делу; объединять классы не требуется.

### C.2 Выглядит избыточным, но необходимо

| Механизм | Почему нужен |
| --- | --- |
| `catalog.Save()` перед checkpoint (`window_catalog_model.cpp:105`) | пишет unloaded/Reset цели (`primary_text`, presented windows) в storage, откуда их читает `AppendUnloadedCheckpointTargets` |
| `primary_text.Save()` + `Reset()` (`:99-100`) | lazy loading строк: в памяти держится только активный язык |
| `MaterializeLanguage` с Save+Reset других слотов (`src/ui/ui_localization_string.cpp`), вызов до `SubmitLanguageIndexFromUi` (`window_catalog_graph.cpp`, `CatalogLanguageButton::HandlePushButtonClicked`) | lazy-materialization кэша строк, не изменение модели в обход Events; язык меняется Event-ом |
| `MakeHolderTitleString` / `PersistUiStringUtf8` runtime `Save()` | новые строки должны существовать в storage до Reset |
| `LoadPresentedWindow` / `UnloadPresentedWindow` в `OpenHolderWindow` / `CloseHolderWindow` | lazy loading окон; состояние открытия — Event |
| Повторный inject sidecar-слоёв при load | восстанавливает слои, перезаписанные без base, до hydrate |
| `SanitizeLoadedPersistentObjPtrs` (сама очистка) | lazy-ссылки на удалённые объекты |
| `AdoptMaterializedPersistentCheckpointJournal` (`1ad2a30`) | следующий `Commit` не должен повторно применять загруженный журнал |

### C.3 FIX U5

U4-01, U4-02 (после подтверждения), U4-03, U4-05; опционально U4-11; U4-04 — по решению пользователя.

### C.4 DEFER

U4-06, U4-07 → **U6**; U4-14, U4-15, U4-17 → **9D/9E**; U4-16 → **11/14**; U4-09, U4-10, U4-12 → CMake-гигиена вне U5; U4-13 → **13.3**; U4-04 (если не U5) → этап **22**; U4-08 → по решению пользователя.

---

## D. U5 Implementation Plan

Каждый шаг — отдельный commit, инкрементальная сборка, без изменения ClassId/ObjId/S/wire format.

### U5.1 — Явная ошибка shutdown-save (U4-01)

- **Файлы:** `examples/surfaces_demo/common/window_catalog_lifecycle.{h,cpp}`, `examples/surfaces_demo/windows/window_catalog_main.cpp`, `examples/minimal_app/common/minimal_app_lifecycle.{h,cpp}`, `examples/minimal_app/windows/minimal_app_main.cpp`, `tests/window_catalog_model_test.cpp` (`ShutdownSaveCatalog`).
- **Изменение:** `durable_storage.BeginSaveSession()` перед shutdown-save; успех = `SavePersistentModelSnapshotToStorage(...) && !durable_storage.SaveSessionHadFailure()`; при неуспехе — сообщение в stderr (`WriteFatalStderr`) и признак ошибки в session/результате `Run…`; `main` возвращает ненулевой код после `host.Run()` (model thread уже присоединён, `win_application_host.cpp:75`). Паттерн — существующий `surfaces_lifecycle.cpp:255-263`. Без abort, без отката, без удаления sidecar (удаление сделало бы следующий старт хуже — см. A.9 п.3). В тесте — `CHECK` результата.
- **Инвариант:** ошибка persistence явно доходит до вызывающего кода (политика пользователя).
- **Тесты:** `apptraverse_window_catalog_model_test`; `apptraverse_minimal_app_win32_acceptance_test` (ожидает `exit_code == 0`, строка 380).
- **Новый тест:** не обязателен; failure-injection (неписуемый каталог) — кандидат на этап 22.
- **Остаётся:** откат к старому sidecar после сбоя (A.9 п.7) документируется как известный риск этапа 22.

### U5.2 — Убрать build-time нормализацию из runtime-shutdown MinimalApp (U4-02)

- **Файлы:** `examples/minimal_app/common/minimal_app_lifecycle.cpp`.
- **Изменение:** удалить `StabilizeDistilledPersistentSnapshot(*application)` из shutdown; export tool не трогать.
- **Инвариант:** journal/base меняются только через `CompactJournalInto` с guard 11B.2; lifecycle MinimalApp совпадает с WindowCatalog.
- **Тесты:** `apptraverse_minimal_app_win32_acceptance_test` (frame restore между запусками), `apptraverse_window_catalog_model_test` (регрессия общего пути).
- **Новый тест:** нет; при желании — проверка, что журнал frame-события переживает перезапуск (решение пользователя).
- **Условие:** подтверждение, что «сворачивание» журнала при каждом выходе не было намеренным.

### U5.3 — Fail-fast вместо assert в `LoadPersistentModelSnapshot` (U4-03)

- **Файлы:** `src/object_serialization.cpp`.
- **Изменение:** каждую `assert(in.ok …)` формата заменить на проверку + `WriteFatalStderr("fatal: persistent checkpoint truncated/corrupt …")` + `std::abort()` (стиль `:1509-1514`). Формат не меняется.
- **Инвариант:** нет UB в Release на собственном повреждённом файле; поведение Debug не меняется для валидных файлов.
- **Тесты:** `apptraverse_model_persistence_test`, `apptraverse_window_catalog_model_test`, `apptraverse_minimal_app_win32_acceptance_test`.
- **Новый тест:** желателен death-test на усечённый checkpoint (через существующий helper запуска процесса в acceptance test) — по решению пользователя.

### U5.4 — Одна функция загрузки sidecar (U4-05)

- **Файлы:** `include/apptraverse/object_serialization.h`, `src/object_serialization.cpp`, оба lifecycle, `tests/window_catalog_model_test.cpp`, `tests/minimal_app_win32_acceptance_test.cpp`.
- **Изменение:** вынести чтение файла из `TryLoadPersistedModelFromStorageSidecar` в внутренний helper по каталогу; добавить overload для `DirectoryDomainStorage&` (каталог = `storage.root()`, как у Save-overload). «Файла нет» → false; «есть, но не читается» → fatal (как уже для `exists` error). Заменить 4 inline-копии.
- **Инвариант:** один путь загрузки checkpoint с проверкой ошибок; Save/Load симметричны по API.
- **Тесты:** `apptraverse_model_persistence_test` (строка 412), `apptraverse_window_catalog_model_test`, `apptraverse_minimal_app_win32_acceptance_test`, `apptraverse_partitioned_model_domain_storage_test`.
- **Новый тест:** нет.

### U5.5 — (опционально) удалить неиспользуемый patch bookkeeping (U4-11)

- **Файлы:** `src/model_persistence.cpp`, `include/apptraverse/model_persistence.h`.
- **Изменение:** убрать `PersistentSnapshotPatchGuard` и неиспользуемые записи `RecordObjPtrPatch`/`RestoreAll`, сохранив очистку stale `ObjPtr`.
- **Тесты:** `apptraverse_model_persistence_test`, catalog/minimal tests. **Новый тест:** нет.

### D.2 Варианты для U4-04 и U4-08 (не выбраны)

- **U4-04 sidecar:** (а) оставить до этапа 22 и задокументировать; (б) в U5 писать sidecar через tmp+rename тем же способом, что `ReplaceLayerFile` (без межфайловой транзакции).
- **U4-08 SharedNode:** (а) оставить ON, обновить I0 ledger/комментарий CMake как сознательное решение unified; (б) вернуть решение I0 — опция/OFF и исключение SharedNode-исходников из core (меняет vtable `Node`; требует полной регрессии U6).

### D.3 Решения пользователя (2026-10-26, после U4)

- **U4-02:** «сворачивание» журнала при выходе MinimalApp не намеренное → **U5.2** выполнять.
- **U4-04:** sidecar tmp+rename — **этап 22**; в U5 только документируется.
- **U4-08:** вернуть решение I0 — SharedNode опционально/OFF, исходники вне core → отдельный шаг **U5.6** (последним; меняет vtable `Node`, требует полной регрессии в **U6**).
- **U6:** отдельный build-каталог с Release-флагами / fresh populate — решение откладывается до U6.

---

## E. Risks for U6 / 9D / 9E

### E.1 Обязательно проверить

1. **Настоящий Release** (`/O2 /DNDEBUG`) — U4-06: assert-only инварианты исчезают; повторить catalog/minimal сценарии.
2. **Fresh configure** со всеми 9 патчами — U4-07.
3. **Checkpoint при BACKING + USER** — U4-15: `Save()`/`Reset()` и `AppendUnloadedCheckpointTargets` сейчас опираются на единый `DirectoryDomainStorage`; в overlay unloaded-цели могут жить в BACKING, а write-skip `BackingComparedStorageWriter` не запишет совпадающие байты в USER.
4. **Судьба checkpoint в 9D** (I0 ledger): base bindings сейчас только в sidecar; при отказе от sidecar нужен иной носитель base — это решение архитектуры 9D, не U5.
5. **Supply-constant ids** в overlay: `Store` для них отбрасывается (`SupplyConstantDiscardWriter`) — checkpoint inject таких слоёв в USER не произойдёт; проверить hydrate.
6. **Compaction guard в apps** после U5.2 и при появлении S+1 слоёв (R/E/B/S).
7. **Рост журналов** (U4-16) при многократных сессиях.
8. **Ошибки shutdown-save** — после U5.1 ненулевой exit code не должен ломать host/automation.

### E.2 Существующие тесты

| Тест | Что покрывает |
| --- | --- |
| `apptraverse_window_catalog_model_test` | lifecycle WindowCatalog, checkpoint reload, selection, holders, required refs |
| `apptraverse_minimal_app_win32_acceptance_test` | MinimalApp cold load, frame restore, required refs, exit code |
| `apptraverse_model_persistence_test` | checkpoint round-trip, journal, storage equivalence, Partitioned sidecar |
| `apptraverse_directory_domain_storage_test` | save session, failed commits |
| `apptraverse_partitioned_model_domain_storage_test` | Partitioned storage |
| `apptraverse_overlay_user_backing_test`, `apptraverse_overlay_domain_storage_test` | USER/BACKING, historical base в overlay |
| `apptraverse_object_clone_test`, `apptraverse_object_origin_facet_test` | 13.2 Clone, origin facet (12R/12B) |
| evolution / prefab release / event version / downgrade fixtures (`APPTRAVERSE_BUILD_EVOLUTION_TESTS`) | S-эволюция, compaction guard 11B.2, R0–R2 |
| `apptraverse_surfaces_distilled_embedded_matrix` | embedded distilled load (прототип 9D) |
