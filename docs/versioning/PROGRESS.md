# AppTraverse — прогресс версионирования поставок

## Source of truth

1. Google Doc: [AppTraverse — версионирование состояния и жизненный цикл поставок](https://docs.google.com/document/d/196STRvU1QMbMu0ArGRLahPpgnTb6HQoldUV-7tOIcfo/edit) — **прямая сверка не выполнена** (403 при fetch).
2. Архитектурный контракт и roadmap: [PLAN.md](./PLAN.md) — **AppTraverse Libraries Architecture RU v0.3 (25.10.2026)** + Object Core **`01_Object_Core_Versioning_PLAN.md` (25.10.2026)** ([share](https://chatgpt.com/library/share/libfile_c97b08ed7af88191a916944ac06566f7?account_id=personal)).

**Branch (рабочая):** **integration/unified-application** (merge **4848402**). Baseline: **integration/serialization-surfaces**, **integration/application-versioning**. См. [PLAN §Unified](./PLAN.md#unified-integration-историческая-координация--merge).

**Подтверждённый HEAD (unified, U1):** **`8835d26`**. **Object Core срез до merge:** **`df536e6`** (C0.2.2); docs C0.3 — **`9e91f4d`**. Application docs — [application-versioning.md](../integration/application-versioning.md), [window_catalog_demo.md](../window_catalog_demo.md).

**Baseline SHA (старт tracking fixtures):** `19d7da61954ddbab02ef098eec540ac17e936fcf`

**Порядок Object Core:** **C0 PASS** → **12R/12B PASS** → **13 IN_PROGRESS** (**13.1–13.2 PASS**, **13.3–13.6 TODO**) → **14–16 TODO**; **17–22** — roadmap ([PLAN §порядок](./PLAN.md#порядок-работ-после-11)).

**Активный порядок:** **P0 DONE** → **P1.1 DONE** → **P2–P6** ([PLAN §P0–P6](./PLAN.md#активный-план-мультиплатформенный-windowcatalog-p0p6)). Канон: [AppTraverse_Architecture_RU.md](../architecture/AppTraverse_Architecture_RU.md). **U5.2–U6 / 9D–9H** как очередь **сняты** (архив U1–U5.1 ниже).

**Текущий этап Object Core:** **13 IN_PROGRESS**; **NEXT: 13.3**. **11** **IN_PROGRESS** (частичный PASS). **01–10** **PASS** в заявленном scope. **14–22** — **TODO**. **T16/T17** — фрагменты. Object Core regressions на unified **не** PASS до **U6**.

**OPEN:** динамический источник Clone **без** устойчивого const **P** — явное решение, не silent fallback ([PLAN §13](./PLAN.md#13--универсальный-clone)).

**Нормативные правила Object Core:** [PLAN §нормативные правила](./PLAN.md#нормативные-правила-object-core-v03--01-plan) (здесь не дублируются).

**Fixture vs библиотека:** **`CloneCanonicalRoot`** — **13.2 PASS** (один объект); **P2.e.1 PASS** — remap штатного Save + двухобъектный graph (**13.3** частично); **P2.e.1R** — общий BACKING + cold **`Load(C)`**; **P2.e.2 PASS** — loaded Node R→D, **`CanonicalClone`**, собственные base/journal; **P2.e.2R PASS** — fresh history по remap-набору (Obj-корень + вложенный Node); **P2.e.3 PASS** — unloaded **`ObjPtr`** child через **`NotifyCloneReference`**, lazy сохранён; **P2.e.4 PASS** — **`UiDesktopWindow`** ↔ **`UiDesktopWindowPresenter`**, **`stable_prefab_ids`**, двойной Clone W→A→B; **P2.e.5 PASS** — **`WindowCatalog::AddWindow`** через **`CloneCanonicalRoot`**, **`WindowCatalog/S1`**; MinSizeRel manual (prefab Add/Open/Close/geom/locale/restart) **user confirmed**; дефект Z-order → **P2.e.5R PASS** — **`WindowCatalog/S2`** (`desktop_z_order`), **`SetWindowCatalogDesktopZOrderEvent`**, shutdown HWND→ObjId capture, **`WinApplicationHostCallbacks::after_initial`**. Полный lazy closure / **13** — **TODO**; holder/title builder и общий embedded lifecycle — **TODO**.

Общие правила разработки: [.cursor/rules/apptraverse-coding-agent.mdc](../../.cursor/rules/apptraverse-coding-agent.mdc). Корневой журнал других подсистем: [Progress.md](../../Progress.md).

---

## Unified integration (`integration/unified-application`)

| Milestone | SHA / evidence | Status |
| --- | --- | --- |
| Merge serialization-surfaces + application-versioning | **`4848402`** | **DONE** |
| **U1** — `apptraverse_window_catalog_model_test` Release + CTest | **`8835d26`** | **PASS** |
| **U2** — `win32_window_catalog_demo` ручной сценарий | incremental build, без кода | **manual PASS** (не full regression) |
| **U3** — PLAN/PROGRESS sync | doc-only | **PASS** (commit U3) |
| **U4** — аудит объединённой архитектуры | [unified-architecture-audit.md](../integration/unified-architecture-audit.md) | **PASS** (audit-only) |
| **U5.1** — shutdown-save → exit code | **`8c863bc`**, CTest 65–67 | **PASS** (история) |
| **P0** — канон + sync docs/rules | doc-only | **DONE** |
| **P1.1** — удаление сети/replication | см. §P1.1 ниже | **PASS** (Debug build + model test; см. evidence) |
| **P2.a** — единая загрузка checkpoint | см. §P2.a ниже | **PASS** |
| **P2.b** — MinimalApp без runtime Stabilize | см. §P2.b ниже | **PASS** |
| **P2.d.1** — checkpoint `BinaryArchive` | **`6676f7d`**, §P2.d.1 | **PASS (исторически; superseded)** |
| **P2.d.1R.1** — MinimalApp без sidecar | **`9f1d0a7`**, §P2.d.1R.1 | **PASS** |
| **P2.d.1R.2a** — WindowCatalog без sidecar | **`93a18dc`**, §P2.d.1R.2a | **PASS** |
| **P2.X1** — обязательный граф UI, assert side effects | **`51ecd2b`**, §P2.X1 | **PASS** |
| **P2.d.1R.2b.1** — persistence tests без checkpoint | **`63d3b18`**, §P2.d.1R.2b.1 | **PASS** |
| **P2.d.1R.2b.2** — cycle/rehydration DomainGraph | **`bc4d03f`**, §P2.d.1R.2b.2 | **PASS** |
| **P2.d.1R.2b.3a** — PresenterGraph DomainGraph | **`8ca6a23`**, §P2.d.1R.2b.3a | **PASS** |
| **P2.d.1R.2b.3b.1** — Win HWND DomainGraph | **`3d85a31`**, §P2.d.1R.2b.3b.1 | **PASS** |
| **P2.d.1R.2b.3b.2** — cross-process presenter storage | **`e750166`**, §P2.d.1R.2b.3b.2 | **PASS** |
| **P2.d.1R.2b.4** — runtime_observer DomainGraph | **`90908ce`**, §P2.d.1R.2b.4 | **PASS** |
| **P2.d.1R.2b.5** — удаление checkpoint API | **`15f1879`**, §P2.d.1R.2b.5 | **PASS** |
| **P2.d.1R** — без отдельного persistent checkpoint | §P2.d.1R.2b.5 | **PASS** |
| **P2.d.1R.2b** — миграция + удаление API | §P2.d.1R.2b.5 | **PASS** |
| **P2.d.2.1** — structural envelope BinaryArchive | **`d8e0094`**, §P2.d.2.1 | **PASS** |
| **P2.d.2.2** — graph payload BinaryArchive | **`bae3106`**, §P2.d.2.2 | **PASS** |
| **P2.d.2.2R** — NDEBUG-safe graph decode/apply | **`a6fa412`**, §P2.d.2.2R | **PASS** |
| **P2.d.2.3a** — initial envelope BinaryArchive | **`5d420c9`**, §P2.d.2.3a | **PASS** |
| **P2.d.2.3b** — incremental envelope BinaryArchive | **`4e8ba30`**, §P2.d.2.3b | **PASS** |
| **P2.d.2.3c** — single-object ClassData BinaryArchive | **`9afad2f`**, §P2.d.2.3c | **PASS** |
| **P2.d.2.4** — Close/Open lifecycle test | **`1669eb8`**, §P2.d.2.4 | **PASS** |
| **P2.d.2.5** — UiMirror BinaryArchive batch | **`bc247fe`**, §P2.d.2.5 | **PASS** |
| **P2.d.2.6** — final UI publication audit | **`c3b709e`**, §P2.d.2.6 | **PASS** |
| **P2.d.2** — UI publication wire | §P2.d.2.6 | **PASS** |
| **P2.d** — штатная сериализация | §P2.d.2.6 | **PASS** |
| **P2.e.1R** — graph clone test: shared BACKING, cold + `Load(C)` | **`c23ec0a`**, §P2.e.1R | **PASS** (Debug: `apptraverse_object_clone_test`) |
| **P2.e.1** — loaded graph Clone + const boundary; Save remap | **`a1a21f1`**, §P2.e.1 | **PASS** (remap; fixture до **P2.e.1R** не проверял lazy **C**) |
| **P2.e.2** — Node graph Clone + fresh history (`CanonicalClone`) | **`192a4b7`**, §P2.e.2 | **PASS** (Debug: `apptraverse_object_clone_test`, `apptraverse_model_persistence_test`, `apptraverse_application_contract_headless_test`) |
| **P2.e.2R** — fresh history по remap-копиям (Obj→Node) | **`9bfbc9e`**, §P2.e.2R | **PASS** (Debug: `apptraverse_object_clone_test`, `apptraverse_model_persistence_test`) |
| **P2.e.3** — Clone unloaded dynamic Node child | **`cb4873c`**, §P2.e.3 | **PASS** (Debug: `apptraverse_object_clone_test`, `apptraverse_model_persistence_test`, `apptraverse_application_contract_headless_test`) |
| **P2.e.4** — Presented window prefab clone + repeat origin | **`016c3a3`**, §P2.e.4 | **PASS** (Debug: `apptraverse_object_clone_test`, `apptraverse_application_contract_headless_test`) |
| **P2.e.5** — WindowCatalog Add via prefab + S1 link | **`a5e4d6f`**, §P2.e.5 | **PASS** (model); MinSizeRel manual (без Z-order) **user confirmed** |
| **P2.e.5R** — desktop Z-order S2 + Win32 restore | *(commit below)*, §P2.e.5R | **PASS** (Debug/MinSizeRel: `apptraverse_window_catalog_model_test`); manual Z-order **LAUNCHED** |
| **P2.e** — graph Clone prefab | §P2.e | **IN_PROGRESS** (полный lazy closure **TODO**; holder/title builder; embedded lifecycle) |
| **P2 (остальное)** | — | **IN_PROGRESS** |

**Application scope:** Windows WindowCatalog **manual PASS** (U2, **`8835d26`** era); **новые платформы / P3+ PASS не заявляются**. Embedded **9D/9E** — перенесено в **P2** (template), не отдельная очередь.

**Открыто:** атомарность профиля и corrupt-checkpoint hardening **не** в scope прототипа; **U5.1** exit code — факт **`8c863bc`**, failure branch в CTest не инжектировался.

---

## P1.1 — удаление сетевой синхронизации (2026-10-27, **`51ef0ce`**)

**Удалено:** SharedNode, SharedSyncRuntime, SharedNetworkGraph (transport/replication), `Link` (replication), SyncFrame/ACK/presence path, ByteTransport/MemoryTransport, `runtime_lifecycle` (NetworkState/Aether registration), `aether-client-cpp` из CMake, chat/shared/aether demos и связанные CTest targets.

**Локальный журнал:** `EventRecord` = `event` + `EventOrder` (`timestamp_us`) + `retained_since_us`; поле `SharedEventId identity` убрано. Отражённая сериализация journal меняет on-disk layout Node — **старые профили не совместимы**; dev: новый `--state-dir` (например `%TEMP%\apptraverse_p11_catalog_dev`).

**Сохранено:** `Node::Commit`, mid-journal insert/replay, retention/compaction block, closed Event graph helpers → `closed_event_graph.{h,cpp}` (без SharedNode export).

**Debug evidence (`build/win64-ninja-msvc-debug`):** `apptraverse_window_catalog_model_test` **PASS**; `win32_window_catalog_demo` запуск с новым профилем **OK** (краткий smoke, без полного GUI-сценария).

---

## P2.a — единая загрузка persistent checkpoint (2026-10-27, **`6c4a77c`**)

**API:** `TryLoadPersistedModelFromStorageSidecar(ae::Domain&, DirectoryDomainStorage&, ae::Obj&)`; общее чтение `persistent_model_checkpoint.bin` через внутренние `TryReadPersistentModelCheckpointSidecarBytes` / `TryLoadPersistedModelFromSidecarRoot` (sidecar root = `storage.root()`).

**Удалены дубли:** ручное чтение sidecar в `window_catalog_lifecycle.cpp`, `minimal_app_lifecycle.cpp`, `LoadPersistentCheckpointIntoCatalog` в `window_catalog_model_test.cpp`, `ProbePersistedGraph` в `minimal_app_win32_acceptance_test.cpp`.

**Debug:** сборка catalog/minimal targets **OK**; `apptraverse_window_catalog_model_test` **PASS**; MinimalApp `%TEMP%\apptraverse_p2a_minimal_dev3` bootstrap → WM_CLOSE → load-only **exit 0/0**.

---

## P2.b — MinimalApp shutdown без distillation normalize (2026-10-27, **`efe653e`**)

**Изменение:** из `RunMinimalApplicationModel` убран вызов `StabilizeDistilledPersistentSnapshot` перед shutdown-save; `StabilizeDistilledPersistentSnapshot` в `distill.h` без изменений (build-time export).

**Debug evidence (`build/win64-ninja-msvc-debug`):** инкрементальная сборка `win32_minimal_app`, `win32_minimal_app_load_only` **OK**; профиль `%TEMP%\apptraverse_p2b_minimal_dev`: первый запуск → WM_CLOSE → exit **0**, checkpoint создан → `win32_minimal_app_load_only` с тем же `--state-dir` → exit **0** (без проверки пользовательских Events / Commit после reload).

---

## P2.d.1 — checkpoint через Æther BinaryArchive (2026-10-27, **`6676f7d`**)

**Формат `persistent_model_checkpoint.bin`:** один blob `ae::seri::BinaryArchive` для `PersistentModelCheckpointPayload` (`root_id`, `layers[]` с `object_id/class_id/version/bytes`, `node_generations[]`, `node_base_bindings[]`; `AE_REFLECT_MEMBERS`). Старый ручной envelope (root + layer_count + …) **не** читается — новый dev `--state-dir`.

**Удалено:** `WritePersistentCheckpointScratchLayers`, `WritePersistentCheckpointNodeBaseBindings`, `RestorePersistedNodeBaseBindings(ByteSource&)`, `SkipPersistentCheckpointNodeBaseBindings`. Подготовка scratch/export filter и hydrate/load_keepalive без изменений по смыслу.

**Debug evidence (`build/win64-ninja-msvc-debug`):** targets `apptraverse_model_persistence_test`, `apptraverse_window_catalog_model_test`, `win32_minimal_app`, `win32_minimal_app_load_only`, `win32_window_catalog_demo` — сборка **OK**; оба model-теста **exit 0**; MinimalApp `docs/smoke_artifacts/p2d1_minimal_checkpoint_verify`: первый запуск → WM_CLOSE → **exit 0**, sidecar есть → `win32_minimal_app_load_only` → **exit 0**.

**Статус:** typed checkpoint **`6676f7d`** выполнен, но **не** конечная архитектура; заменяется штатным объектным storage (P2.d.1R).

---

## P2.d.1R.1 — MinimalApp: штатный Save без sidecar (2026-10-27, **`9f1d0a7`**)

**MinimalApp:** убраны `TryLoadPersistedModelFromStorageSidecar` и `SavePersistentModelSnapshotToStorage`; cold load через `LoadApplication`, shutdown — `BeginSaveSession` + `SavePresentedWindow` + `MinimalApplication::Save`.

**Headless:** `RunStandardDirectoryStorageNodeJournalRoundTripTest` — два Commit, `DomainGraph::SaveRoot` в `DirectoryDomainStorage`, cold `LoadRoot`, проверка label/base/journal, третий Commit и повторная загрузка (без checkpoint API).

**Sidecar-потребители (на момент P2.d.1R.1):** Host/Admin session, shell tests, library checkpoint API (WindowCatalog переведён в P2.d.1R.2a).

**Debug evidence (`build/win64-ninja-msvc-debug`):** `apptraverse_model_persistence_test` **exit 0** (`RunStandardDirectoryStorageNodeJournalRoundTripTest`); MinimalApp `%TEMP%` profile: WM_CLOSE **0**, sidecar **нет** → `load_only` **0**.

---

## P2.d.1R.2a — WindowCatalog: штатный Save без sidecar (2026-10-27, **`93a18dc`**)

**Lifecycle:** убраны `TryLoadPersistedModelFromStorageSidecar` и `SavePersistentModelSnapshotToStorage`; cold load — `LoadApplication<WindowCatalog>`; shutdown — `SaveWindowCatalogPersistentSnapshot` + `SaveSessionHadFailure()`.

**Tests:** `ShutdownSaveCatalog` / `LoadPersistedWindowCatalog` без sidecar; `TestWindowCatalogOpenCloseEventJournalPersistence` (бывш. …Checkpoint) — Add/Open/Close, cold reload, Open после load, replay/base/journal; `ShutdownSaveCatalog` проверяет отсутствие `persistent_model_checkpoint.bin`.

**Sidecar-потребители (остались):** Host/Admin session, shell tests, `model_persistence_test` checkpoint paths, distill tools.

**Debug evidence:** `apptraverse_window_catalog_model_test` **exit 0**; `%TEMP%\apptraverse_p2d1r2a_catalog`: demo → WM_CLOSE **0**, sidecar **нет** → `load_only` **0**.

**NEXT (не начато):** **P2.d.1R.2b** — оставшиеся потребители и удаление общего checkpoint API.

---

## P2.X1 — обязательный граф UI и assert без побочных эффектов (2026-10-27, **`51ecd2b`**)

**Runtime:** `WinPushButtonPresenter` / `CreateChildPushButton` — без повторной проверки обязательных связей, пустой подписи при отсутствии label/locale, fallback layout и ранних return в `SyncFromMirror`/`ApplyLayout` (кроме «геометрия не изменилась»).

**Presenters:** удалены `PresentationCreateSucceeded` и ветка «OnLoad без native» в `InitializeNewPresenters`; после нормального `OnLoad` — `presentation_loaded` и load order.

**Checkpoint (ещё для Host/Admin):** `SerializePersistentModelSnapshot` / `LoadPersistentModelSnapshot` — Save/Load/Inject вынесены из `assert`.

**Debug evidence (`build/win64-ninja-msvc-debug`):** `apptraverse_window_catalog_model_test`, `apptraverse_model_persistence_test` **exit 0**; `win32_window_catalog_demo` старт с dev-профилем **OK**.

**NEXT:** **P2.d.1R.2b** (без изменений очереди).

---

## P2.d.1R.2b.1 — persistence tests без checkpoint API (2026-10-27, **`63d3b18`**)

**`model_persistence_test`:** `RunModelPersistenceTest`, `RunSaveToStorageDoesNotMutateLiveGraphTest`, `RunSaveToStorageColdDirectoryRoundTripTest` — `DirectoryDomainStorage` + `DomainGraph::SaveRoot` / `LoadRoot`; удалены `RunPersistentJournalCheckpointRoundTripTest` (дубль `RunStandardDirectoryStorageNodeJournalRoundTripTest`) и `RunCheckpointSidecarStorageEquivalenceTest`.

**`minimal_app_win32_acceptance_test`:** `ProbePersistedGraph` — только `LoadApplication`; удалены `RemoveCheckpointSidecar` и sidecar hydrate.

**Debug:** `apptraverse_model_persistence_test` **exit 0**; acceptance target собран (GUI/negative suite не прогонялся по scope).

**NEXT:** оставшиеся presenter persistence tests → удаление библиотечного checkpoint API.

---

## P2.d.1R.2b.2 — cycle/rehydration через SaveRoot/LoadRoot (2026-10-27, **`bc4d03f`**)

**Tests:** `RunCycleGraphStorageRoundTripTest`, `RunCycleGraphLocaleRehydrationTest` — один `ae::RamDomainStorage`, save scope `DomainGraph::SaveRoot`, destroy domain, cold `LoadRoot(kAppId)` без pre-created shells; циклы App↔Window↔Presenter, locale aliases, journal/base, language Commit после load.

**Debug:** `apptraverse_checkpoint_load_cycle_persistence_test`, `apptraverse_checkpoint_load_rehydration_test` **exit 0**.

**NEXT:** presenter persistence tests (2b продолжается).

---

## P2.d.1R.2b.3a — PresenterGraph через SaveRoot/LoadRoot (2026-10-27, **`8ca6a23`**)

**Test:** `RunPresenterGraphPersistenceTest` — один `RamDomainStorage`, `SaveRoot(kRootId)` / cold `LoadRoot` без checkpoint blob или pre-created root shell.

**Проверки после load:** aliases window/presenters/edit/locale/bitmap/list, localization, bitmap bytes, `presentation_loaded == false`, layer ClassId/S v0 на storage.

**Debug:** `apptraverse_presenter_graph_persistence_test` **exit 0**.

**NEXT:** **P2.d.1R.2b.3b.2** — cross-process presenter persistence (см. § ниже).

---

## P2.d.1R.2b.3b.2 — cross-process presenter через DirectoryDomainStorage (2026-10-27, **`e750166`**)

**Test:** `apptraverse_presenter_platform_persist_roundtrip` — Save EXE / Load EXE делят `DirectoryDomainStorage` (`presenter_platform_persist_state`); `SaveRoot(kRootId)` / cold `LoadRoot` без checkpoint blob, sidecar `.bin` или pre-created root shell.

**Проверки после load:** root/window/presenter ObjId, window↔presenter, `LoadBackendDesktopWindowPresenter` (тот же serialized ClassId, другой C++ backend), `backend_tag_on_load == 2` (runtime-only; save tag 9 не persisted).

**Debug:** `apptraverse_presenter_platform_persist_roundtrip` **exit 0**; save/load EXE **exit 0**.

**NEXT:** **P2.d.1R.2b.4** — `model_persistence_runtime_ref_test` (см. § ниже).

---

## P2.d.1R.2b.4 — runtime_observer ref через SaveRoot/LoadRoot (2026-10-27, **`90908ce`**)

**Test:** `RunModelPersistenceRuntimeRefTest` — один `RamDomainStorage`, save scope → `SaveRoot(kDocId)` → destroy Domain → cold `LoadRoot` без pre-created doc; отражённый `runtime_observer` ObjPtr и historical `base` root Node сохраняются.

**Debug:** `apptraverse_model_persistence_runtime_ref_test` **exit 0**.

**NEXT:** **P2.d.1R.2b.5** — удаление checkpoint API (см. § ниже).

---

## P2.d.1R.2b.5 — удаление persistent checkpoint API (2026-10-27, **`15f1879`**)

**Удалено из библиотеки:** `SerializePersistentModelSnapshot` / `LoadPersistentModelSnapshot`, sidecar `persistent_model_checkpoint.bin`, inject/hydrate helpers, `model_persistence.h` / `model_persistence.cpp`, `Node::AdoptMaterializedPersistentCheckpointJournal`, `PartitionedModelDomainStorage::checkpoint_sidecar_directory`.

**Сохранено:** штатная object serialization, UI publication, `StructuralPublicationSidecar`, `ClearReachableNodeHistoryForPublicationSave` / `RestoreReachableNodeHistory`, `PartitionedModelDomainStorage` как forwarder.

**Debug regression:** `apptraverse_model_persistence_test`, `apptraverse_model_persistence_runtime_ref_test`, `apptraverse_checkpoint_load_cycle_persistence_test`, `apptraverse_checkpoint_load_rehydration_test`, `apptraverse_presenter_graph_persistence_test`, `apptraverse_presenter_win_hwnd_persistence_test`, `apptraverse_presenter_platform_persist_roundtrip`, `apptraverse_partitioned_model_domain_storage_test`, `apptraverse_window_catalog_model_test`; link **PASS** `win32_window_catalog_demo`, `win32_minimal_app`.

**NEXT:** **P2.d.2** — штатная сериализация UI publication (см. §P2.d.2.1).

---

## P2.d.2.1 — structural envelope через BinaryArchive (2026-10-27, **`d8e0094`**)

**Изменение:** `SerializeStructuralNodePublication` / `ApplyStructuralPublication` — `StructuralPublicationEnvelope` (`object_id`, `generation`, `graph_bytes`, `sidecar`) через `ae::seri::BinaryArchive`; внутренний payload по-прежнему `SerializeObjectGraphToBuffer` / `DeserializeObjectGraphFromBuffer`. Sidecar frame request без изменений.

**Debug PASS:** `apptraverse_ui_bitmap_structural_publication_test`, `apptraverse_application_contract_headless_test`, `apptraverse_application_model_runtime_test`, `apptraverse_presenter_win_desktop_frame_publication_e2e_test`.

**Примечание:** `apptraverse_application_ui_session_test` — `TestPresenterLifecycleCloseOpen` (341) **FAIL** на **`f286dc8`** до envelope; не регрессия P2.d.2.1.

**NEXT:** **P2.d.2.2** — BinaryArchive для object layers + Node generations в graph payload.

---

## P2.d.2.2 — graph payload через BinaryArchive (2026-10-27, **`bae3106`**)

**Изменение:** `SerializeObjectGraphToBuffer` / `DeserializeObjectGraphFromBuffer` / `TryLoadInitialPublication` — `GraphPublicationPayload` (`ae::RamDomainStorage::ObjClassData layers`, `generations`) через `BinaryArchive`; общий decode/inject helper; удалены ручные `layer_count`, per-layer headers и отдельная generation table в wire format. Внешний initial envelope (`root ObjId`) без изменений.

**Debug PASS:** `apptraverse_ui_bitmap_structural_publication_test`, `apptraverse_application_contract_headless_test`, `apptraverse_application_model_runtime_test`, `apptraverse_window_catalog_model_test`, `apptraverse_presenter_win_desktop_frame_publication_e2e_test`.

**Примечание:** `apptraverse_application_ui_session_test` — `TestPresenterLifecycleCloseOpen` (341) **FAIL** (pre-existing на **`f286dc8`**); не регрессия P2.d.2.2.

**NEXT:** **P2.d.2.3** — оставшиеся initial/incremental envelopes и single-object serialization.

---

## P2.d.2.2R — NDEBUG-safe graph publication decode (2026-10-27, **`a6fa412`**)

**Изменение:** `DeserializeObjectGraphFromBuffer` — `LoadGraphPublicationPayload` и `ApplyGraphPublicationPayloadToUiDomain` выполняются вне `assert`; при ошибке `WriteFatalStderr` + `std::abort()` (как у structural envelope).

**Debug PASS:** те же пять regression, что P2.d.2.2 (bitmap structural, contract headless, model runtime, window catalog model, frame E2E).

**NEXT:** **P2.d.2.3**.

---

## P2.d.2.3a — initial envelope через BinaryArchive (2026-10-27, **`5d420c9`**)

**Изменение:** `SerializeInitialPublication` / `TryLoadInitialPublication` — `InitialPublicationEnvelope` (`root_id`, `graph_bytes`) через `BinaryArchive`; внутренний `GraphPublicationPayload` без изменений. Удалена ручная запись/чтение root ObjId. Диагностика: `ReadEnvelope`.

**Debug PASS:** `apptraverse_ui_bitmap_publication_test`, `apptraverse_ui_bitmap_structural_publication_test`, `apptraverse_application_contract_headless_test`, `apptraverse_application_model_runtime_test`, `apptraverse_window_catalog_model_test`.

**NEXT:** **P2.d.2.3b** — incremental publication envelope.

---

## P2.d.2.3b — incremental envelope через BinaryArchive (2026-10-27, **`4e8ba30`**)

**Изменение:** `SerializeIncrementalNodePublication` / `ApplyIncrementalPublication` — `IncrementalPublicationEnvelope` (`object_id`, `generation`, `object_bytes`) через `BinaryArchive`; append в конец `out.bytes` (префикс `processed_sequence` сохранён); чтение с `in.pos`, advance по `archive.buffer().read_position()`. Single-object слои внутри envelope без изменений.

**Debug PASS:** `apptraverse_application_model_runtime_test` (прямой incremental-сценарий `TestDirectIncrementalPublicationApply`), `apptraverse_application_contract_headless_test`, `apptraverse_window_catalog_model_test`.

**NEXT:** **P2.d.2.3c** — штатная single-object serialization.

---

## P2.d.2.3c — single-object ClassData через BinaryArchive (2026-10-27, **`9afad2f`**)

**Изменение:** `SerializeObjectToBuffer` / `DeserializeObjectFromBuffer` — один `ae::RamDomainStorage::ClassData` через `BinaryArchive` (append в `out.bytes`; decode с `in.pos`, advance по `read_position`); удалены `AppendSavedObjectLayers` / `InjectSavedObjectLayers` и ручные class_id/version/size/layer bytes.

**Debug PASS:** `apptraverse_application_model_runtime_test` (`TestDirectIncrementalPublicationApply`), `apptraverse_application_contract_headless_test`, `apptraverse_window_catalog_model_test`.

---

## P2.d.2.4 — Close/Open presenter lifecycle (2026-10-28, **`1669eb8`**)

**История:** `apptraverse_application_ui_session_test` / `TestPresenterLifecycleCloseOpen` — **FAIL** на **`f286dc8`** и до P2.d.2.4 на **`f5757f9`**: чтение `presenter.on_unload_count` после structural Close при уже освобождённом keepalive (`ApplyStructuralPublicationAndUpdatePresenters`).

**Причина (подтверждено):** ошибка наблюдения в тесте (use-after-free ссылки на presenter), не ошибка live-топологии в `object_serialization.cpp`. Production-код не менялся.

**Исправление:** `TestPresenterLifecycleCloseOpen` — `g_session_presenter_load_total` / `g_session_presenter_unload_total`, проверки model/UI `presented_open` и lazy `presented_window` без разыменования unloaded окна; nested scope для финальной выгрузки session (2× OnUnload суммарно).

**Счётчики по этапам:** после initial OnLoad=1/OnUnload=0; после Close OnLoad=1/OnUnload=1; после Open OnLoad=2/OnUnload=1; после уничтожения session OnLoad=2/OnUnload=2.

**Debug PASS:** `apptraverse_application_ui_session_test`, `apptraverse_application_contract_headless_test`, `apptraverse_window_catalog_model_test`.

---

## P2.d.2.5 — UiMirror publication через BinaryArchive (2026-10-28, **`bc247fe`**)

**Изменение:** `UiMirror::Publish` / `ApplyPublished` — `std::vector<UiMirrorPublicationRecord>` (`object_id`, `class_id`, `generation`, `graph_bytes` из `SerializeObjectGraphToBuffer`) через `BinaryArchive`. Удалены `WriteU32`/`ReadU32`/`ReadU64`, `PreferredClassIdFor`, ручные `record_count`/`payload_size`/memcpy backfill; class id берётся из записи публикации.

**Debug PASS:** `apptraverse_application_model_runtime_test` (`TestUiMirrorBatchPublication`), `apptraverse_application_ui_session_test`, `apptraverse_application_contract_headless_test`.

**NEXT (отдельно):** финальный аудит P2.d.2 на оставшиеся ручные форматы в действующих путях.

---

## P2.d.2.6 — final UI publication audit (2026-10-28, **`c3b709e`**, audit base **`8829659`**)

**Scope (read-only):** `object_serialization.*`, `ui_mirror.*`, `graph_mirror.cpp`, `application_model_runtime.cpp`, `application_ui_session.cpp`, `publication_channel.h`. Production-код **не менялся**.

### Матрица путей (wire + тест)

| Путь | Wire-формат | Проверяющий тест (Debug) | Статус |
| --- | --- | --- | --- |
| A Initial | `InitialPublicationEnvelope` → `GraphPublicationPayload` | `apptraverse_ui_bitmap_publication_test`, `application_ui_session_test` (`TestInitialMirror`) | **PASS** |
| B Structural | `StructuralPublicationEnvelope` (+ sidecar byte) → `GraphPublicationPayload` | `apptraverse_ui_bitmap_structural_publication_test`, `application_contract_headless_test`, `application_ui_session_test`, `presenter_win_desktop_frame_publication_e2e_test` | **PASS** |
| C Incremental | `IncrementalPublicationEnvelope` → `ClassData` in `object_bytes` | `application_model_runtime_test` (`TestDirectIncrementalPublicationApply`) | **PASS** |
| D UiMirror batch | `vector<UiMirrorPublicationRecord>` → per-record `GraphPublicationPayload` | `application_model_runtime_test` (`TestUiMirrorBatchPublication`, один existing root) | **PASS** (узкий) |
| E Graph copy | `ClassData` via `SerializeObjectToBuffer` / `DeserializeObjectFromBuffer` | `application_contract_headless_test` (indirect via harness), persistence/graph tests | **PASS** |

### Debug acceptance (`build/win64-ninja-msvc-debug`, exit 0)

`apptraverse_ui_bitmap_publication_test`, `apptraverse_ui_bitmap_structural_publication_test`, `apptraverse_application_model_runtime_test`, `apptraverse_application_ui_session_test`, `apptraverse_application_contract_headless_test`, `apptraverse_window_catalog_model_test`, `apptraverse_model_persistence_test`, `apptraverse_presenter_graph_persistence_test`, `apptraverse_presenter_win_desktop_frame_publication_e2e_test`.

### Критерии (подтверждено по коду)

- DomainGraph scratch Save для слоёв; graph payload — `BinaryArchive` + `RamDomainStorage::ObjClassData`; single-object — `ClassData`; envelopes — `AE_REFLECT_MEMBERS`.
- В перечисленных файлах **нет** ручного UI wire parse ClassId/S/size (единственный `memcpy` — `ByteSource::read` infra в `publication_channel.h`).
- Append/in.pos: incremental envelope, single-object, UiMirror channel buffer; initial/structural consume whole buffer by design.
- Save/Load/Inject publication paths вне `assert` (P2.d.2.2R); structural keepalive без изменений; Close/Open lifecycle — test observation fix (P2.d.2.4).
- Persistent checkpoint API в библиотеке **отсутствует** (P2.d.1R.2b.5).

### Ручные форматы вне P2.d.2 UI wire

- Embedded/distilled export, `DistilledSupplyPackage`, demo wire, legacy shell test helpers (`DumpPublicationLayersForObj` / old layer_count scan) — **не** UI publication channel.
- Application-specific префиксы (e.g. `processed_sequence` перед incremental в `main_window_lifecycle.cpp`) — **вне** envelope; covered косвенно `TestDirectIncrementalPublicationApply`.

### Непроверенные ветки (не блокируют P2.d.2 PASS)

- `UiMirror::ApplyPublished`: **несколько** records в одном batch; **CreateUiShell** для отсутствующего UI shell (chat-style new Node) — нет dedicated headless test.
- `ApplicationModelRuntime` end-to-end incremental с app-specific prefix — не отдельный target (runtime covered structural/incremental косвенно).

**Итог:** **P2.d.2 PASS**, **P2.d PASS** (P2.d.1R persistent + P2.d.2 UI). **NEXT:** **P2.e** — graph Clone prefab.

---

## P2.e.5 — WindowCatalog Add через presented-window prefab (2026-10-28)

**Модель:** `WindowCatalog` class version **1**; **`WindowCatalog/S1`** — только `window_prefab` (`CatalogWindow::ptr`, lazy unloaded). S0 и ClassId без изменений. Prefab ID **15** (W), presenter **16** (V) в [`window_catalog_ids.h`](../../examples/surfaces_demo/common/window_catalog_ids.h).

**Дистилляция:** [`BuildWindowCatalogGraph`](../../examples/surfaces_demo/windows/window_catalog_graph.cpp) — `ComposePresentedWindow` (title prefix + locale), frame 160×120 / 480×320 via FrameEvent/Apply, `SaveRoot` W+V, `catalog.window_prefab` = Declare (`kUnloadedByDefault|kUnloaded`); W/V evicted from domain RAM before `InitializeRuntimeNode(*catalog)`.

**AddWindow:** [`window_catalog_model.cpp`](../../examples/surfaces_demo/common/window_catalog_model.cpp) — `CloneCanonicalRoot` from prefab (stable origin W, `stable_prefab_ids` {W,V}, boundary title prefix + locale); per-instance presenter `title` → holder `primary_text`; no Win32 / no platform presenter create in common code; prefab link restored unloaded after Add.

**Storage:** по-прежнему один `DirectoryDomainStorage` для catalog demo (не production embedded BACKING/USER split).

**Debug PASS:** `apptraverse_window_catalog_model_test` (`TestCatalogAddWindowViaPresentedWindowPrefab` — two Adds, Open/Close, save, cold load, third Add; prefab layers unchanged); `apptraverse_object_clone_test` **exit 0**.

**Demo:** `win32_window_catalog_demo` — **LAUNCHED** with `--state-dir` `manual-test-state/window-catalog-prefab` (не manual PASS).

**Остаётся вне P2.e.5:** holder/title builder в common; полный lazy closure (**13**); embedded lifecycle для catalog.

**Push SHA:** **`a5e4d6f`**.

---

## P2.e.5R — desktop Z-order persistence (2026-10-28)

**Модель:** `WindowCatalog` class version **2**; **`WindowCatalog/S2`** — только `desktop_z_order` (lazy `CatalogWindow::ptr`, top→bottom). S0/S1 без изменений.

**Event:** `SetWindowCatalogDesktopZOrderEvent`; **`SetDesktopZOrderFromObjIds`** на model thread (open windows + `main_window` only).

**Win32:** перед `RequestStop` при закрытии main — `EnumWindows` + HWND→ObjId через UI mirror presenters; **`after_initial`** в `WinApplicationHost` — `SetWindowPos` restore (см. `window_catalog_main.cpp`).

**Debug PASS:** `apptraverse_window_catalog_model_test` (`TestCatalogDesktopZOrderPersistence`), `apptraverse_object_clone_test` exit 0.

**MinSizeRel PASS:** `apptraverse_window_catalog_model_test` exit 0; demo **LAUNCHED** — `manual-test-state/window-catalog-zorder-manual` (Z-order manual **не PASS** до подтверждения пользователя).

**Push SHA:** *(commit этого раздела)*.

---

## P2.d.1R.2b.3b.1 — Win HWND persistence через DomainGraph (2026-10-27, **`3d85a31`**)

**Test:** `RunWinHwndPersistenceTest` — `RamDomainStorage`, `SaveRoot` / cold `LoadRoot`; snapshot `storage.state` до `InitializePresenters`; после OnLoad повторный `SaveRoot` — слои идентичны; повторный `InitializePresenters` — тот же HWND; `UnloadPresenters` — `hwnd() == nullptr`.

**Минимальный сопутствующий fix:** `UiDesktopWindowPresenter::SubmitFrameFromNative` — no-op при `model_proxy == nullptr` (тест вызывает `InitializePresenters(..., nullptr, nullptr)`; native frame sync не требует model thread в headless persistence).

**Debug:** `apptraverse_presenter_win_hwnd_persistence_test` **exit 0**.

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

**Следующий шаг:** **13.3** (graph closure + remap) — [PLAN §13](./PLAN.md#13--универсальный-clone-in_progress).

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
| 12B | PASS | `02272ab` | **12B.1** + **12B.2**; facet + disk roundtrip + prefab/persistence/overlay Debug/Release **7/7** each | P→A origin; RAM overlay + cross-process disk USER/BACKING |
| 12B.1 | PASS | `4e8662b` | `object_origin_facet_test`, `model_persistence_test`, `overlay_domain_storage_test` Debug+Release | **`TestConstPrefabDynamicInstanceRoundtrip`** (overlay RAM) |
| 12B.2 | PASS | `02272ab` | `apptraverse_object_origin_disk_roundtrip_test` (`--write`/`--read` on facet EXE) + regressions Debug+Release | Process 1 save / Process 2 cold load; BACKING **P** frozen; no USER **P** |
| 13 | IN_PROGRESS | `1b9ba5b` | **13.1** + **13.2 PASS**; **13.3–13.6** TODO | **`CloneCanonicalRoot`** — [PLAN §13](./PLAN.md#13--универсальный-clone-in_progress) |
| 13.1 | PASS (doc) | `44bc222` | — | Native Clone audit; **`LoadCopy` rejected** |
| 13.2 | PASS | `1b9ba5b` | `apptraverse_object_clone_test` + regressions Debug/Release; prefab compaction/guard Release | Memory **1601/252** vs disk **150/251**; **`CloneCanonicalRoot`** |
| 14 | TODO | — | — | **S + historical base**; завершение основных **T16/T17** |
| 15 | TODO | — | — | Единая модель поставок R/E/B/S; **T11–T15** |
| 16 | TODO | — | — | **R0–R5** lifecycle; **T01–T18** subset |
| 17 | TODO | — | — | Application/UI integration; baseline в merge **`4848402`**; доведение — **U4–U6**, затем **9D+** |
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

- **2026-10-26** — **12B.1:** `TestConstPrefabDynamicInstanceRoundtrip` in `object_origin_facet_test.cpp` — const **P** in BACKING, dynamic **A** in USER with **`SetOriginId(P)`** via **`OverlayDomainStorage`**; cold **`LoadRoot(A)`** preserves fields/origin without materializing **P**; **12B.1 PASS**; **12B IN_PROGRESS**. Verify: `apptraverse_object_origin_facet_test`, `apptraverse_model_persistence_test`, `apptraverse_overlay_domain_storage_test` Debug+Release; prefab compaction/guard/dynamic-layer matrices Debug+Release. Baseline **`b5f050c`**. Push — **`c1c8a12`**.
- **2026-10-26** — **12B.2 / 12B PASS:** **`apptraverse_object_origin_facet_test`** modes **`--write` / `--read`** + **`object_origin_disk_roundtrip_run.cmake`** — Process 1: **`SaveRoot(P)`** to BACKING, **`SaveRoot(A)`** with **`SetOriginId(P)`** via **`OverlayDomainStorage`** (no **`LoadRoot(P)`**); Process 2: cold **`LoadRoot(A)`**, repeat **`SaveRoot(A)`**. Disk: USER **A** — **Derived/S0+S1**, **Obj/S0+S1**; no USER **P**; BACKING **P** unchanged (Derived/S0+S1, **Obj/S0** only). CTest Debug+Release **7/7 PASS:** `apptraverse_object_origin_facet_test`, `apptraverse_object_origin_disk_roundtrip_test`, `apptraverse_model_persistence_test`, `apptraverse_overlay_domain_storage_test`, prefab compaction/guard/dynamic-layer matrices. **`object_origin.h` not added**. Push — **`02272ab`**.
- **2026-10-26** — **13.1 (doc):** [PLAN §13.1](./PLAN.md#131--native-clone-audit-and-implementation-contract) — audit **`ObjPtr::Clone`/`LoadCopyImpl`** (storage-only, 0 call sites); gap table vs **P→A→B→C**; **`SaveObjectGraphToScratch`** + remap + **`SetOriginId(P)`**; fixtures **`CloneWorkspaceFromPrefabs`** / **`PrefabPtr`** не являются universal Clone. **13 IN_PROGRESS**; **NEXT 13.2**. Код не менялся; тесты не запускались. Push — **`44bc222`**.
- **2026-10-26** — **13.2:** **`CloneCanonicalRoot`** (`object_clone.h/.cpp`) + **`apptraverse_object_clone_test`** — **`SavePersistedObjectGraphToRam`** snapshot, single-object closure, **`SetOriginId(P)`**, **`SaveRoot`**; memory **1601/252** vs BACKING **150/251**; **13.2 PASS**. CTest Debug **4/4**, Release **6/6** (incl. prefab compaction/guard). Push — **`1b9ba5b`**.
- **2026-10-26** — **Merge unified:** **`4848402`** на **`integration/unified-application`** — Object Core + Windows Application + объединённые тесты/CMake; исходные ветки заморожены как baseline.
- **2026-10-26** — **U1 PASS:** Release incremental **`apptraverse_window_catalog_model_test`** CTest **1/1**; тесты + Æther patch header fix; push — **`8835d26`**. Object Core matrix на unified **не** перепрогонялся.
- **2026-10-26** — **U2 manual PASS:** пользователь — **`win32_window_catalog_demo`** (Add/Open/Delete, EN/RU, геометрия, restart с `--state-dir`); **не** заявление о полном CTest/regression PASS.
- **2026-10-26** — **U3 PASS (doc):** синхронизация [PLAN.md](./PLAN.md), PROGRESS, [application-versioning.md](../integration/application-versioning.md), [window_catalog_demo.md](../window_catalog_demo.md); код не менялся; push — **`integration/unified-application`** (commit U3 doc sync).
- **2026-10-26** — **U4 PASS (audit-only):** [unified-architecture-audit.md](../integration/unified-architecture-audit.md) — Critical 2 / Major 5 / Minor 6 / Deferred 4; план U5.1–U5.5; production-код, тесты и CMake не менялись, сборка не запускалась.
- **2026-10-26** — **U5.1 PASS:** shutdown-save WindowCatalog/MinimalApp — `BeginSaveSession`, проверка checkpoint + layer commits, exit code через lifecycle → `main`; CTest **65–67** Release **3/3**; ветка ошибки не инжектировалась.
- **2026-10-27** — **P0 DONE (doc):** импорт [AppTraverse_Architecture_RU.md](../architecture/AppTraverse_Architecture_RU.md); активный план **P0–P6** в PLAN; U5.2–U6 сняты; SharedNode → **полное удаление** в **P1**; следующий шаг **P1.1**.

*(Baseline fixtures: `19d7da6`; **03** — build-time R; **04** — контракт; **05** — embedded graph; **06+** Surfaces GUI / USER на диске.)*

---

## Итерация 04 — контракт const/dynamic (doc)

**Область:** только [PLAN.md](./PLAN.md) §04 и этот раздел. **Срез:** `c139267`. **Enforcement:** не реализован. **Не дублировать:** [01C](#итерация-01c--константность-и-ссылки) (аудит фактов).

**Итог (doc):** идентичность по `ObjId`, Load/Save, ссылки/lazy, `force_materialize` — [PLAN §04](./PLAN.md#решение-итерации-04--контракт-const-vs-dynamic).

**Итог (код):** [`OverlayDomainStorage`](../../include/apptraverse/overlay_domain_storage.h) — `supply_constant_ids`; const Load/Enumerate с BACKING; Store discard (USER не для тел const); тесты в [`overlay_domain_storage_test.cpp`](../../tests/overlay_domain_storage_test.cpp). **Runtime const-mutation checks:** не делаются (архитектурное решение); **Cursor** проверяет Events при изменении кода. **Не сделано:** Surfaces GUI startup из embedded (**06**). **T01–T18:** не PASS. **T16/T17:** не решены.
