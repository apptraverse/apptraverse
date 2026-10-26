# I0 — Architecture cleanup ledger

Inventory for **`integration/application-versioning`** (stages **I0.1** + **I0.2** slice).

**Baseline (I0.1):** `36b0a5c08780a7a0136587a7bf3858b86b334982`
**I0.2 orphan slice baseline:** `6e952d181295d17f9944b225a6a3c36f0587199e`

**Configuration reviewed:** Windows + `APPTRAVERSE_BUILD_EXAMPLES=ON`, CTest as in
`tests/CMakeLists.txt` (17 tests when examples enabled).  
**Method:** source + `rg` call-site survey + CMake target lists. I0.1 inventory
had no builds; **I0.2 slice verified** with incremental Release/MinSizeRel build +
**17/17 CTest PASS** (2026-10-25).

## A. Scope and build map

### Libraries and executables (active integration configuration)

| Target | Role |
|--------|------|
| `apptraverse` | Core static library (model, serialization, both runtimes, overlay, embedded export) |
| `apptraverse_ui_windows` | Win32 presenters, `WinApplicationHost`, message/loading hosts |
| `apptraverse_ui_extended` | **EXCLUDE_FROM_ALL** — edit box / bitmap (not in catalog/minimal graph) |
| `apptraverse_ui_windows_extended` | **EXCLUDE_FROM_ALL** — extended Win presenters |
| `minimal_app_common` | MinimalApplication types + lifecycle |
| `surfaces_demo_window_catalog` | WindowCatalog model + graph |
| `win32_minimal_app`, `win32_minimal_app_load_only` | Minimal app executables |
| `win32_window_catalog_demo`, `win32_window_catalog_demo_load_only` | WindowCatalog executables |
| `apptraverse_export_minimal_distilled_graph` | Build-time embedded header exporter (9C) |

### Active CTest executables (17)

`apptraverse_application_contract_headless_test`,  
`apptraverse_embedded_distilled_domain_test`,  
`apptraverse_overlay_user_backing_test`,  
`apptraverse_historical_base_materialization_test`,  
`apptraverse_minimal_app_embedded_export_test`,  
`apptraverse_application_model_runtime_test`,  
`apptraverse_application_ui_session_test`,  
`apptraverse_ui_desktop_window_geometry_test`,  
`apptraverse_win_application_loading_window_test`,  
`apptraverse_win_application_host_test`,  
`apptraverse_window_catalog_model_test`,  
`apptraverse_window_catalog_win32_acceptance_test`,  
`apptraverse_minimal_app_win32_acceptance_test`,  
`apptraverse_win_application_message_host_test`,  
`apptraverse_win_ui_native_push_button_test`,  
`apptraverse_presenter_win_desktop_frame_sync_test`,  
`apptraverse_win_ui_command_routing_test`.

### Orphan `src/*.cpp` removed in I0.2 (2026-10-25)

These seven files were **not** listed in any active CMake target; **deleted** on
`integration/application-versioning`. Matching **public headers remain** (API
declarations only — **not implemented** on this branch until FUTURE SharedNode /
transport work restores `.cpp` from Git history or parallel branch):

- `src/runtime_lifecycle.cpp`
- `src/shared_node.cpp`, `src/shared_network_graph.cpp`, `src/shared_sync_runtime.cpp`
- `src/memory_transport.cpp`, `src/sync_frame.cpp`, `src/link.cpp`

Disabled historical demos/tests that referenced these units are **unchanged** and
**not** in current CTest; deleting orphan sources does **not** prove those demos
link or run.

### Examples present in tree but **not** added by root `CMakeLists.txt`

`examples/model_ui_runtime_demo`, `chat_ui_runtime_demo`, `main_window_runtime_demo`,
`dynamic_objects_demo`, `shared_node_demo`, `chat_demo`, full multi-platform
`surfaces_demo` (only `surfaces_demo_window_catalog` + `windows/` win32 demos build).

---

## B. Candidate table

Status legend: **ACTIVE** | **TEST-ONLY** | **COMPILED-UNREFERENCED** (in
`apptraverse.lib` but no call site in active link closure) | **DISABLED/HISTORICAL**
| **UNKNOWN**.

| Area | File / symbol | Real consumer | Status | Problem / overlap | Minimal change | Regression test | Risk |
|------|---------------|---------------|--------|-------------------|----------------|-----------------|------|
| Model thread | `ApplicationModelRuntime` (`application_model_runtime.cpp`) | `minimal_app_main.cpp`, `window_catalog_lifecycle.h`, `WinApplicationHost`, `application_model_runtime_test`, host tests | **ACTIVE** | Canonical publication loop (`SerializeInitialPublication`, structural incremental) | Keep | `apptraverse_application_model_runtime_test`, Win32 acceptance | Low |
| UI session | `ApplicationUiSession` (`application_ui_session.cpp`) | `WinApplicationHost` (`OnInitialPublished` / `OnIncrementalPublished`), `application_ui_session_test` | **ACTIVE** | Replaces manual `CopyModelGraphToUiDomain` + `InitializePresenters` in app code | Keep | `apptraverse_application_ui_session_test`, `apptraverse_win_application_host_test` | Low |
| Host stack | `WinApplicationHost` | `minimal_app_main`, `window_catalog_main`, acceptance tests | **ACTIVE** | Thin composition of runtime + session + message host | Keep | `apptraverse_minimal_app_win32_acceptance_test`, `apptraverse_window_catalog_win32_acceptance_test` | Low |
| Legacy model thread | `ModelRuntime` (`model_runtime.cpp`) | `tests/model_ui_runtime_test.cpp`, `tests/model_runtime_stop_test.cpp`, `examples/model_ui_runtime_demo`, `examples/chat_ui_runtime_demo` | **TEST-ONLY** / **DISABLED** | Parallel thread + dirty-node publish via `UiMirror`; superseded by `ApplicationModelRuntime` + publication bytes for apps | Defer removal until legacy demos/tests deleted or isolated library | *Not in CTest* — `model_ui_runtime_test`, `model_runtime_stop_test` | High if removed prematurely |
| Legacy UI apply | `UiMirror` (`ui_mirror.cpp`) | Only via `ModelRuntime` (above) | **COMPILED-UNREFERENCED** in active apps | Second UI update path (channel apply) vs `LoadInitialPublication` / structural publication | Same as `ModelRuntime` | Same legacy tests | High |
| UI graph copy | `CopyModelGraphToUiDomain` (`graph_mirror.cpp`) | Legacy tests (`model_ui_runtime_test`, chat tests, presenter e2e tests); `model_ui_runtime_demo`, `chat_ui_runtime_demo`; comments in `object_serialization.cpp` | **COMPILED-UNREFERENCED** in active apps | Duplicates “model → UI domain” vs publication pipeline used by `ApplicationUiSession` | Keep until legacy path retired; optional split to `apptraverse_legacy_runtime` EXCLUDE_FROM_ALL | Legacy tests only (not CTest) | Medium |
| Publication load | `LoadInitialPublication`, `ApplyStructuralPublicationAndUpdatePresenters` (`object_serialization.cpp`) | `ApplicationUiSession`, contract/model/catalog tests | **ACTIVE** | Not a duplicate of persistent `DomainGraph` save — different scope (`GraphSerializationScope::MaterializedPublication` / structural) | Keep | `apptraverse_application_contract_headless_test`, `apptraverse_application_ui_session_test`, catalog model test | Low |
| Presenter wiring | `ComposePresentedWindow` (`ui_presented_window_composition.h`) | `minimal_app_graph.cpp`, `window_catalog_graph.cpp`, contract test | **ACTIVE** | Required typed cross-links (window ↔ presenter ↔ title/locale) | Keep | `apptraverse_application_contract_headless_test` | Low |
| Presenter lifecycle | `LoadPresentedWindow` / `SavePresentedWindow` / `UnloadPresentedWindow` | `minimal_app_lifecycle.cpp`, `window_catalog_model.cpp` | **ACTIVE** | App-specific call sites; helpers are shared | Keep | Win32 acceptance, catalog model test | Low |
| Direct presenter init | `InitializePresenters` (`ui_presenters.cpp`) | `ApplicationUiSession` ctor; disabled `surfaces_demo` platform apps | **ACTIVE** (via session) | Old demos called directly; production apps use session | Keep | `apptraverse_application_ui_session_test` | Low |
| Runtime lifecycle API | `runtime_lifecycle.h` (header only) | Chat demo sources (not built) | **DISABLED/HISTORICAL** | `.cpp` **removed I0.2**; header kept | Defer header removal until chat demo dropped | None active | Low–Med |
| SharedNode / transport | `shared_node.h`, `shared_sync_runtime.h`, etc. (headers only) | FUTURE roadmap; disabled demos | **DEFERRED/FUTURE** | `.cpp` **removed I0.2**; `APPTRAVERSE_ENABLE_SHARED_NODE=0` | Restore implementation when SharedNode returns | N/A | Low |
| Persistent checkpoint | `SavePersistentModelSnapshotToStorage` / `LoadPersistentModelSnapshot` | `minimal_app_lifecycle`, `window_catalog_lifecycle`, catalog/minimal tests | **ACTIVE** | Separate from layer-wise `DirectoryDomainStorage` + optional checkpoint blob | Keep; embedded startup (9D) must define whether checkpoint remains | `apptraverse_minimal_app_win32_acceptance_test`, catalog model test | Med (9D design) |
| Runtime distill bootstrap | `BuildMinimalApplicationGraph` + `SaveDistilledRoot` in `minimal_app_lifecycle.cpp` (`APPTRAVERSE_ENABLE_DISTILLATION`) | `win32_minimal_app` only (not load-only) | **ACTIVE** | Duplicates build-time graph in `apptraverse_export_minimal_distilled_graph` (9C) | **Deferred to 9D** — remove only after embedded startup E2E PASS | `apptraverse_minimal_app_embedded_export_test` (partial), future 9D | High |
| Runtime distill bootstrap | `BuildWindowCatalogGraph` + distill in `window_catalog_lifecycle.cpp` | `win32_window_catalog_demo` | **ACTIVE** | No build-time export yet (9E) | **Deferred to 9E** | Catalog acceptance | High |
| Build-time export | `apptraverse_export_minimal_distilled_graph`, `WriteEmbeddedDistilledSupplyHeader` | CMake custom command → `apptraverse_minimal_app_embedded_export_test` | **ACTIVE** | Parallel initial graph to runtime bootstrap | Keep; wire production in 9D | `apptraverse_minimal_app_embedded_export_test` | Low |
| Embedded install | `InstallEmbeddedDistilledBacking` | 9A test, 9C consumer test | **ACTIVE** | Distinct from directory bootstrap | Keep | `apptraverse_embedded_distilled_domain_test`, embedded export test | Low |
| Overlay | `OverlayDomainStorage` | Versioning tests; embedded export test (USER overlay) | **ACTIVE** | Not duplicated | Keep | `apptraverse_overlay_user_backing_test`, historical base test | Low |
| Registration core | `EnsureObjectRegistration` (`apptraverse.cpp`) | Every app, test, exporter entry | **ACTIVE** | Forces link of core `APPTRAVERSE_REGISTER` statics | Keep (required for static lib) | All headless tests | Low |
| Registration UI | `EnsureUiPresenterRegistration` | Apps/tests; called from `EnsureWinUiPresenterRegistration` | **ACTIVE** | Idempotent with Win ensure | Keep | Contract, UI tests | Low |
| Registration Win | `EnsureWinUiPresenterRegistration` | `RunMinimalApplicationModel`, `minimal_app_export.cpp`, tests, catalog lifecycle | **ACTIVE** | Redundant call **removed from** `BuildMinimalApplicationGraph` (I0.2 slice #2) | Callers must register before graph build (static lib link) | embedded export + minimal acceptance | Low |
| Registration app | `EnsureMinimalApplicationRegistration`, `EnsureWindowCatalogGraphRegistration` | Respective apps/tests/exporter | **ACTIVE** | App-specific static registrars | Keep | App tests | Low |
| Registration chain | `EnsureWindowCatalogGraphRegistration` | Catalog lifecycle/tests | **ACTIVE** | Re-calls UI+Win ensure (already in lifecycle) | Document as intentional; trim only with proof | Catalog tests | Low |
| Historical test | `tests/overlay_domain_storage_test.cpp` | — | **DISABLED/HISTORICAL** | Superseded by `overlay_user_backing_test` in CTest | Re-enable or delete in I0.2 after diff review | Was standalone; not in CTest | Low |
| Historical test | `tests/model_ui_runtime_test.cpp` | — | **DISABLED/HISTORICAL** | Legacy `ModelRuntime`/`UiMirror`/`CopyModelGraphToUiDomain` | Delete or move with runtime split | Not in CTest | Med |
| Historical test | `tests/model_runtime_stop_test.cpp` | — | **DISABLED/HISTORICAL** | Legacy runtime shutdown | Same | Not in CTest | Med |
| Chat tests | `tests/chat_*`, local `struct UiMirror` | — | **DISABLED/HISTORICAL** | Not chat product; many duplicate mirror stubs | No cleanup until branch scope decided | Not in CTest | Low |
| Extended UI libs | `apptraverse_ui_extended`, `apptraverse_ui_windows_extended` | `apptraverse_win_ui_command_routing_test` only (extended) | **TEST-ONLY** | EXCLUDE_FROM_ALL except one CTest | Keep for extended control tests | `apptraverse_win_ui_command_routing_test` | Low |
| Æther patch | `aether-objects-local-persistent-load.patch` | All `DomainGraph` persistent save/load | **ACTIVE** | Required for integration | Keep | Full CTest suite | High if removed |
| Æther patch | `aether-objects-domain-storage-access.patch` | Ram overlay tests | **ACTIVE** | Storage introspection for tests/export | Keep | Overlay/embedded tests | Med |
| Æther patch | `aether-objects-objptr-reset-flags.patch` | ObjPtr lazy load semantics | **ACTIVE** | Application + catalog lazy windows | Keep | Catalog model test | High |
| Æther patch | `aether-objects-force-materialize.patch` | `Node::Save` historical base, overlay | **ACTIVE** | 9B2 | Keep | `apptraverse_historical_base_materialization_test` | High |
| Æther patch | `reject-unknown-stored-class`, `should-save-version` | — | **DISABLED** | Versioning branch only (9G) | Do not add without audit | N/A | — |

**Candidate count:** 31 rows in section B (including grouped historical tests).

**Status distribution (approximate):** ACTIVE 20 · TEST-ONLY/COMPILED-UNREFERENCED 5 · DISABLED/HISTORICAL 6 · UNKNOWN 0.

---

## C. Safe cleanup candidates (for I0.2 only)

| # | Action | Rationale | Depends on | Tests |
|---|--------|-----------|------------|-------|
| 1 | ~~Remove orphan `src/*.cpp`~~ | **DONE (2026-10-25)** — seven files listed in §A removed; headers kept | — | Release + MinSizeRel **17/17 PASS** |
| 2 | ~~Remove duplicate `EnsureWinUiPresenterRegistration()` in `BuildMinimalApplicationGraph`~~ | **DONE (2026-10-25)** — entry points: `RunMinimalApplicationModel` (`minimal_app_lifecycle.cpp`), `minimal_app_export.cpp`; load-only has no graph builder; tests use own ensures | Static `APPTRAVERSE_REGISTER` still requires explicit `Ensure*` at process entry | Release + MinSizeRel **17/17 PASS** |
| 3 | Archive or delete `tests/model_ui_runtime_test.cpp` + `tests/model_runtime_stop_test.cpp` **only after** (1) legacy runtime moved to EXCLUDE_FROM_ALL lib or removed | Frees `ModelRuntime`/`UiMirror` removal | Decision on keeping legacy demo sources | N/A until re-added to CTest |

**Not safe in I0.2:** removing `ModelRuntime`/`UiMirror`/`graph_mirror.cpp` from `apptraverse` without migration; removing runtime distill bootstrap (9D/9E); removing `SavePersistentModelSnapshot` paths; adding versioning Æther patches.

---

## D. Deferred (not cleanup)

| Item | Owner stage | Notes |
|------|-------------|-------|
| Production embedded startup MinimalApplication | **9D** | Replace runtime distill bootstrap; requires E2E PASS before bootstrap removal |
| WindowCatalog build-time export + startup | **9E** | Parallel to 9D |
| Required cold-load contract tests/docs | **I0.3** | All-or-nothing load failures |
| Release R/E/B/S, active-release, supply.bin | **9F** | |
| Clone/origin/S system core | **9G** | Blocked on serialization-surfaces C0→12R→12B→13→14 |
| Full T01–T18 matrix | **9H** | |
| Legacy `ModelRuntime` migration | **I0.2+** | After consumer inventory proves zero production use |
| Optional supply, ArtifactSource, SharedNode, plugins | **FUTURE** | Documented in `plan.md` §1.6 |

---

## E. I0.2 slices

### First slice — **DONE** (2026-10-25)

Deleted the seven orphan `src/*.cpp` files (§A). Public headers **not** deleted.
Incremental build reported `ninja: no work to do` (files were never in targets);
CTest **17/17 PASS** Release and MinSizeRel.

### Second slice — **DONE** (2026-10-25)

Removed `ui::windows::EnsureWinUiPresenterRegistration()` from
`BuildMinimalApplicationGraph` (`minimal_app_graph.cpp`). Confirmed callers:

- **Dev / distill bootstrap:** `RunMinimalApplicationModel` → ensures (object, UI,
  Win, minimal app) → `BuildMinimalApplicationGraph` when profile empty.
- **Build-time exporter:** `minimal_app_export.cpp` `main` → same ensures → graph
  builder.
- **Load-only** executable does not compile graph builder path for bootstrap.
- **Tests** do not call `BuildMinimalApplicationGraph` directly;
  `apptraverse_minimal_app_embedded_export_test` keeps its own Win ensure before
  cold load.

**Registration note:** Win/UI presenter types still rely on explicit `Ensure*`
at executable or test entry — required for static registrar linkage; graph builder
is not a registration root.

**I0.2 status:** no further **proven** safe dead-path removals in ledger §C;
§C #3 (legacy tests / `ModelRuntime`) remains **deferred**.

**Next stage:** **I0.3B** WindowCatalog required graph. **Still deferred:**
legacy `ModelRuntime`/`UiMirror`; runtime bootstrap removal (**9D/9E**).

---

## G. I0.3A — MinimalApplication required cold-load boundary (DONE 2026-10-25)

**Baseline:** `92b63ae844f157b454cccf890b452dc7806568b8`

| Boundary | Change |
|----------|--------|
| `ui::LoadPresentedWindow` | After `window.Load()` / `presenter.Load()`, empty result → `WriteFatalStderr` + `std::abort()` (required window/presenter). |
| `RunMinimalApplicationModel` | Removed optional `if (main_window.is_valid())`; always `LoadPresentedWindow(main_window)` before `runtime.Run`. |
| `LoadApplication` | **Unchanged** (root missing still fails there). |

**Failure cases verified** (`apptraverse_minimal_app_win32_acceptance_test`, load-only child processes):

- Empty `--state-dir` → `fatal: LoadApplication failed`
- Root persisted, empty `main_window` ref (DomainGraph `SaveRoot`, checkpoint sidecar removed)
- Window persisted, empty `presenter` ref
- Presenter object removed via `DirectoryDomainStorage::Remove(MinimalPresenterId())` → fail-fast before publication (message may be load root or presenter boundary)

**Preserved:** optional/lazy catalog windows (`RestoreLoadedCatalogWindows` skips unloaded holders); dev distillation bootstrap; registration at entry; no auto-create of main window on load-only.

**NEXT:** **I0.3B** WindowCatalog required graph + ClassId/**S** compatibility (not corrupted-byte torture).

---

## F. Acceptance test mapping (active CTest only)

| Concern | Primary tests |
|---------|----------------|
| Headless contract / composition / lifecycle helpers | `apptraverse_application_contract_headless_test` |
| `ApplicationModelRuntime` publication | `apptraverse_application_model_runtime_test` |
| `ApplicationUiSession` + structural apply | `apptraverse_application_ui_session_test` |
| Embedded backing primitive | `apptraverse_embedded_distilled_domain_test` |
| USER/BACKING + supply | `apptraverse_overlay_user_backing_test` |
| Historical base materialization | `apptraverse_historical_base_materialization_test` |
| Build-time embedded minimal graph | `apptraverse_minimal_app_embedded_export_test` |
| Minimal app E2E (runtime bootstrap today) | `apptraverse_minimal_app_win32_acceptance_test` |
| WindowCatalog E2E | `apptraverse_window_catalog_win32_acceptance_test`, `apptraverse_window_catalog_model_test` |
| Host / loading / presenters | `apptraverse_win_application_host_test`, loading window, frame sync, native push button, command routing |

Legacy tests (`model_ui_runtime_test`, `model_runtime_stop_test`, most `chat_*`,
`surfaces_model_test`, etc.) are **not** registered in current CTest — **no PASS
claimed** for this inventory.
