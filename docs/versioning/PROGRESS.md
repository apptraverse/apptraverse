# AppTraverse — прогресс версионирования поставок
## Windows/headless contract follow-up — 2026-11-10 — PASS

Separated mobile catalog event registration from the Windows desktop path; removed serialized defaults from the common list presenter and contract-test model/events; removed the headless presenter's missing-window silent skip; consolidated headless registration at main. The headless mobile scenario remains enabled.

The expanded binary check rejects the previous catalog EXE and passes all four rebuilt normal/load-only consumers. **82/82 Debug**, **17/17 targeted MinSizeRel** tests pass. Catalog is **681,984 B (666 KiB)**, down **2,560 B**; MinimalApp stays **622,592 B**. Both import only KERNEL32/USER32. This closes the four audit findings; the named binary exclusions are not a proof of globally minimal code. [Implementation, regression evidence and validation](../validation/windows-headless-contract-followup-2026-11-10.md).

## One Windows application registration entry point — 2026-11-10 — PASS

WindowCatalog and MinimalApp each register their complete runtime schema once from main, before normal startup or Debug distillation. Duplicate registration in model sessions, distillation, the catalog builder and Windows test helpers was removed. Tests now cold-load through the same entry point as the applications. Internal library anchors and other-platform entry points remain compatible.

**82/82 Debug**, **17/17 targeted MinSizeRel** tests pass, including four binary contracts rejecting full registration modules, unused factories and distillation. Sizes are unchanged: catalog **684,544 B**, MinimalApp **622,592 B**; imports remain KERNEL32/USER32 only. [Implementation, call paths and validation](../validation/windows-registration-entry-points-2026-11-10.md).

## Windows static release runtime and size — 2026-11-10 — PASS

Release configurations now select the static MSVC CRT for the application and dependencies; Debug retains its existing CRT. Windows runtime registration and archive members exclude unused standalone presenters, mobile Close factory, initial graph builders and distillation/directory-persistence code. Full library registration entry points remain available.

Final MinSizeRel: WindowCatalog **684,544 B (668.5 KiB)**, MinimalApp **622,592 B (608 KiB)**. Both import only KERNEL32 and USER32. Pruning saves **24.5 / 18 KiB** against the static-CRT-only controls; the static CRT itself adds **212 / 214.5 KiB** against the dynamic baseline. **82/82 Debug**, **17/17 targeted MinSizeRel** tests pass, including four binary contracts and native save/restart of the normal EXEs. [Size analysis, implementation and evidence](../validation/windows-static-runtime-size-2026-11-10.md).


## RAM USER and one shutdown file — 2026-11-10 — PASS

WindowCatalog desktop and Windows MinimalApp now use `SingleFileUserStorage`: startup loads `user.bin` into ordinary RAM domain storage; child Close saves only to RAM; application shutdown writes the complete USER layer table and scope metadata to one file. BACKING remains separate and is in memory during the session (including normal Debug). No destructor/background flush, session sidecar or migration of old directory profiles. Existing binary streams encode the file.

Verified zero files after fresh child Close, unchanged existing file bytes/timestamp, RAM lazy reload, cold file restoration of open/closed child geometry and private presenter state, independent sessions, scopes/tombstones and overwrite/error handling. **85/85 Debug tests PASS**, **4/4 targeted MinSizeRel tests PASS**, **40/40 MinSizeRel timing observations PASS**. Child Close medians: Debug **116/148/157/111 ms**, MinSizeRel **23/42/15/13 ms** (cold/burst/stable/saved). MinSizeRel process exit with open child, including file save: **43–55 ms** median across scenarios. [Full validation and scope](../validation/single-file-user-storage-2026-11-10.md).

## Immediate application event compaction — 2026-11-10

Application runtimes now fold committed events into the base immediately; new nodes inherit this runtime policy. Geometry tests verify empty journals, current bases, zero USER writes before Close, and cold restoration. Library history/version guards remain intact. All 83 non-timing Debug entries passed across the full run plus corrected MinimalApp rerun; Debug timing PASS; 3/3 targeted MinSizeRel tests and 20 normal-executable Close observations PASS. Observed Debug Close slowdown is explicitly unresolved, not an optimization claim. See [validation and timings](../validation/immediate-event-compaction-2026-11-10.md).

## Automatic clone boundaries, Debug preparation and Close — 2026-11-10 — PASS

Branch: `feature/domain-hierarchy-cloning`, follow-up to `166631a`. The final implementation and tests are recorded in [English validation](../validation/window-catalog-automatic-cloning-2026-11-10.md).

- WindowCatalog no longer supplies `boundary_ids`. Explicit roots copy; parent-domain and semantic constant references remain shared. Common persistent scope metadata retains this partition after cold load, including nested scopes.
- Corrected temporary-domain storage shadowing and relocation of serialized raw ObjPtr fields absent from reflection. Regression tests cover live/cold lazy external references, private internal ownership, exact flags, no eager parent Load and zero runtime USER Store.
- Normal Debug regenerates the initial graph and uses a fresh USER each launch. Windows standalone catalog exporter removed; MinSizeRel embeds Debug-prepared supply and retains ordinary USER restore behavior. Separate object/layer files remain the disk format.
- Close uses shared save traversal and cheaper directory preparation. Private presenter state, lazy window links and ancestor storage ownership remain verified.
- Fresh **26/26 patch chain PASS**; **84/84 Debug CTest entries PASS** including Close timing; **4/4 targeted MinSizeRel tests PASS**. Assertions remain enabled in Debug, `AE_NO_DEBUG_LOG=ON`.
- Final Close HWND destruction medians, 5 repetitions each: **400 / 505 / 550 / 171 ms** (cold / burst / stable geometry / saved). Main exit 125–190 ms. Final model-only disk/RAM controls: 316/4 ms cold, 523/7 ms burst. Individual file commits remain the primary cost; no claim of instantaneous Close.

Full logs, fixed-supply A/B, final supply hash, exact test list and deferred scope are linked in the validation record. No source changes followed final acceptance. Adaptive WIP was not modified.

## Domain hierarchy cloning and persistence boundaries — 2026-11-10 — PASS

Branch: `feature/domain-hierarchy-cloning`; completion work started from `580f36d7ddffd995d61eb026d1bca010d11582c7`. This entry supersedes the earlier session-overlay and partial hierarchy acceptance notes for this branch. Adaptive WIP was not modified.

- Completed ancestor lookup, clone finalization and owning LocalPtr/ObjPtr rebinding without traversing external boundary graphs.
- History capture/replay/compaction use operation-local scratch in the owning domain; no runtime USER injection or session USER RAM store. Ordinary explicit saves preserve compacted history across BACKING replacement. Partial known-prefix compaction preserves unknown version layers and retained events.
- Unload saves presenter-only state before reset, then persists the lazy window. Cold holder/window/presenter/geometry, shutdown, seeded USER and session isolation checks pass. Bootstrap/Add/geometry/language work performs zero USER Store calls.
- Corrected direct UI mirror serialization and Windows acceptance fixtures, including one shared MinimalApp generated supply.
- Fresh pinned source plus all **22 repository patches: PASS**. Windows x64 MSVC/Ninja Debug, `AE_NO_DEBUG_LOG=ON`, assertions/runtime checks enabled: **83/83 CTest entries PASS**.
- Debug Close, five repeats per scenario, HWND destruction medians: **679 / 830 / 842 / 225 ms** (cold / burst immediate / after settle / saved). Main exit **108–157 ms**; no timeouts. Real persistence is enabled. These measurements establish remaining Close cost, not a claimed speedup over the earlier session-overlay implementation.

[English plan and completed progress](../domain-hierarchy-cloning-plan-progress.md) · [Full test and timing evidence](../validation/domain-hierarchy-cloning-2026-11-10.md).


## Source of truth

1. Google Doc: [AppTraverse — версионирование состояния и жизненный цикл поставок](https://docs.google.com/document/d/196STRvU1QMbMu0ArGRLahPpgnTb6HQoldUV-7tOIcfo/edit) — **прямая сверка не выполнена** (403 при fetch).
2. Архитектурный контракт и roadmap: [PLAN.md](./PLAN.md) — **AppTraverse Libraries Architecture RU v0.3 (25.10.2026)** + Object Core **`01_Object_Core_Versioning_PLAN.md` (25.10.2026)** ([share](https://chatgpt.com/library/share/libfile_c97b08ed7af88191a916944ac06566f7?account_id=personal)).

**Branch (рабочая):** **`feature/window-catalog-adaptive`** (платформенная приёмка Windows→…); историческая **`feature/linux-window-catalog`**, base `d590ee7f932d1f6df82c196f36c99e3b5e0b9102` (`origin/integration/unified-application`). Историческая координация: [PLAN §Unified](./PLAN.md#unified-integration-историческая-координация--merge).

<a id="p4-wc-close-timing-diagnostic--2026-11-08"></a>

## P4 / WindowCatalog — диагностика Close timing harness + `AE_NO_DEBUG_LOG` A/B (2026-11-08)

**База harness:** `088ea28e59610c3b95a66f65979293baed13354f`. **Production:** без изменений. **Target:** `apptraverse_window_catalog_win32_close_timing_diagnostic_test` — только subprocess demo Debug, логи stdout/stderr в каталог прогона **вне** USER.

**Ошибки старого harness (до fix, ~30 s «зависания» — не подтверждённый production hang):** подстрока в title (`Window 1` / локаль / `Window 10`); stderr-файл внутри профиля; `PeekMessage` в harness; `(Closed)` в list трактовался как destroy HWND; timeout записывался как время операции; MinSizeRel/in-process смешаны в отчёт.

**Исправленный harness:** HWND child по Δ top-level после Add или точное совпадение строки list ↔ title; `TARGET_NOT_FOUND` без WM_CLOSE; отдельно wm_close / hidden / destroy / list / main exit; burst = `WM_ENTERSIZEMOVE`…`WM_EXITSIZEMOVE` на HWND demo; CMakeCache `AE_NO_DEBUG_LOG` + compile defs `aether-objects` (`-DAE_NO_DEBUG_LOG=1` при ON). Артефакт `surfaces_catalog_agent_fresh_embedded` рядом с demo **невалиден** (`LoadApplication failed id=1`); seed для saved-сценария — runtime copy после cold Add+Save (`SAVED_PROFILE_SEED=runtime_prepared`).

**Медианы HWND destroy / list (ms), 3× rep, `win64-ninja-msvc-debug`:**

| Сценарий | `AE_NO_DEBUG_LOG=OFF` | `AE_NO_DEBUG_LOG=ON` |
| --- | --- | --- |
| COLD_ADD_CLOSE | 354 / 344 | 352 / 329 |
| BURST_CLOSE_IMMEDIATE | 527 / 516 | 476 / 451 |
| BURST_CLOSE_AFTER_SETTLE | 580 / 568 | 515 / 499 |
| SAVED_PROFILE (runtime seed) | 200 / 179 | 153 / 131 |

**OBJ_SYS (subprocess stdout, не stderr после redirect):** OFF ~4.6k–11.7k строк/close (burst выше); ON → **0**. Main exit ~107–185 ms; **30 s child timeout на исправленном harness не воспроизведён**. Debugger stacks / CPU% — **NOT RUN** (нет реального timeout).

**Политика USER (embedded session, @ `87302ee`+):** `session_overlay` владеет конкретная model-сессия; **нет** process-global `Active()` — flush привязан через `ApplicationModelRuntime::BindUserSessionStorage` + `WindowCatalog::BindEmbeddedUserSession` / `MinimalApplication::BindEmbeddedUserSession`. **Close:** `UnloadPresentedWindow` → `WindowHolder::Save` → `FlushReachableSubgraphToDisk(holder)`: `SavePersistedObjectGraphToRam` в staged, **слои из `session_overlay` (в т.ч. окно после `Save`) накрывают staged**, запись на `DirectoryDomainStorage` (optional `InstrumentedDomainStorage`), снятие flushed id из RAM. **Aux close:** flush по `domain.Find(aux_window_id)` после `UnloadPresentedWindow`. **Shutdown:** snapshot + `StagePersistentGraphInSessionOverlay` + `FlushEntireSessionOverlayToDisk`. Тест `apptraverse_window_catalog_user_persistence_boundary_test`: `InstrumentedDomainStorage` **0** `Store` до close; после close — holder+window USER dirs + layers; existing USER / две сессии изолированы; shutdown cold — язык/ObjId **PASS** (boot **0**, close **+17**, shutdown **204** в прогоне @ flush-fix). Регрессия Debug `AE_NO_DEBUG_LOG=ON`: WC model/headless/embedded_overlay/win32_acceptance, minimal headless, application_contract, unload round-trips, boundary — **PASS**. Close-timing harness medians hwnd_destroy: cold **158**, burst imm **204**, after settle **249**, saved **112** ms (`AE_NO_DEBUG_LOG=ON`, 5×). **Ручная Win32** — **NOT RUN** (агент).

**Локализация (Debug, `AE_NO_DEBUG_LOG=ON`, disk USER+BACKING):** узкое место — **двойной `Save` в `UnloadPresentedWindow`**: pre-`presenter.Reset()` сохранял уже выгружаемый loaded-presenter subgraph; достаточен один `Save` после `Reset` (unloaded lazy flags; open-state — journal `SetWindowCatalogOpenEvent`). Harness medians hwnd_destroy (до RAM-buffer, @ `2eca5e2`): cold **381→319 ms**, burst immediate **520→430**, after settle **563→474** (5× rep). Тест `apptraverse_window_catalog_close_unload_disk_stage_test` — model-only close ms + `InstrumentedDomainStorage` store_calls.

**Обратная совместимость `UnloadPresentedWindow` (Debug disk USER, `AE_NO_DEBUG_LOG=ON`):**

- **Shared-title (MinimalApp):** `apptraverse_unload_presented_window_minimal_disk_roundtrip_test` — общий `aux_title` с `closer->window_title` / controls; только `ui::UnloadPresentedWindow`. Helper **4fe19b2** vs **2eca5e2**: оба **PASS**, probe на диске и через presenter/closer.
- **Presenter-only title (без дублирующих refs):** `apptraverse_unload_presented_window_presenter_branch_disk_roundtrip_test` — root держит только `presented_window`; `title`/`locale` только через `ComposePresentedWindow` → presenter; baseline `PresenterBranchBaseline` → probe `PresenterBranchProbe-X9` via `pres.title->strings[0]`; harness сбрасывает временные владеющие копии presenter/title/string до `UnloadPresentedWindow`; reload — lazy window valid/unloaded, presenter, probe. Helper **4fe19b2** vs **2eca5e2**: оба **PASS** (ожидание probe в обоих вариантах). **Путь:** мутация materialized `UiString` + post-`Reset` `window.Save()` сериализует изменённый leaf без pre-`Reset` Save; внешних ссылок на title в графе нет. **Регрессия данных** (потеря presenter-эксклюзивной ветви без pre-`Reset` Save): **не воспроизведена** — production helper **2eca5e2** **не откатывали**. Close-timing harness — **NOT RUN** (production helper без изменений).

**Неизвестно:** decrement graph / sampling на UI keepalive teardown; валидность bundled `agent_fresh_embedded` в tree.

**Устаревшие строки (первый прогон `088ea28e`, невалидный измеритель):** MinSizeRel cold ~30 s, burst/saved «timeout 30 s» — **снято как доказательство production**, оставлено только как артефакт flawed harness.

<a id="p4-wc-adaptive-windows-acceptance--2026-11-07"></a>

## P4 / WindowCatalog adaptive — Windows приёмка (2026-11-07) — IN_PROGRESS (автотесты verified; ручная resize/layout PASS @ `baa90bf`; очередь — повторная ручная после ownership fix)

**Worktree:** `apptraverse-wt-adaptive`. **Ветка:** `feature/window-catalog-adaptive`. **База этапа (resize coalesce):** `d7fb0267e7fc0f4e0675d57265b020d5ddf211e4`. **Google Doc** «архитектура / каталог» и «версионирование» — **прямая сверка не выполнена** (403); опора — repo `docs/architecture/AppTraverse_Architecture_RU.md` (§25.3 production hooks), [PLAN](./PLAN.md).

**Debug exe:** `build/win64-ninja-msvc-debug/examples/surfaces_demo/windows/win32_window_catalog_demo.exe`

**Ручная приёмка (мышь/клавиатура):** новый профиль, напр. `--state-dir %TEMP%\apptraverse_wc_manual_acceptance` — **NOT RUN** (агент; полный чеклист — задача пользователя).

### Исправления (подтверждённые дефекты)

| # | Принцип / место | Нарушение | Исправление / проверка | Статус |
| --- | --- | --- | --- | --- |
| 1 | List layout ← mirror | `WinListContainerPresenter::SyncFromMirror` не двигал LISTBOX | `ApplyListLayoutFromMirror` из mirror `layout_*` | PASS (`win32_acceptance`, materialization) |
| 2 | Viewport native → model (без mirror loop) | После снятия list `OnModelChanged` report не было Windows-доставки | `ReportNativeFrameToModel` → Main: один coalesced `runtime.Post` (frame+viewport); child: `SubmitFrameFromNative` | PASS (`win32_chrome_adaptive_test`) |
| 2b | Interactive resize backlog | Два FIFO job на resize → хвост публикаций; geometry/viewport рассинхрон | `WindowCatalogMainNativeFrameQueue` на `WindowCatalogModelSession`, bind через `ModelObjectProxy`; coalesce до drain | PASS (coalesce/isolation tests; user live resize **PASS** @ `baa90bf`, не полная архитектурная приёмка) |
| 2c | Queue ownership | process-global queue, fallback 2× Post, `_for_test`, `BindPostWork` сбрасывал slot | Сессионная очередь, без fallback/shared; proxy route; `ClosePosting` + drain | PASS (авто isolation/shutdown) |
| 3 | Startup / lazy | Молчаливые return при failed HWND | Повторный обзор: обязательный граф по-прежнему в Prepare; create-fail на child — как раньше; list layout — `FatalWin32` на границе | CHECKED (нового нарушения нет) |
| 4 | §25.3 без production hooks | Файловый focus-счётчик в demo + presenter test counters | Удалены; acceptance — `GetForegroundWindow` / HWND geometry / `pending_reports` | PASS |
| 5 | Close → Unload → Save USER | RAM-only / лишние записи | Без изменения ядра: `window_catalog_embedded_overlay_test` + model save/close сценарии | PASS (существующие тесты) |
| 6 | Поздний native input после Close/Delete | Lifetime presenter vs model | Штатный proxy + lifecycle в acceptance; interactive resize: live client + viewport до `WM_EXITSIZEMOVE`, restore geometry — на exit | PASS |
| 7 | Embedded Load → … → new process | Цепочка end-to-end | Win32 acceptance two-process + embedded overlay cold | PASS (авто) |

### Расхождение спецификации (отдельно, без правки loader)

Google Doc **§5.1** — платформенный потомок presenter из **stored-most-derived** слоя графа; в коде — **`GetMostRelatedStoredFactory`** / patch chain (leaf-related stored factory). **Зафиксировано**; loader и автоматический leaf-only rollback **не** делались.

### Автотесты (incremental `win64-ninja-msvc-debug`, 2026-11-07)

| Контур | Итог |
| --- | --- |
| `apptraverse_window_catalog_{headless,model,embedded_overlay,win32_acceptance}_test` | PASS |
| `apptraverse_presenter_win_desktop_{frame_sync,interactive_drag_e2e,frame_publication_e2e}_test` | PASS |
| `apptraverse_window_catalog_win32_chrome_adaptive_test` | PASS (burst coalesce enqueue; HWND layout — `win32_acceptance`) |
| `apptraverse_window_catalog_main_native_frame_coalesce_test` | PASS |
| `apptraverse_win_ui_native_push_button_test`, `apptraverse_ui_control_materialization_test` | PASS |
| `apptraverse_application_contract_headless_test` | PASS |
| persistence / Clone / origin disk matrix | **NOT RUN** (общий storage не менялся) |

**Измерения (harness, не live drag):** pending model jobs на 8 burst resize Main — **16** (fallback) vs **1** (coalesce); время публикации/live lag — **NOT MEASURED** (без внешнего профиля в этом прогоне).

**NEXT (Windows):** повторная ручная проверка ownership-fix на свежем Debug EXE / отдельном `--state-dir`; user resize/layout **PASS** зафиксирован @ `baa90bf` (не full arch). Linux/Android/Web/Apple — **не начинать** без отдельной команды.

<a id="p4-wc-adaptive-save-after-close-language--2026-11-07"></a>

## P4 / WindowCatalog — Save после close child + смена языка, supply-локализация (2026-11-07) — PASS (Windows/Android Debug, verified)

**Ветка:** **`feature/window-catalog-adaptive`**, base `9b74f16`.

### Причина (cdb)

- **Chrome E2E assert:** UI-поток, домен UI mirror: `ResolveWindowCatalogHolderListRowText` → `ObjPtr<UiString>::operator->` для слота EN у `list_row_title_format`. Модель (`MaterializeCatalogStrings`) не материализовала активный слот у `list_row_title_format`, `list_row_title_closed_suffix` и `window_title_prefix`, поэтому слот не попадал в publication и mirror не мог его загрузить. Общий `primary_text` и Save здесь ни при чём.
- **`TestCatalogSaveAfterLanguageToggleAndChildClose` exit 3:** `dynamic_cast` в `BindCatalogDesktopPresenter` при `/GR-` → `__RTDynamicCast` → terminate.

### Исправление

- `MaterializeCatalogStrings` материализует активный язык у format/suffix/title prefix.
- Связь presenter → catalog задаётся при создании presenter-а (typed `create_window(domain, id, catalog)`), а не отдельным Bind. `window_catalog_presenter_wiring.h` и все `Bind*` удалены, RTTI нет.
- Удалены обходы WIP: `Declare` в `UnloadPresentedWindow`, `FinalizeWindowCatalogJournalForPersist`, Clear/Rebind `primary_text`, close-mark, overlay-подмена в headless E2E. Save — единственный путь.
- Supply-классификация по роли (`CollectWindowCatalogSupplyConstantIds`) используется во всех экспортёрах (Windows, Linux, Android): тела локализации лежат только в BACKING.
- Win32 acceptance: профиль читается так, как его читает приложение (USER поверх embedded BACKING). `--user-only-load-probe` берёт user-owned объекты только из USER, а supply — из embedded. Позитивный контроль на валидном профиле проходит. Negative «нет `label_add` в USER» заменён проверкой контракта: `label_add` — supply, в USER тела нет.

### Прогоны (incremental Debug)

| Контур | Итог |
| --- | --- |
| `apptraverse_window_catalog_headless_test` (вкл. Chrome E2E) | PASS |
| `apptraverse_window_catalog_embedded_overlay_test` (close → EN→RU→EN → Save → reopen → cold) | PASS |
| `apptraverse_window_catalog_win32_acceptance_test` | PASS |
| `TestCatalogSaveAfterLanguageToggleAndChildClose` и прочие сценарии model | PASS (по отдельности) |
| `apptraverse_window_catalog_model_test` целиком | PASS (historical remap: live presenter; mutable leaves always isolated; PrepareReachable… + distill persist before Init) |
| Android `:app:assembleDebug` | PASS |
| Linux | NOT RUN |

<a id="p4-wc-linux-child-close-list-identity--2026-11-05"></a>

## P4 / WindowCatalog — Linux child close SIGSEGV, list identity, frame request (2026-11-05) — PASS (Linux slice, verified)

**Ветка:** **`feature/linux-window-catalog`**, base remote **`59e54bb`** (production-код **`847f898`**).

### Подтверждено (core dump / GDB / проба)

- **SIGSEGV exit 139 (ручной MinSizeRel):** core `wsl-crash-…-28334-…linux_window_catalog_demo-11.dmp`. Модельный поток: `LinuxDesktopWindowPresenter` → `ModelObjectProxy::Invoke<UiDesktopWindow,int×4>` → `UiDesktopWindow::SubmitFrameFromNative+49` (`cmp …,0x108(%rdi)`, **`rdi=0`**). UI-поток в это время — штатный `ApplyStructuralPublication`. Это обращение к цели, которой нет в домене (`domain.Find(id) == nullptr`), а не UAF: окно уже выгружено `UserCloseWindow` / `CloseHolderWindow` (`UnloadPresentedWindow`). В MinSizeRel `assert` отсутствует, поэтому происходит null-разыменование.
- **Debug-воспроизведение под GDB** (main + 3 child, move + close child): срабатывает `assert("model proxy target must exist")`. Порядок: `OnDeleteEvent` → `UserCloseWindow` в очереди модели → далее `configure-event` того же presenter-а → `SubmitFrameFromNative` для уже выгруженного окна.
- **«Закрытие child завершает приложение»** — это то же падение. Breakpoint на `RequestStop` при закрытии child **не срабатывает**. Время работы (~50 мин) причиной не является: достаточно одного move + close.
- **WSLg X11 (Weston xwm), проба `gtk_window_move`:** client оказывается в `(x+6, y+27)`, а `_NET_FRAME_EXTENTS` = l38 t59, то есть смещение не описано extents. До размещения WM приходят `configure` с позициями 0,0 / −32768. Прежний presenter отправлял их в модель как кадры.
- **Случайная позиция main на cold:** у main не было `USPosition`/`PPosition` (`XGetWMNormalHints`), у child был `PPosition`. WM размещал окно, запрошенное в (0,0), по своему усмотрению. С `GDK_HINT_USER_POS` main 3 из 3 раз оказывается в (6,27), raw-кадры save и cold совпадают.
- **Мерцание selection:** `UpdatePresentersAfterStructuralPublication` вызывает `OnModelChanged` на каждую publication, включая отчёты о геометрии. `LinuxListContainerPresenter::SyncFromMirror` пересоздавал все `GtkListBoxRow` и заново выставлял selection. Тест identity на прежнем presenter-е падает: `fatal: geometry publication recreated GTK list rows`.
- **Падения Weston во время прогонов:** дампы `_usr_bin_weston-6.dmp` (17:24:19, 17:29:57) содержат `window-manager.c:1387: weston_wm_handle_map_request: Assertion '!window->shsurf' failed`, WSLg 1.0.79. Первое падение произошло во время цикла чистой GTK-пробы, без процессов приложения. Вместе с Weston завершился ручной процесс пользователя (pid 28430). Профиль `linux_window_catalog_geom_z_manual_wslg` не изменялся.

### Исправление

- **`LinuxDesktopWindowPresenter`:**
  - `OnDeleteEvent` отправляет последний кадр, затем выставляет `user_close_requested_` и отключает `configure-event`/`size-allocate` **до** команды закрытия. После команды, которая может удалить окно из модели, presenter больше не шлёт отчёты.
  - Кадры отправляются только после размещения WM: окно mapped, client-прямоугольник на мониторе. Координаты переводятся в систему `gtk_window_move` через смещение первого размещения относительно последнего запроса.
  - `OnModelChanged` обновляет только title. Кадр применяется только по request-sidecar (`ApplyRequestedFrameFromMirror`), поэтому запоздавший отражённый report не становится командой.
  - `GDK_HINT_USER_POS` при `OnLoad`.
- **`LinuxListContainerPresenter`:** native rows хранятся по `ObjId`. Text, порядок и selection обновляются адресно, selection — только если целевая строка отличается от текущей. Программный sync не порождает selection-команду.
- **Automation/smoke:**
  - `l3_child_close_save`/`_cold`: 6 итераций move + close child с проверкой, что main и соседи не затронуты, и повторное открытие.
  - Identity строк и отсутствие `row-selected` при move/resize.
  - Raw-кадры save == cold, язык RU после cold.
  - `l3_geom_z_*` ждут, пока окна устоятся, перед move и перед close. Раньше тест проходил только за счёт отчётов, пришедших после delete.
  - Между запусками процессов в smoke пауза 3 с (см. гипотезы).

### Гипотезы (не подтверждены)

- **Delete кнопкой во время in-flight `configure` удаляемого child:** `DeleteSelected` выгружает окно до unload presenter-а, и тот же класс null-target теоретически возможен. **Не воспроизведено** и не исправлено.
- **Механизм падения Weston:** предположительно повторное использование XID окнами последовательных процессов, пока у xwm ещё есть запись с `shsurf`. Доказана только корреляция: без пауз 1 полное прохождение из 3 и 2 падения Weston, с паузой 3 с — 36/36 фаз и 5/5 smoke без падений.

### Прогоны (WSL Ubuntu, incremental, `GDK_BACKEND=x11`)

| Контур | Debug | MinSizeRel |
| --- | --- | --- |
| `tools/linux_window_catalog_l3_smoke.py` (child close + L3 + geom/Z) | **PASS** (2/2 повтора) | **PASS** (3/3 повтора) |
| `tools/linux_minimal_app_l3_smoke.py` | **PASS** | **PASS** |
| `apptraverse_window_catalog_headless_test` | **PASS** | **PASS** |
| `apptraverse_minimal_app_headless_test` | **PASS** | **PASS** |
| `apptraverse_ui_control_materialization_test` | **PASS** | **PASS** |
| Win32 / WASM / Android | **NOT RUN** (изменены только Linux-файлы) | |

**MinSizeRel exe:** 792 192 B; stripped-копия 518 568 B.

<a id="p4-wc-linux-gtk-embedded--2026-11-05"></a>

## P4 / WindowCatalog — Linux/GTK geom + Z-order persist (2026-11-05) — PASS (Linux slice, verified)

**Ветка:** **`feature/linux-window-catalog`** @ **`847f8985ee33cf2b57e417a892b435b1be237f41`** (base **`bc3a5d03525495c0384f3151ce02834f659abc7f`**).

### Дефект (подтверждён на `@bc3a5d0`, `GDK_BACKEND=x11`)

После native move/resize и смены перекрытия окон shutdown → cold launch **не** восстанавливал геометрию (main оставался на bootstrap **80,80**) и не применял stacking. Причина: `LinuxDesktopWindowPresenter` — frame только в `OnLoad`; shutdown копировал **устаревший** `desktop_z_order` из модели; `after_initial` не вызывал restore.

**Среда smoke (WSL Ubuntu + WSLg):** для полного сценария geom+Z smoke **явно** задаёт **`GDK_BACKEND=x11`** (Wayland: native stack через `_NET_CLIENT_LIST_STACKING` недоступен — не подставляем порядок списка/focus). При X11 — EWMH `_NET_CLIENT_LIST_STACKING` + `gdk_window_raise` (ограничения compositor/WSLg: cold Z-verify в smoke **best-effort**, marker `WINDOW_CATALOG_LINUX_Z_ORDER_COLD_OK` при совпадении).

### Исправление

- **`LinuxDesktopWindowPresenter`:** `configure-event` / `size-allocate` на client → `ReportNativeClientFrame` → `SubmitFrameFromNative` (без отката к mirror при user drag; `ApplyRequestedFrameFromMirror` только из model request).
- **`linux_desktop_window_stack`:** X11 capture/apply desktop stack (только окна каталога).
- **`window_catalog_linux_desktop_zorder`:** catalog ObjId mapping; shutdown — `FlushCatalogNativeFrames` + native capture → `SetDesktopZOrderFromObjIds`; cold — `RestoreCatalogDesktopZOrder` в `after_initial`.
- Smoke: main + 3 child, geom+Z native, два save/cold цикла (`l3_geom_z_*`).

**Ветка (prior slice):** base `d590ee7` (integration).

### Переиспользование

- Общая модель: `examples/surfaces_demo/common/window_catalog_*` (builder, model, actions, prepare, localization, `WindowCatalogModelSession` + `RunEmbeddedApplicationModelSession`).
- Платформа: `LinuxApplicationHost`, `LinuxPresentationHost`, `LinuxDesktopWindowPresenter`, `LinuxListContainerPresenter`, `LinuxPushButtonPresenter`.
- Образец embedded: `examples/minimal_app/linux/` (exporter + `apptraverse_embed_distilled_supply.cmake`).
- Legacy **`linux_surfaces_demo`** / `linux_app` / `linux_presenters` **не** трогались.

### Добавлено

- `examples/surfaces_demo/linux/window_catalog_graph_linux.cpp` — `CatalogListContainerPresenter` + `BuildWindowCatalogGraph` (Linux factories).
- `examples/surfaces_demo/linux/window_catalog_export_linux.cpp` — host exporter → `embedded_distilled_window_catalog_supply.h` (Linux ClassId/layers).
- `examples/surfaces_demo/linux/window_catalog_main.cpp` — thin GTK host (close/focus/persist; flush frames + native Z capture on main close; Z restore after initial load).
- `window_catalog_linux_desktop_zorder.*`, `linux_desktop_window_stack.*` — X11 stack + catalog wiring.
- `examples/surfaces_demo/linux/window_catalog_linux_session_automation.*` + `tools/linux_window_catalog_l3_smoke.py`.
- `examples/surfaces_demo/common/window_catalog_graph.h` (общий заголовок graph API; Windows graph .cpp без изменения семантики).
- Маркеры embedded load в `window_catalog_lifecycle.cpp`: `WINDOW_CATALOG_LOADED_FROM_EMBEDDED_BACKING` / `…_PROFILE`.

**Target:** `linux_window_catalog_demo` (`APPTRAVERSE_WINDOW_CATALOG_USE_EMBEDDED_DISTILL`).

### Ограничения GTK / WM

- **Wayland / default WSLg без `GDK_BACKEND=x11`:** native desktop Z-order **не** читается/не применяется (пустой capture, без подстановки порядка rows/focus).
- **X11 + EWMH:** persist/restore stacking только для top-level окон каталога; чужие окна не трогаем.

### Прогоны (WSL **`Ubuntu`**, incremental, smoke с **`GDK_BACKEND=x11`**)

| Контур | Результат |
| --- | --- |
| `tools/linux_window_catalog_l3_smoke.py` (**Debug**, geom/Z two-cycle) | **PASS** |
| `tools/linux_window_catalog_l3_smoke.py` (**MinSizeRel**, geom/Z) | **PASS** |
| `apptraverse_window_catalog_headless_test` (Debug) | **PASS** |
| `tools/linux_minimal_app_l3_smoke.py` (Debug) | **PASS** |
| `apptraverse_window_catalog_win32_acceptance_test` (Win Debug) | **NOT RUN** (exe не собран на хосте) |
| WASM `wasm_window_catalog_browser_smoke.py` | **NOT RUN** |
| `apptraverse_window_catalog_embedded_overlay_test` | **NOT RUN** (Win-only target) |

### MinSizeRel executable (x86-64, GCC 13.3, `-DCMAKE_BUILD_TYPE=MinSizeRel`, `build/linux-x64-minsizerel`)

| | Байты | KiB (1024) |
| --- | --- | --- |
| **Оригинал** (not stripped) | 769 984 | ~752.0 |
| **Копия + `strip`** (оригинал не заменялся) | 501 984 | ~490.2 |

**Путь:** `build/linux-x64-minsizerel/examples/surfaces_demo/linux/linux_window_catalog_demo`  
**Debug:** `build/linux-x64-debug/examples/surfaces_demo/linux/linux_window_catalog_demo`  

**Динамические зависимости (ldd):** `libgtk-3`, `libgdk-3`, GLib/GObject/GIO, Pango/Cairo, X11/Wayland client libs, … — **размер ELF не включает** системные GTK/shared libraries.

### Ручной запуск (MinSizeRel, отдельный USER, X11 geom+Z)

PowerShell:

```powershell
wsl -d Ubuntu env GDK_BACKEND=x11 /mnt/c/Users/nickc/Projects/apptraverse-wt-web5/build/linux-x64-minsizerel/examples/surfaces_demo/linux/linux_window_catalog_demo --state-dir /mnt/c/Users/nickc/Projects/apptraverse-wt-web5/build/manual_profiles/linux_window_catalog_geom_z_manual_wslg
```

Повтор с тем же `--state-dir` восстанавливает USER overlay. Профили **`linux_window_catalog_manual_wslg`**, **`linux_window_catalog_clean_state`** не удалялись. После прогона агента MinSizeRel оставлен открытым на **`linux_window_catalog_geom_z_manual_wslg`**.

<a id="window-geometry-contract--2026-11-06"></a>

## Window geometry contract — restore vs live client (2026-11-06) — PASS (headless, verified)

**Ветка:** **`feature/window-geometry-contract`** @ **`d13d7d6`**, base **`d590ee7`** (`origin/integration/unified-application`). **`feature/linux-window-catalog`** @ **`ae8ed7e`** — не вливалась. macOS / Android WC — не трогались.

- Persist: **`x_`/`y_`/`restore_client_width_`/`restore_client_height_`**. Live: **`UiWindow` client size**.
- **`ReportUiDesktopWindowFrameFromNativeEvent::updates_restore_geometry`**: user vs system; report alone не вызывает `ApplyRequestedFrameFromMirror`.
- Win32: interactive → restore+live; иначе только live client. Linux `OnLoad`: restore client для GTK size (без merge Linux WC presenter).

| Прогон | Результат |
| --- | --- |
| `apptraverse_ui_desktop_window_geometry_test` | **PASS** (Win64 Debug, Linux WSL Debug) |
| `apptraverse_application_contract_headless_test` | **PASS** (Win + Linux) |
| `apptraverse_window_catalog_model_test`, `apptraverse_minimal_app_headless_test` | **PASS** |
| `apptraverse_ui_control_materialization_test`, `apptraverse_presenter_graph_persistence_test` | **PASS** |
| Embedded export (build-time regenerate) | **PASS** |
| Linux WC smoke / Android WC | **NOT RUN** |

<a id="p4-wc-win-debug-embedded--2026-11-05"></a>

## P4 / WindowCatalog — Windows Debug embedded executable (post-`c9a608f`, 2026-11-05) — PASS (Win slice, verified)

**Ветка:** **`feature/windows-window-catalog-complete`** → merge в **`integration/unified-application`** после проверки. **iOS runtime:** см. §[P3.T3.I](#p3t3i--ios-minimalapp-prototype-2026-11-05) — **BLOCKED** (Simulator); не отменяется этим срезом.

### Причина первого timeout (установлено)

| Сценарий | Exe | Факт |
| --- | --- | --- |
| **`missing_root`** (subprocess, до fix) | `win32_window_catalog_demo_load_only.exe` | Пустой USER + embedded → штатный cold start из BACKING, GUI loop → **30s timeout** `:622`. **Не** «BACKING подставляет presenter при `Remove`». |
| **`unsupported_schema_window_catalog_root`** (subprocess) | тот же load_only | USER только S1, overlay **`Load(v0)` → kLoaded из BACKING** (`DiagnoseUnsupportedSchemaOverlayFallback`); subprocess не fatal. |
| **`missing_presenter_object`** и прочие **`Remove` → kRemoved** | load_only subprocess | **USER kRemoved, overlay kRemoved, id=3 не supply constant** — probe OK; subprocess **`fatal: LoadApplication failed`** без timeout. |

Убран искусственный **`UserProfilePresent`** в lifecycle. **`missing_root`**: USER-only probe (`kEmpty`), не subprocess embedded exe.

### Supply (единый артеfact)

- Одна CMake-команда → **`build/.../examples/surfaces_demo/generated_distilled/embedded_distilled_window_catalog_supply.h`** (`apptraverse_window_catalog_embedded_supply`): main, load_only, overlay-тест.
- Exporter: уникальный temp dir per output path (нет гонок `remove_all`).

### Продукт (без новой политики загрузки)

- Debug embedded на **`win32_window_catalog_demo`** + **`win32_window_catalog_demo_load_only`**; persist USER через embedded session (как MinimalApp).

### Acceptance (покрытие)

- **`RunRestartPersistenceAcceptance`**: Add/Open/native Close/reopen, identity, **два cold-load цикла**.
- **`RunDeleteAndColdReloadAcceptance`**: Delete chrome → proxy/Event, **состояние списка** и cold reload (соседняя строка, новая команда); **не** проверяет HWND identity открытых окон (это в restart/open-path acceptance).
- **`RunLanguageAndFocusAcceptance`**: EN→RU→EN **до** negative subprocess (не зависит от порядка negatives).
- Negatives: **`Remove`/tombstone** — overlay in-process probe + embedded load_only subprocess; **schema/unknown ClassId** — **`--user-only-load-probe`** (user-owned объекты только из USER `DirectoryDomainStorage`, supply-константы из embedded; не embedded exe).

### Прогоны (incremental Debug)

| Контур | 1-й FAIL / итог |
| --- | --- |
| acceptance после `e96c915` | **SEGFAULT** (overlay probe UAF: overlay outlived stack `backing`/`user`) → **исправлено** |
| acceptance до USER-only schema probes | **timeout** `unsupported_schema_window_catalog_root` на load_only |
| **Финальный** acceptance + §W2 (14) + overlay/model/headless + minimal export (post-`c9a608f`, merge `bcd4dcf`) | **14/14 PASS** (1-й прогон, без retry) |
| Точность acceptance tombstone ClassId (`CatalogAddButton`, `UiLocalizationString`) | Исправление **только теста** (не loader); включено в интеграционный merge |

### DEFERRED (без изменений)

- Несовместимые USER/schema и расширенное версионирование — **не** входят в этот срез; loader/overlay policy **не** менялись.

### Executable

- `build/win64-ninja-msvc-debug/examples/surfaces_demo/windows/win32_window_catalog_demo.exe`
- `…/win32_window_catalog_demo_load_only.exe` (`--state-dir`; schema/unknown ClassId negatives — `--user-only-load-probe`, не embedded subprocess).

### Ограничение (не блокер среза)

- Embedded subprocess **не** годится для проверки «несовместимый USER-слой без v0»: overlay fallback на BACKING v0 — задокументировано probe; контракт loader — отдельно от WindowCatalog app.

### USER-профиль — read-only аудит (MinSizeRel manual, 2026-11-05)

**Контекст executable / supply (не регенерировали supply для сравнения):**

| | Путь |
| --- | --- |
| Exe | `build/win64-ninja-msvc-minsizerel/examples/surfaces_demo/windows/MinSizeRel/win32_window_catalog_demo.exe` (~405 504 B) |
| Профиль | `…/MinSizeRel/surfaces_catalog_s0_proto_state` — **DefaultStateDir** (явный `--state-dir` не требовался) |
| Embedded BACKING, linked при сборке exe | `build/win64-ninja-msvc-minsizerel/examples/surfaces_demo/generated_distilled/embedded_distilled_window_catalog_supply.h` — `kEmbeddedGraph` **165** layers, `kSupplyConstantIds[] = {15, 16}` |

**Ограничение снимка «34 файла / 3 окна»:** тот же каталог профиля после ручных сессий **перезаписан** (агентская фиксация ~12:08: **34** layer-файла, **10** ObjId-подкаталогов, ~**300** B; аудит на HEAD того же пути: **302** файла, **89** ObjId-подкаталогов). Пофайловую таблицу для раннего снимка **восстановить нельзя** (копия не сохранялась); ниже — установленные факты и разбор наблюдения **34**.

**Формат USER (DirectoryDomainStorage):** только `ObjId/ClassId/version` (файлы); посторонних имён нет. Supply-constant prefab **15/16 в USER отсутствуют** (`user_dir/15`, `user_dir/16` — нет) — ожидаемо (BACKING-only).

**Почему 34 файла при ~3 окнах — не дефект плотности:** `SaveWindowCatalogPersistentSnapshot` пишет `WindowCatalog` root, `SavePresentedWindow(main)`, на каждый holder — `primary_text.Save()` + `SavePresentedWindow(window)` при loaded; каждое **AddWindow** (Clone prefab) создаёт **несколько** dynamic ObjId (holder, window, presenter, localized strings, Event/journal слои `3527371339`). **34 layer-файла** согласуются с **несколькими** ObjId-подкаталогами (~10) и мелкими v0-слоями (Node `2178182515`, Obj `3486612257`, …), а не с «одним файлом на окно».

**Аудит копии текущего профиля** (read-only, `%TEMP%\\wc_user_audit_copy`; полная таблица **302** строк локально, **не** в git): byte-сравнение с тем же `embedded_distilled_window_catalog_supply.h`:

| vs BACKING (302 слоя) | count |
| --- | ---: |
| byte-identical | 110 |
| USER-only-key (dynamic ObjId) | 176 |
| BACKING-override (длина/содержимое USER ≠ BACKING) | 16 |

**Overrides без нарушения:** root `1/3486612257/0` и `1/3951442439/0` — USER больше BACKING (catalog state + list/selection после Add); **не** классифицировано как дефект. **110 byte-identical** слоёв vs embedded BACKING: **причина записи в USER не установлена**; **нарушение не доказано** (отдельный audit `force_materialize` / оптимизация хранения **не** начинались).

**Состав USER (текущая копия):** dynamic holders/windows (~80 ObjId, ~265 слоёв); chrome enum ObjId 2–13 частично в USER (identical или малые overrides); **EventRecord** 18 слоёв на 18 ObjId; journal/base в USER как сериализованные Node/Event слои, **не** отдельный sidecar.

**Нарушений контракта USER/BACKING в аудите не установлено** (prefab 15/16 не в USER; «34 файла» объясняется моделью persist/Clone). **Пофайловый снимок на 34 layer-файла отсутствует** (см. ограничение выше).

### WindowCatalog model — удаление избыточных guard (2026-11-05)

**Scope:** `examples/surfaces_demo/common/window_catalog_model.cpp` only. **Не** исправление воспроизведённого сбоя — снятие дублирующих проверок там, где граф уже гарантирует `locale` и `list`/`domain`.

- **`MakeHolderTitleString`:** `language_index` через `catalog.locale->language_index()` (без `catalog.locale ? … : 0`).
- **`AddWindow`:** убраны `!list` / `list->domain == nullptr` + fatal/abort; тело без замены guard.

**Regression (incremental Debug, 1-й прогон):** WindowCatalog model / headless / embedded overlay / Win32 acceptance **PASS**; Windows §W2 **14/14 PASS**; WASM `wasm_window_catalog_browser_smoke.py` **PASS** (serve **8765**).

<a id="p3t3i--ios-minimalapp-prototype-2026-11-05"></a>

## P3.T3.I — iOS/iPadOS MinimalApp prototype (2026-11-05)

**Base:** `e94899255ab779df8d9d625b13b958d919eb64d5`. **Scope:** functional iOS/iPadOS MinimalApp prototype only; WindowCatalog not started.

### Platform boundary

- **C++:** shared `minimal_app_common`, `RunEmbeddedApplicationModelSession`, iOS presenter types (`IosDesktopWindowPresenter`, `IosPushButtonPresenter`, `IosListContainerPresenter`) registered for embedded export; library has no MinimalApp headers or fixed ObjIds.
- **Objective-C++:** `ATIosApplicationBridge` — lifecycle, immutable display snapshots, UI commands, `persist_model` for background USER checkpoint; `submitWindowFrame` is a no-op (mobile must not write desktop geometry).
- **Swift/SwiftUI:** `IosApplicationHost` — UIKit scene, main + Detail sheet, weak command sink disconnect on unload, `scenePhase` → `requestBackgroundSave`, list row identity via ObjId in snapshots.

### Toolchains and build trees

- **Host (embedded export):** `build/macos-x64-mp20-debug`, Ninja Debug x86_64, macOS SDK 14.2, MacPorts `clang++/clang-mp-20`, target `apptraverse_export_minimal_distilled_graph_ios`.
- **iOS sim app:** `build/ios-sim-x86_64-debug`, `-DCMAKE_SYSTEM_NAME=iOS`, iPhoneSimulator SDK (Xcode 15.2), deployment 17.0, MacPorts C++/ObjCXX + Xcode Swift (`x86_64-apple-ios17.0-simulator`), `APPTRAVERSE_IOS_HOST_EXPORTER` points at macOS exporter; target `apptraverse_minimal_app_ios.app`.
- **CMake guards:** macOS AppKit/Swift targets gated with `APPLE AND NOT iOS`; iOS bridge/Swift gated with `CMAKE_SYSTEM_NAME STREQUAL "iOS"`.
- **First compile FAIL (iOS bridge):** brace-initialized `std::atomic<bool>` members in an ObjC `@interface` extension; fixed with default construction + explicit init in `-init`.
- **First configure FAIL (Swift):** missing `CMAKE_Swift_COMPILER_TARGET` before `enable_language(Swift)` on iOS; fixed at root iOS block (mirrors surfaces demo subdirectory pattern).

### Embedded supply

- Generated header (iOS build tree): `build/ios-sim-x86_64-debug/examples/minimal_app/ios/generated_distilled/minimal_app_ios_composition/embedded_distilled_minimal_app_ios_supply.h`
- SHA-256 of generated supply header at build time: `91d0ed3daadac8e2bbdc9a6cacc163e7b0767d5f68740d41f3bafbb5dc4bd3e2` (distinct from macOS BACKING; separate iOS distill/export).

### Verification

- **iOS sim build / macOS host export:** `apptraverse_minimal_app_ios`, `apptraverse_export_minimal_distilled_graph_ios` — **PASS** (incremental Debug, 2026-11-05 re-check). Build/export **≠** runtime PASS.
- **macOS regressions (no Apple product code changes this slice):** prior PASS retained; macOS native two-process E2E **NOT RUN** on this slice.
- **Simulator host diagnosis (2026-11-05):** Xcode **15.2** (15C500b), `xcode-select` → `/Applications/Xcode.app/Contents/Developer`; sole installed runtime **iOS 17.2** (`com.apple.CoreSimulator.SimRuntime.iOS-17-2`, 21C62). Boot fails for **every** tested UDID with `SimLaunchHostService` / `launchd_sim`: *could not bind to session* → `NSPOSIXErrorDomain` **60** / `SimLaunchHostService.RequestError` **4** (~8 s timeout in `CoreSimulator.log`). Reproduced from Cursor agent shell, `launchctl asuser 501`, and **Terminal.app** (`BOOT_FAIL`); **not** an AppTraverse defect. Mitigations tried without erasing user devices: restart `CoreSimulatorService` / `SimLaunchHost.x86`, `open -a Simulator`, erase **new** test devices only.
- **Dedicated E2E simulators created (user devices/data untouched):** `AT-Minimal-E2E-iPhone-15PM` **`84D17537-5A72-4C23-B852-EBB92B429BEB`** (iPhone 15 Pro Max, iOS 17.2); `AT-Minimal-E2E-iPad10` **`24DDD487-E38E-4706-A4AB-A8D7AD741C5F`** (iPad 10th gen, iOS 17.2). Legacy **`EB9ED0F9-3C43-48A2-A18B-66C654FAAD4D`** not used after repeated boot failure.
- **iPhone Simulator runtime E2E** (empty USER → embedded UI → Row B → Apply → Detail open/close/reopen → background checkpoint → foreground command → terminate → cold relaunch): **BLOCKED** (no booted device; `run_simulator.sh` cannot install/launch).
- **iPad Simulator runtime E2E:** **BLOCKED** (same `launchd_sim` failure; iPad UDID ready, not booted).
- **NOT RUN:** Windows (`feature/windows-window-catalog-complete` in flight on origin), Android, Web, Linux backend runners; full CTest; clean/Release.

<a id="p3t3m--macos-minimalapp-embedded-user-2026-11-05--pass"></a>

## P3.T3.M — macOS MinimalApp: SwiftUI + embedded BACKING + USER (2026-11-05) — PASS

**Base:** `43c66bdb802a64e8f4b48cef95eee2551207b35f`. **Scope:** только macOS; iOS/iPadOS и WindowCatalog не начинались. **NEXT:** P3.T3.I — iOS/iPadOS отдельной итерацией.

### Граница платформы

- **C++:** `minimal_app_common`, `RunEmbeddedApplicationModelSession`, `ApplicationUiSession`, `ModelObjectProxy`, штатные Prepare/Normalize/Persist; зарегистрированные `MacDesktopWindowPresenter`, `MacPushButtonPresenter`, `MacListContainerPresenter` содержат только сериализуемые common-связи и делегируют lifecycle в `MacUiNativeViews`.
- **Objective-C++:** `ATMinimalNativeBridge.mm` реализует только boundary ownership, immutable display snapshots, publication apply и передачу Swift-команд существующим presenters/proxy. Model pointers в Swift не передаются.
- **Swift/AppKit/SwiftUI:** `NSApplication` lifecycle/main-thread dispatch, `NSWindow`/delegates, SwiftUI Window/List/Button и runtime-only `ObservableObject` adapters из UI mirror. Detail — отдельное non-modal окно; native close отправляет common close Event. Main/Cmd-Q выполняют stop → drain → persist → teardown без ожидания worker на main thread.

### Toolchain и сборка

- Отдельное дерево `build/macos-x64-mp20-debug`, Ninja, Debug, x86_64, deployment 13.0, macOS 14.2 SDK.
- C/C++/Objective-C/Objective-C++: `/opt/local/bin/clang{,++}-mp-20`; Swift: Xcode `swiftc`. Реальные compile/link commands проверены; финальный link выполнен MacPorts `clang++`, Swift и Objective-C++ targets разделены.
- Первый compile FAIL: в macOS graph ошибочно использовался отсутствующий helper `MakeMinimalPresenterFactory`; заменён на штатные typed factory lambdas.
- Повторный compile FAIL: MacPorts Objective-C++ оставлял Objective-C exceptions включёнными при `-fno-exceptions`; target получил `-fno-objc-exceptions`. Следующий incremental build PASS. Clean/Release не запускались.

### Проверки

- Focused CTest: `apptraverse_embedded_distilled_domain_test`, `apptraverse_overlay_user_backing_test`, `apptraverse_minimal_app_headless_test` — **3/3 PASS** (38.30 s).
- macOS exporter — **PASS**; generated embedded header до/после двух процессов: SHA-256 `cbdad4ebc8fc0f02018961ff41ec30f9bfdcec662ee0546c8755aa120d605f77` (BACKING неизменён).
- Первый native E2E обнаружил lifecycle FAIL: AppKit `terminateLater` вошёл во вложенное ожидание после завершения model worker. Исправлено без common-изменений: первый termination request отменяется, затем после worker drain/persist и UI teardown выполняется повторный terminate. Диагностический процесс был остановлен после подтверждённой записи USER.
- Чистый повтор на `/tmp/apptraverse-minimal-macos-e2e-20261105-b` — **PASS**: embedded startup (`Minimal App`, Apply/Open Detail, Row A/Row B) → native Row B → Apply (`Applied`, Apply disabled, selection cleared) → Detail open → native Detail close (Main остаётся) → native Main close и exit 0 → новый процесс (`LOADED_FROM_PROFILE`) восстанавливает `Applied`/disabled/cleared до кликов → native Row A → publication показывает Row A selected → native close и exit 0.
- Windows/Android/Web/Linux backend runners — **NOT RUN**: на текущем macOS host нет соответствующих штатных runner profiles; их последний зафиксированный результат на базовой unified-ветке сохранён. Известные matrix FAIL не запускались.

**Common changes for other branches:** нет. Изменены только macOS backend/example и platform-gated CMake.

### Corrective library-backend slice (post-`717c1c4`, 2026-11-05) — PASS

- Reusable AppKit/SwiftUI host, Window/List/Button adapters, display snapshots,
  and the generic Objective-C++ bridge now live in
  `include/apptraverse/platform/macos/` and `src/platform/macos/`. The library
  has no MinimalApp headers, fixed application ObjIds, or `examples/`
  dependency. `examples/minimal_app/macos/` now contains only the Swift entry,
  USER-directory selection, embedded model composition/lifecycle, and the
  MinimalApp close callback.
- Button/List adapters disconnect their weak command sink during presenter
  unload, so retained controls and bindings cannot reuse a presenter-address
  token. Window unload clears the delegate and hosting content before closing,
  breaking the `WindowAdapter → NSWindow → NSHostingView → WindowAdapter`
  ownership cycle.
- List snapshots carry each `UiListRow` ObjId as display identity. SwiftUI uses
  that identity for `ForEach`; selection Events continue to use the row's
  current index.
- **First compile FAIL:** extracted public `MacApplicationHost` protocol
  requirements were not declared `public`. Fixed and repeated incrementally.
- **Second compile FAIL:** row identity attempted an invalid `ObjId` conversion;
  importing a Swift module built with a temporary bridging-header PCH failed;
  and the example bridging header lacked the library include path. Fixed with
  `ObjId::id()`, a library Clang module map, and the target include path.
  The next incremental Debug build passed.
- **Focused checks:** macOS callback-detach/hosting-release/duplicate-label
  identity test plus application runtime Event/publication row-removal test;
  prior embedded domain, USER/BACKING overlay, and MinimalApp headless tests:
  **5/5 PASS**.
- **Native E2E:** fresh USER loaded embedded BACKING; native Row B and Apply
  produced `Applied`, disabled Apply, and cleared selection; Detail native
  open/close passed twice; Main close exited 0. A second process loaded the same
  USER as `LOADED_FROM_PROFILE`, restored `Applied` before input, then a new
  Detail open/close command passed and Main close exited 0. Generated BACKING
  header stayed SHA-256
  `838983bae2f6b8cb7c3152bea5a42cd6737f7a16cba4a5b340528b9cb7d55bd8`
  across both processes.
- **Exporter:** ran successfully. A separately distilled temporary export has
  different generated ObjIds, so cross-distillation byte equality is not
  claimed; the application-used generated BACKING remained unchanged.
- **NOT RUN:** Windows, Android, Web, and Linux backend runners are unavailable
  in this macOS-only worktree/session. This is not a full cross-backend
  regression claim. Release, clean build, full CTest, and known matrix failures
  were not run.
- **Correction commit:** `e3d9062fdd94a74818a2bca16fb3025e55b0bbb2`.
  Push status: the correction was pushed to `feature/macos-minimal-app` and
  fast-forward integrated into `integration/unified-application`; remote
  containment was verified.

<a id="p3t3ae--android-embedded-backing--user-2026-11-04--pass"></a>

## P3.T3.AE — Android MinimalApp: build-time embedded BACKING + USER (post-`3b9aa26`, 2026-11-04) — PASS; regression: 2 открытых pre-existing FAIL

**Base:** `3b9aa26`. **NEXT:** **P3.T3.M — macOS MinimalApp** (Apple не начинался).

### Поставка (build host)

- **Разделение presenters:** portable описание (`OnLoad/OnModelChanged/OnUnload`, регистрация ClassId) — `apptraverse_ui_android_presenters` (все платформы); JNI-реализация — `AndroidPresentationHost : AndroidUiNativeViews` в `apptraverse_ui_android` (только ANDROID). ClassId и сериализация presenters не дублируются.
- **Exporter:** `examples/minimal_app/android/host_export` → `apptraverse_export_minimal_distilled_graph_android` (штатный `BuildAndroidMinimalApplicationGraph` + `FinalizeDistilledGraph` + `SaveDistilledRoot` + `WriteEmbeddedDistilledSupplyHeader`).
- **NDK CMake:** вложенная host-сборка `build/android-host-export/<type>-<abi>` → `embedded_distilled_minimal_app_supply.h` перед `minimal_app_android`; пересборка от builder/schema sources. AVD/adb для экспорта не используется; Windows supply не копируется.
- Production `libapptraverse_minimal_app.so`: builder-символов **0**, есть `InstallEmbeddedDistilledBacking`; builder линкуется только в test fixture `.so`.

### Lifecycle и persist

- `RunMinimalApplicationModel(user_storage, …)` → `RunEmbeddedApplicationModelSession` (BACKING из supply + USER `DirectoryDomainStorage` через overlay); prepare → `PrepareMinimalApplicationForRun` (+ `NormalizeMinimalApplicationAfterEmbeddedUserLoad` при непустом USER). Bootstrap/builder в production не вызываются; `APPTRAVERSE_ENABLE_DISTILLATION` не отключался.
- **Оба persist пути** — `hooks.persist` и checkpoint `callbacks.persist_model` (model thread, сессия продолжается) — `PersistMinimalApplicationEmbeddedUserOverlay`; `PersistMinimalApplicationFromDomain` удалён. BACKING неизменяем.

### Тесты

- Probes (cold-load label, initial publication, early save, pending race, model-thread start, cold reload/apply) читают BACKING + USER; новый `RunAndroidEmbeddedBackingProbe`: USER содержит изменения, ни один supply constant id не имеет USER-слоя, пустой USER → состояние supply.
- **`EmbeddedColdStartInstrumentedTest`** (профиль `minimal_app_embedded_cold_start_e2e`): пустой USER → UI из supply (native marker `EMBEDDED_BACKING`) → Row B → Apply → Home + подтверждённый checkpoint → foreground → Row A → Home re-save → kill только `:demo` + подтверждение смерти → новый `:demo` с тем же USER (`LOADED_FROM_PROFILE`) → восстановленное состояние до кликов → новая команда.
- **Kill `:demo`:** прежний helper liveness (`test -d … && echo`) через `UiDevice.executeShellCommand` без shell всегда давал false — прежние проверки смерти были фиктивными; broadcast к non-exported `DemoProcessExitReceiver` от shell UID не доставлялся, `kill -9` от shell UID не действует. Теперь: liveness по `ps -A -o PID,NAME` (точный pid + `:demo`), kill — `Process.killProcess(pid)` из instrumentation (тот же UID); receiver и `MinimalAppTestControl` (main source set) удалены.

### Прогоны (incremental Debug)

| Контур | Результат |
| --- | --- |
| Android `connectedDebugAndroidTest`, **первый прогон** | **13/13 существующих PASS; новый `EmbeddedColdStart` FAIL** (liveness до kill) |
| Android, второй прогон (реальная liveness) | **10 PASS / 4 FAIL** — `EmbeddedColdStart`, `NewPidColdReload`, `SaveColdReload`×2: «demo pid still alive after kill» |
| Android, третий прогон (`Process.killProcess`) | **14/14 PASS** |
| Windows full incremental build | **FAIL** (первый FAIL контура): link `win32_surfaces_demo{,_v1,_load_only}.exe` — `surfaces_demo_win32_registration.cpp` / `_attach.cpp` не в CMake sources; `examples/surfaces_demo` срезом не менялся, на HEAD отдельно не проверялось; остальные цели собраны (`-k 0`) |
| Windows 14 + embedded export + Android exporter на host | **14/14 PASS**; exporter → `kEmbeddedGraph = {127991809u, …, 224}` |
| Linux materialization/model runtime/headless/historical base + Android exporter; L3 | **4/4 PASS**, exporter тот же граф, **L3 PASS** |
| WASM origin facet + MinimalApp / presenter lifecycle / WindowCatalog smoke | **PASS** |
| `event_version_matrix`, `surfaces_distilled_embedded_matrix` | не запускались; известные **FAIL** без изменений |

### Ограничения

- Один `Bootstrap…` символ остаётся мёртвым кодом в production `.so` (общий объект `minimal_app_persistence.cpp`), не вызывается.
- Вложенная host-сборка exporter использует host toolchain (на Windows — генератор SDK CMake по умолчанию, VS 2022); первая конфигурация длительная.
- Разные PID для `:demo` не требуются и не проверяются как критерий; критерий — подтверждённая смерть процесса и cold load того же USER.

<a id="p3t3o1pub3--historical-base-snapshot-scope-2026-11-04--pass"></a>

## P3.T3.O1pub3 — HistoricalBaseSnapshot: capture vs UI publication (post-`24e989e`, 2026-11-04) — PASS; regression: 2 открытых pre-existing FAIL

**Base:** `24e989e`. **NEXT:** без изменений.

### Красный контроль (на `24e989e`, до fix)

- **`CaptureBaseStateInto` на `LocalPersistent`:** parent → child, **100** committed Events на child; один `parent.CaptureBaseState()` сериализовал **100** `EventRecord` и **1** child base (`journal_entries_serialized` / `node_base_snapshots_serialized` ≠ 0). Новый **`TestCaptureParentOmitsChildNodeHistory`** в `historical_base_materialization_test` — **FAIL** на `24e989e`.
- **`OpCompactJournal` с unlimited retention:** журнал оставался длиной 1 без реального уплотнения; `expect_replay_item` не проверял `replay_ok` / value.

### Механизм (две политики)

| Назначение | Scope | `ShouldSaveVersion` | Node `base`/`journal` в обходе |
| --- | --- | --- | --- |
| UI publication | `MaterializedPublication` | не применяется (материализованный S1) | опускаются |
| Historical base capture | **`HistoricalBaseSnapshot`** (Æther patch `historical-base-snapshot-scope`, 17-й в chain) | **применяется** (нет «своего» S1 без `own_tag`) | опускаются (`OmitNodeHistoryForHistoricalBaseSnapshot` в `Node::Save`) |

`Node::CaptureBaseStateInto` переведён на **`HistoricalBaseSnapshot`**. Исправления **`5e935e7`** (UI S1) и **fresh replay** сохранены. CPM dir: **`8e2d`** win64/WASM/Android (Linux — отдельный reconfigure при прогоне).

### Тесты origin evolution (расширение)

- **fresh replay** + **compact S0-only:** `max_events=0` перед `CompactJournal`; проверки journal до/после, `base_value`, `replay_ok`, value/tag после replay, USER без Item/S1, BACKING неизменён; **`publish-ui`** (252 / 999) — PASS.

### Regression (incremental Debug)

| Контур | Результат |
| --- | --- |
| origin evolution (full matrix), historical base (+ child-history test), checkpoint rehydration + load-cycle, origin facet | **PASS** win64 |
| Windows 14 + embedded export | **14/14 PASS** |
| Linux materialization/model runtime/headless + evolution + historical base; L3 | **5/5 PASS** + **L3 PASS** |
| WASM origin facet + ×3 smoke (PIDs 28264 / 46492) | **PASS** |
| Android connectedDebugAndroidTest | **13/13 PASS** |
| `event_version_matrix`, `surfaces_distilled_embedded_matrix` | **FAIL** (без изменений) |

<a id="p3t3o1pub2--historical-base-capture-scope-2026-11-04--pass"></a>

## P3.T3.O1pub2 — historical base capture vs UI publication scope (post-`5e935e7`, 2026-11-04) — PASS; regression: 2 открытых pre-existing FAIL

**Base:** `5e935e7`. **NEXT:** без изменений.

### Регрессия от P3.T3.O1pub

`should-save-version-scope` отключил `ShouldSaveVersion` в **`MaterializedPublication`**, но **`Node::CaptureBaseStateInto`** использовал тот же scope при `InitializeRuntimeNode` → `CaptureBaseState()` на clone. На свежем USER (rev1 export → create-instances, без Event) в USER/base попадал **Item/S1** с `tag=0`, origin lookup при replay больше не подставлял prefab S1 → **`TryReplayFromBase()`** обнулял модель (**252→0**). Модель до replay: **252**, историческая base: **0**, `replay_ok=true`. На **`3dee9fc`** (до scope-fix) replay сохранял **252**.

### Красный тест (до fix CaptureBaseStateInto)

`origin_evolution_matrix`: отдельные сценарии **`fresh replay`** (export → create → cold → `replay-base`) и **`compact S0-only`** (set-value на A → `compact-journal` → cold → replay); проверка `model/base tag` до и после replay, отсутствие USER/Item/S1 без `own_tag`. На **`5e935e7`**: FAIL «fresh replay: a model tag after replay» (0 vs 252).

### Fix

**`Node::CaptureBaseStateInto`:** snapshot исторической base пишется с **`GraphSerializationScope::LocalPersistent`** (правила владения слоями / `ShouldSaveVersion`); UI publication по-прежнему через **`MaterializedPublication`** в `SerializeObjectGraphToBuffer`. Æther chain **без изменений** (`0084` / `596e`).

### Regression

| Контур | Результат |
| --- | --- |
| `apptraverse_origin_evolution_matrix` (publish-ui + fresh replay + compact S0) | **PASS** win64 / Linux |
| `apptraverse_historical_base_materialization_test`, checkpoint rehydration + load-cycle | **PASS** win64 |
| Windows headless subset (4) | **PASS** |
| `event_version_matrix`, `surfaces_distilled_embedded_matrix` | **FAIL** (без изменений) |

<a id="p3t3o1pub--origin-s1-ui-publication-2026-11-04--pass"></a>

## P3.T3.O1pub — origin-resolved S1 в UI publication (post-`3dee9fc`, 2026-11-04) — PASS; regression: 2 открытых pre-existing FAIL

**Base:** `3dee9fc`. **NEXT:** без изменений (Android embedded; ссылки внутри S1 — отдельная задача).

### Дефект

После cold load на rev1 экземпляр **A** без собственного USER/S1 читает `tag=252` из prefab (origin lookup); **B** с USER/S1 — `tag=999`. В модели значения верные, но **initial** и **structural** publication (`SerializeInitialPublication` / `SerializeStructuralNodePublication` → UI `RamDomain` без prefab BACKING) теряли S1 у **A**: `mirror.tag` становился **0** при `model.tag=252`. У **B** зеркало совпадало (**999**). Изменение S0 через `EvolutionSetValueEvent` + structural publication доходило до UI — отличало потерю S1 от полного обрыва публикации.

### Воспроизведение (до fix)

Расширены `tests/origin_evolution_fixture.cpp` (`--op publish-ui`) и `tests/origin_evolution_matrix.py` (после `set-tag` на B, до финального `set-value` на A): штатная initial publication обоих экземпляров, Event на S0, structural publication в то же UI-зеркало; проверка `mirror.tag == model.tag` и value на A. **FAIL:** «publish-ui: A initial mirror tag must match model» (252 vs 0).

### Fix

Patch **`aether-objects-should-save-version-scope.patch`** (заменяет **`should-save-version`** в CPM-списке): в `DomainGraph::Save` `ShouldSaveVersion` применяется только вне **`GraphSerializationScope::MaterializedPublication`**. Запись в USER (`LocalPersistent`) по-прежнему не создаёт A/Item/S1 без `own_tag`; publication сериализует материализованное состояние модели (в т.ч. S1, полученный через origin load). `Node::CaptureBaseStateInto` остаётся на том же scope — **PASS** `apptraverse_historical_base_materialization_test`. CPM source dir: `0084` win64/WASM/Android, `596e` Linux; `full patch chain OK (16 patches)`.

### Regression (incremental Debug)

| Контур | Результат |
| --- | --- |
| `apptraverse_origin_evolution_matrix` (включая publish-ui) | **PASS** win64 / Linux |
| `apptraverse_object_origin_facet_test`, checkpoint rehydration + load-cycle | **PASS** win64 |
| `apptraverse_historical_base_materialization_test` | **PASS** win64 |
| Windows 14 + headless/materialization | **14/14 PASS** |
| Linux L3 | **PASS** |
| WASM facet + ×3 smoke (serve PIDs 35368 / 3144, stop по PID) | **PASS** |
| Android `connectedDebugAndroidTest` | **13/13 PASS** |
| `apptraverse_event_version_matrix`, `apptraverse_surfaces_distilled_embedded_matrix` | **FAIL** (без изменений, не в срезе) |

<a id="p3t3o1--origin-objs0--prefab-layers-2026-11-04--pass"></a>

## P3.T3.O1 — origin в Obj/S0 + слои prefab по origin (post-`6e39ef2`, 2026-11-04) — PASS (срез); regression: 2 открытых pre-existing FAIL

**Base:** `6e39ef2`. **NEXT:** Android embedded (без изменений); Apple — не начинать.

### Два механизма (разделены)

- **Ссылка на origin — часть исходной схемы Obj/S0.** Patch **`origin-s0`**: `Obj` получает `AE_REFLECT(AE_MMBR(origin_))` (private lazy `ObjPtr<Obj>`), `SetOriginId`/`origin_id()`. Обычный обход базовых классов пишет `Obj/0` у каждого объекта (5 байт: ObjId + flags); у `Obj` нет Load/Save, поэтому `HasAnyVersioned*` для `Obj` и reflection-only потомков = false на всех toolchain. Patch **`obj-layer-factory`**: фабрика `Obj` загружает Obj/S0 до производных слоёв.
- **Новый слой S1 у prefab для существующих экземпляров.** Patch **`origin-layer-lookup`**: в штатном `DomainGraph::LoadVersion<T,V>` (`T != Obj`) при `kEmpty` у узла и валидном origin ≠ собственный id слой `T/V` читается по ObjId origin из того же storage. Один шаг; `kRemoved` не подменяется; P не материализуется, в USER ничего не пишется. Собственный USER-слой экземпляра приоритетен (читается первым). Сохранение S1 экземпляром — существующий `ShouldSaveVersion` (по образцу `PrefabWindow` user-owned S1).
- **Удалено:** служебный Obj/S1, auto Save hook, наследуемые/private facet Load/Save, patches `origin-facet`, `obj-facet-factory`, `clone-canonical-state`, `emscripten-domain-include`, `origin-facet-overload-set`. Новые: `origin-s0`, `obj-layer-factory`, `clone-canonical-scope`, `emscripten-domain-buffer-split`, `origin-layer-lookup`; context-only перегенерация `domain-registered-objects`, `clone-save-remap`, `clone-reference-visitor`, `nested-obj-archive-deseri`, `graph-load-instance-identity`. Цепочка на чистом clone pin `79283b6`: **full patch chain OK (16 patches)**. Новые CPM source dirs по имени списка: `75d2` (win64/WASM/Android), `5ec7` (Linux); cache вручную не правился.
- **Профили прежнего формата (Obj/S1)** не загружаются как origin; миграция/fallback не добавлялись (по заданию). Проверки — на новых тестовых профилях.

### Воспроизведение до исправления

- Unit `TestMissingLayerResolvedFromOrigin` без `origin-layer-lookup`: **FAIL** «A: own S0 kept, missing S1 resolved from origin».
- Сценарий `origin_evolution_matrix` без lookup: **FAIL** «inspect rev1: a.tag expected 252, got 0». С lookup — **PASS**.
- Первая версия lookup (`static_cast<Obj const&>(obj).origin_id()`) не компилировалась под **em++**: non-dependent выражение с неполным `Obj` в `domain.h` (MSVC/gcc откладывают проверку). Исправлено на `obj.Obj::origin_id()`; patch переименован (`origin-layer-resolution` → `origin-layer-lookup`), т.к. hash CPM следует именам.

### Сценарий эволюции (`apptraverse_origin_evolution_matrix`, два EXE: rev0 / rev1)

prefab P (S0) в BACKING → два экземпляра A, B (`CloneCanonicalRoot`, origin = P) → индивидуальное изменение S0 экземпляра → save → cold load; доставка rev1 (BACKING P с новым S1 tag=252) → оба экземпляра получают недостающий S1, прежние USER S0 сохранены; Event на B задаёт собственный S1 (priority над P); повторный cold load и новый Event. Штатный load, без ручной правки экземпляров; P не материализован, обычный save экземпляра не копирует слои P в USER. Регрессии reflection-only / EventFor / собственный versioned Load/Save, lazy origin — `apptraverse_object_origin_facet_test` (ожидаемые слои `Obj` {0}).

### Найдено в regression и исправлено в срезе

- **`apptraverse_window_schema_layers_roundtrip`** (win64) — первый прогон **FAIL** (embedded-first-install, rollback-to-a/b: «LoadRoot failed»). Причина: fixture писал embedded `Obj/0` и seed USER `Obj/0` пустыми байтами (валидно, пока у `Obj` не было полей). Исправлено: константа `kEmbeddedObjV0Layer` = байты штатного save null-origin `00 00 00 00 02` (сверено с файлами `Obj/0` из того же прогона). Повтор — **PASS**.
- Web `UserProfilePresent`: удалены новые ветки file-error; профиль = USER существует и не пуст.

### Incremental regression (без clean, без полного CTest; финальная цепочка `75d2`/`5ec7`)

| Контур | Результат |
| --- | --- |
| win64 origin/clone/evolution (facet, clone, disk roundtrip, disk matrix, evolution matrix) | **5/5 PASS** |
| Windows 14 + embedded export | **14/14 PASS** |
| win64 prefab/evolution/persistence (prefab ×10, application evolution, window schema, event downgrade ×2, lazy/surfaces distilled, checkpoint ×2, persistence ×4, directory/overlay storage, embedded distilled + origin 5) | **30/31**: FAIL `apptraverse_surfaces_distilled_embedded_matrix` («LABEL_ADD_EN expected Add, got »). **Pre-existing:** тот же вывод consumer, собранного в отдельном worktree на `6e39ef2` (исходная цепочка patches) |
| `apptraverse_event_version_matrix` win64 | **FAIL** (первый прогон и повтор на финальной цепочке): «known-prefix A compact-zero journal_len expected 2, got 3». **Pre-existing:** тот же FAIL на `6e39ef2` (worktree). Ожидание не менялось |
| win64 полный incremental build | все targets, кроме legacy `win32_surfaces_demo`, `_v1`, `_load_only` — LNK2019 `EnsureSurfacesDemoWin32Registration`/`AttachSurfacesDemoWin32SurfacePresenters` (определяющие `.cpp` не входят ни в один CMakeLists). **Pre-existing:** тот же LNK2019 на `6e39ef2` |
| Linux ctest (materialization, model runtime, minimal headless, Object Core 9 + origin evolution) | **13/13 PASS** |
| Linux L3 `linux_minimal_app_l3_smoke.py` | **PASS** |
| Linux `window_schema_layers` fixtures | **NOT BUILT** (pre-existing: `throw` при `-fno-exceptions`); вне Linux scope |
| WASM `apptraverse_object_origin_facet_test` | **PASS** |
| WASM ×3 (minimal, presenter lifecycle, catalog) | **PASS** ×3; серверы `serve_wasm.py` PID 22752 / 49684 запущены и остановлены по PID |
| Android `connectedDebugAndroidTest` (`C:/cpm-cache/aether-objects/75d2`) | **13/13 PASS** (первый прогон) |

**Открыто (вне среза):** event-version matrix FAIL и surfaces distilled label FAIL — pre-existing, не исправлялись. Обход для origin не многошаговый; новые слои Node/Event/Presenter (сейчас только S0) и отдельный источник слоёв для historical base не реализованы.

<a id="p3t3web6--web-embedded-backing--idbfs-user-2026-11-04--pass"></a>

## P3.T3.Web6 — Web MinimalApp embedded BACKING + IDBFS USER (post-`8f2b7e5`, 2026-11-04) — PASS (Web slice); regression: 1 открытый pre-existing FAIL

**Base:** `8f2b7e5`. **NEXT:** только **Android embedded** (BACKING + USER, по образцу Web6/Linux L3); Apple — не начинать.

### Web embedded (продукт)

- **Exporter (build-time):** `examples/minimal_app/web/minimal_app_export_web.cpp` → target **`apptraverse_export_minimal_distilled_graph_web`** (Emscripten, `-sENVIRONMENT=node -sNODERAWFS=1`, запуск через **`CROSSCOMPILING_EMULATOR`** = node); `BuildWebMinimalApplicationGraph` → `FinalizeDistilledGraph` → `SaveDistilledRoot` → **`WriteEmbeddedDistilledSupplyHeader`** (`embedded_distilled_minimal_app_supply.h`). `apptraverse_embed_distilled_supply` передаёт exporter как **имя target** (CMake подставляет emulator); прежняя форма `$<TARGET_FILE>` на Windows открывала `.js` через WScript (exit 0, без header).
- **`web_minimal_app`:** builder (`minimal_app_web_graph.cpp`) **не** входит в product sources; модель — **`RunEmbeddedApplicationModelSession`** (USER `/persistent/minimal_app_web_embedded_user` на IDBFS + `kEmbeddedGraph`). Пустой USER → load из BACKING (**`MINIMAL_APP_LOADED_FROM_EMBEDDED_BACKING`**), bootstrap **нет**; профиль есть → **`NormalizeMinimalApplicationAfterEmbeddedUserLoad`** (**`MINIMAL_APP_LOADED_FROM_PROFILE`**). Save и shutdown → **`PersistMinimalApplicationEmbeddedUserOverlay`**.
- Порядок host без изменений: IDBFS populate → model session → publication; Save → USER persist → `syncfs(false)` → confirmation; shutdown → persist → UI teardown → flush → join. Fallback builder / ID repair / Ensure / новые форматы — **нет**.

### Обнаруженный дефект: выбор versioned/reflection сериализации под Emscripten (aether origin facet)

- **Первый FAIL (до core-fix):** WASM minimal smoke — Save завис на «Saving…»; worker abort **`embedded USER overlay save requires compacted journal`**. Причина: `SetUiDesktopWindowFrameEvent` в USER сохранялся только как layer **v1** (5 байт, `origin_`), payload кадра терялся; `CanCompactStoredState` видел stored v1 > current 0.
- **Механизм:** `origin-facet.patch` объявлял в `ae::Obj` **protected** `Load/Save(ae::Version<1>, Dnv&)`. Reflection-only потомки (без своих `Load/Save`) наследуют это имя; `VersionSaveTrait`/`VersionLoadTrait` (`declval<U const&>().Save(Version<V>{}, …)` в SFINAE) на **em++ (clang 19)** (и, по probe, **NDK clang 21**) считали перегрузку доступной → `HasAnyVersioned*` = true, bounds [1,1] → сохранялся только `origin_` под ClassId потомка, reflection-путь не выполнялся; у `Obj` пропадал layer v0. На **MSVC** и **gcc** trait = false (штатный reflection-путь). Отдельный probe (protected inherited overload + тот же SFINAE): em++ `Report<1,1>`, MSVC `Report<0,0>` — **расхождение toolchain зафиксировано**, ошибкой компилятора **не** объявлялось (доказательства нет).
- **Fix (общий механизм):** новый patch **`cmake/patches/aether-objects-origin-facet-overload-set.patch`** (16-й элемент `APPTRAVERSE_AETHER_OBJECTS_PATCH_NAMES`, после `graph-load-instance-identity`): facet переименован в **private** `LoadOriginFacet`/`SaveOriginFacet` (не входит в overload set `Load/Save` потомков); `DomainGraph::LoadVersion`/`SaveVersion` для `Obj`/v1 вызывают facet явно; мёртвый gate `ShouldSaveOriginFacet` в versioned-итерации `Save<T>` удалён. `TryAppendOriginFacetLayer`/`ShouldSaveOriginFacet` без изменений. EventFor workaround / ручная сериализация / fallback — **нет**.
- **Patch chain:** вся цепочка применена к чистому clone pin **`79283b667cabf689da1059d618fe2a22abf51db4`** — `full patch chain OK`; CPM source dir сменился по hash списка patch (`d580` win64/WASM/Android, `1506` Linux), во всех деревьях `obj.h`/`domain.h` содержат `LoadOriginFacet` (CPM cache вручную не правился).

### Regression Object Core (Save → новый Domain → Load, с origin)

`tests/object_origin_facet_test.cpp`: reflection-only `Obj` потомок (`OriginFacetReflectOnly`), concrete **EventFor** (`SetUiDesktopWindowFrameEvent` 120/80/640/480), класс с собственными versioned `Load/Save` (`OriginFacetTargetV1`). Проверяются ClassId, `origin_id`, payload, prefab не материализуется, точный набор stored layers (`Obj` {0,1}; reflection/event layers v0; `Event` v0).

| Прогон | Результат |
| --- | --- |
| WASM **до** fix | **FAIL** ×3: reflect-only «payload lost after new Domain load»; EventFor «payload lost»; own-versioned «unexpected stored layer set» (`Obj/0` отсутствует). Существующий `TestConstPrefabDynamicInstanceRoundtrip` на WASM тоже FAIL («unexpected Obj S layers») — suite на WASM ранее не запускался |
| win64 **до** fix | PASS |
| WASM **после** fix | **PASS** (`apptraverse_object_origin_facet_test`) |
| win64 / Linux **после** fix | **PASS** |

### Web E2E (WASM ×3, первые прогоны после fix)

- **`wasm_minimal_app_browser_smoke.py` — PASS:** пустой USER → markers `["backing"]` → UI-команды → подтверждённый Save → cold reload → восстановленные данные (marker `profile`) → новые команды; итог markers `["backing","profile","profile","profile"]`; Detail (`Detail Panel`), shutdown, stale-callback, EM_VAL (`emval_pre_stop=11 emval_startup=6 emval_after_stop=0`) — PASS. Supply: frame event = `Obj/0` (empty), `Event/0` (empty), `Frame/0` (payload); в product WASM builder не вызывается.
- **lifecycle — PASS**, **catalog — PASS**.

### Incremental regression (без clean, без полного CTest)

| Контур | Результат (первый прогон) |
| --- | --- |
| Windows 14 + embedded export | **14/14 PASS** |
| Object Core win64 (facet, clone, origin disk roundtrip/matrix, model persistence, checkpoint load-cycle, directory/overlay storage, embedded distilled) | **9/9 PASS** |
| `apptraverse_event_version_matrix` win64 | **FAIL** (первый прогон и повтор): «known-prefix A compact-zero journal_len expected 2, got 3». **Не вызван** patch: тот же FAIL воспроизведён при временном исключении `origin-facet-overload-set` из списка (сборка fixtures на прежних sources `2915`), список затем восстановлен; uncommitted срез fixtures/`node.h` не трогает. **Открытый pre-existing FAIL**, не исправлялся в этом срезе |
| Linux ctest (materialization, model runtime, minimal headless + Object Core 9) | **12/12 PASS** |
| Linux L3 `linux_minimal_app_l3_smoke.py` | **PASS** |
| Linux `apptraverse_event_version_a/b` | **NOT BUILT** (pre-existing: `throw` при `-fno-exceptions` в `tests/event_version_fixture.cpp` под gcc); вне согласованного Linux scope |
| WASM ×3 | **PASS** (см. выше) |
| Android `connectedDebugAndroidTest` (patched `d580`) | **13/13 PASS** (прогон 6, после перезапуска host-процесса emulator); история ниже |

**Android — история прогонов (AVD `Aether_NDK_Smoke_x86_64`, API 34):**

- **Прогон 1 (первый):** **12/13 FAIL** — `SaveColdReloadInstrumentedTest.sameProcessResumeAfterBackgroundSaveAcceptsInput` (Apply/Row B не найдены после fresh launch); в logcat `com.android.systemui` FATAL `FinalizerWatchdogDaemon`.
- **Прогон 2 (повтор):** **11/13 FAIL** — `ListApplyInputInstrumentedTest`, `NewPidColdReloadInstrumentedTest`; ANR systemui/launcher/system. (Промежуточный запуск без тестов — lock каталога результатов, не считается прогоном.)
- **`adb reboot` (guest uptime был 2 дня, load ≈10) → прогон 3:** **11/13 FAIL** (те же два). ANR trace `com.apptraverse.minimal:demo`: main ждёт `RenderProxy::setStopped`, RenderThread — `qemu_pipe_read` в `glLinkProgram` (host GPU pipe); model thread idle; app abort/«requires compacted journal» — **нет**.
- **A/B до patch (временно без `origin-facet-overload-set`, sources `2915`):** **13/13 PASS**, ANR-файлов в окне прогона нет. Patched прогон 4: **10/13 FAIL**, прогон 5: **6/13 FAIL** (деградация).
- **Ручной запуск на том же emulator:** и patched, и pre-patch (`2915`) build — диалог «Minimal App isn't responding» (bootstrap→`INITIAL_RENDER_OK` 10 s / 13 s) → ANR **не** специфичен для patch.
- **NDK clang 21** на том же probe: `Report<1,1>` (как em++) — т.е. до patch Android тоже выбирал versioned путь для reflection-only классов; patch меняет фактическую сериализацию на Android (payload сохраняется).
- **Перезапуск host-процесса emulator** (`adb emu kill`, тот же `-avd … -no-snapshot-load`; qemu работал с 2026-11-02) → **прогон 6 patched: 13/13 PASS**, ANR-файлов за прогон нет.

**Pre-existing (вне среза):** полный WASM tree build падает (`surfaces_demo_evolution` `throw` при `-fno-exceptions`; lazy/prefab test exporters «failed to write» без NODERAWFS) — собирались только названные targets.

## WindowCatalog headless active-only localization EN→RU→EN (post-`760dfc0`, 2026-11-03) — PASS (tests/docs)

**Base:** `760dfc0`. Scope: **`tests/window_catalog_headless_test.cpp`** only; **production не менялся** (тест на текущем продукте **PASS**, дефект **не** выявлен — **не** оформлялся как исправление).

### Доказанный маршрут (E2E в `TestHeadlessCatalogChromeE2e`)

- **`BuildHeadlessWindowCatalogGraph`** + **`RunApplicationModelSession`** + **`ApplicationUiSession`** + **`ModelObjectProxy`** (model thread / UI mirror разделены).
- Переключение: **`UiPushButtonPresenter::NotifyClickedFromUi`** (language chrome) → proxy → **`CatalogLanguageButton::HandlePushButtonClicked`** → **`MaterializeCatalogStrings(next)`** + **`UiLocaleSettings::SubmitLanguageIndexFromUi(next)`** → incremental publication; **`before_incremental`** на model thread проверяет model **до** UI apply; UI mirror **не** меняется до **`ApplyStructural`** (проверка stale mirror между publication и apply).
- Подготовка: два child window (Add×2); второе закрыто **`UserCloseWindow`** через тот же proxy (штатный catalog API, как Win host callback); первое остаётся open с **`presentation_loaded`**.

### Проверки после каждого переключения (RU и обратно EN)

- Точные тексты chrome (**Add/Добавить**, Open, Delete, language switch **RU/EN**), main **Window Catalog/Каталог окон**, list rows **Window 1/2**, title open child.
- Active slot **loaded**, inactive **unloaded**; **ObjId** слотов EN/RU сохранены (**без** удержания inactive loaded и **без** диагностического Load неактивных до проверки loaded-состояний).
- Закрытое окно: ref valid, body **unloaded**, prefab **unloaded**; language toggle **не** reopen.
- EN return восстанавливает исходные EN-строки.

### Удалено из теста

- Явные **`strings[0].Load()` / `strings[1].Load()`** после language switch (маскировали active-only).

### Ограничения / вне среза

- Не дублирует **`TestLocalizationLazyInitialPublication`** (`ui_control_materialization_test` — publication/materialize контракт на изолированном `UiLocalizationString`).
- Без MinimalApp language UI, framework-обёрток, debug-полей, sidecar, счётчиков.
- **CreateFailed**, GTK ownership, loader/publication refactor, Apple — не включались.

### Регрессии (incremental Debug, tests/docs-only)

| Check | Result |
| --- | --- |
| **`apptraverse_window_catalog_headless_test`** | **PASS** |
| **`apptraverse_window_catalog_model_test`** | **PASS** |
| **`apptraverse_ui_control_materialization_test`** | **PASS** |

## Windows window title: valid-but-unloaded title/locale + acceptance Window 2 diagnostics (post-`03cd5ef`, 2026-11-03) — PASS

**Base:** `03cd5ef`. Scope: **`src/platform/windows/win_ui_presenters.cpp`**, **`tests/presenter_win_desktop_title_test.cpp`** (new), **`tests/CMakeLists.txt`**, **`tests/window_catalog_win32_acceptance_test.cpp`**.

### Title defect (воспроизведён до исправления)

- **`WinDesktopWindowPresenter::ResolveTitleText`** проверял `!title || !locale`; `ObjPtr::operator bool` = valid **и** loaded → для существующих, но выгруженных title/locale заголовок становился пустым.
- **`apptraverse_presenter_win_desktop_title_test`:** title (EN/RU `UiString`) и locale (`language_index` 1) сохранены в `RamDomainStorage` через `SaveRoot`, ссылки presenter сброшены **`Reset()`** (`is_valid`, `!is_loaded`, id сохранён). До исправления: **FAIL** `:142` — после `InitializePresenters` текст HWND пустой вместо RU. Отсутствующие title/locale → пустой заголовок (**PASS** и до, и после).
- **Исправление:** `!title.is_valid() || !locale.is_valid()` → `{}`; иначе штатное разыменование (`title->`, `locale->` загружают тело). Без fallback языка, Declare, Ensure, fatal/assert. Допустимое отсутствие сохранено (fixtures без title/locale и с одним locale). После: **PASS** (cold **`OnLoad`** и **`OnModelChanged`** после повторного `Reset()`).

### Acceptance `RunRestartPersistenceAcceptance` (бывший FAIL `:908`)

- Проверки Window 2 (после закрытия Window 1 и после закрытия reopened Window 1) заменены **`ExpectSameTopLevelWithTitle`**: при несовпадении печатает `outcome` = `original_destroyed` / `original_hidden` / `original_retitled` / `other_hwnd_found` / `original_not_enumerated`, оба HWND, их заголовки и все top-level окна pid; только данные Win32 самого теста.
- **Второй Open перед Close:** прежнее ожидание `FindTopLevelWithTitle(Window 1) == child1_reopened` было истинно ещё до команды. Сценарий теперь включает существующий test-only счётчик **`APPTRAVERSE_WC_FOCUS_APPLY_COUNT_FILE`** (только для первого процесса demo, переменная снимается после `StartDemo`): после reopen требуется count **1**, после второго Open — count **2**, затем identity Window 1. Без Sleep / увеличения timeout / повтора. Проверено: файл счётчика сценария = **2**.
- **Прежний FAIL `:908` в этом срезе не повторился; причина остаётся неизвестной.** Title-дефект причиной **не** объявляется (не воспроизведено).

### Build tree note

- Первый reconfigure после правки `tests/CMakeLists.txt` был запущен без MSVC env → `CMAKE_LINKER`/`CMAKE_AR` в кэше `win64-ninja-msvc-debug` указали на msys `ld.exe`/`ar.exe`. Восстановлены на `link.exe`/`lib.exe` toolset **14.44.35207** (`cmake -D… .` под `vcvars64`); без clean, далее incremental.

### Целевые Windows тесты

| Check | Result |
| --- | --- |
| `presenter_win_desktop_title_test` до исправления | **FAIL** `:142` (ожидаемое воспроизведение) |
| title, hwnd_persistence, frame_sync, frame_publication_e2e, interactive_drag_e2e, native_push_button, command_routing, window_catalog_win32_acceptance | **PASS** 8/8 |

### Регрессии (incremental Debug, один прогон, без повторов)

| Check | Result |
| --- | --- |
| Windows 14 ctest (`-j 4`) incl. minimal headless + embedded export | **PASS** 14/14 (1-й прогон) |
| Linux materialization + model runtime + minimal headless | **PASS** 3/3 |
| Linux **`linux_minimal_app`** L3 two-cycle | **PASS** |
| WASM minimal + lifecycle (**8941**), catalog (**8943**) | **PASS** ×3 |
| Android **`connectedDebugAndroidTest`** incl. **`PresenterLifecycleInstrumentedTest`** | **PASS** 13/13 |

## Window presenter lifecycle: OnLoad / Sync / OnUnload (post-`c08f03a`, 2026-11-03)

**Base:** `c08f03a`. Scope: **`src/platform/{linux/linux_ui_window_presenter,android/android_ui_window_presenter,web/web_ui_window_presenter,windows/win_ui_presenters}.cpp`**. Geometry, localization, serialization не менялись.

### Подтверждённые гарантии (`InitializeNewPresenters` / `ApplicationUiSession`)

- До **`OnLoad`** назначены **`presentation_host`** и **`model_proxy`**, presenter прошёл **`ReadyForPresentation`**; **`ComposePresentedWindow`** всегда задаёт **`window`**, **`title`**, **`locale`**; обход live-графа идёт только по загруженным рёбрам → закрытое окно (ref сброшен) не загружается и **`OnLoad`** не получает.
- **`OnModelChanged`** вызывается только для **`presentation_loaded`** presenter, т.е. после возврата из **`OnLoad`**. Сам **`presentation_loaded`** не доказывает наличие native окна (Windows **`CreateFailed`** возвращается молча).
- **`OnUnload`** вызывается **`UnloadPresenter`** только при **`presentation_loaded`** (assert там), ровно один раз, затем флаг сбрасывается.

### Удалённые проверки

- **Linux:** `if (!desktop) return` в **`OnLoad`**; `window_ == nullptr` в **`SyncTitleFromMirror`** / **`OnUnload`**; `delete_handler_ != 0`; повторные **`title.Load()`** / **`locale.Load()`** и fallback **`language_index = 0`**.
- **Android:** `view_refs_ == nullptr` в **`ClientMount`** / **`SyncTitleFromMirror`** / **`OnUnload`**; `host == nullptr`; `client_mount != nullptr` перед **`releaseClientMount`**; повторные **`Load()`** и fallback **`language_index = 0`**.
- **Web:** fatal `!desktop_window().is_valid()` в **`OnLoad`**; `dom_refs_ == nullptr` в **`OnUnload`** / **`NativeChildParentHandle`** / **`ClientMountHandle`**; `strings.size() > 0` и fallback **`language_index = 0`** в title sync.
- **Windows:** `if (!desktop) return` в **`WinDesktopWindowPresenter::OnLoad`**.
- Ни одна проверка не заменена fatal/assert/Ensure/fallback.

### Сохранённые условия (реальные состояния)

- **Отсутствующий title допустим:** `title.is_valid()` (Linux/Android/Web) → пустой заголовок.
- **Android root-only:** `host->activity == nullptr` в **`SyncTitleFromMirror`** — **`PresenterLifecycleInstrumentedTest`** работает без Activity.
- **Linux teardown:** `GTK_IS_WIDGET(window_)` перед **`gtk_widget_destroy`** (GTK мог уничтожить окно сам); `host != nullptr && callback` в **`OnDeleteEvent`**.
- **Web/Android env fatals:** main thread / host mount / JNI env в **`OnLoad`** — проверки окружения, не графа.
- **Windows:** **`ResolveTitleText`** `!title || !locale` и WM_CLOSE `host != nullptr` — frame-sync/drag/HWND-persistence и command-routing тесты строят presenter без title/locale и с `InitializePresenters(root, nullptr, …)` / прямым **`OnLoad()`**; `hwnd_ == nullptr` в **`OnModelChanged`** / **`OnUnload`** — путь **`CreateFailed`** в **`OnLoad`** возвращается молча и оставляет загруженный presenter без HWND (**найденный разрыв контракта, не менялся**: исправление требует fatal).

### Регрессии (incremental Debug, `apptraverse-wt-web5`, 2026-11-03)

| Check | Result |
| --- | --- |
| Windows 14 ctest (`-j 4`) incl. minimal headless + embedded export | 1-й прогон: **FAIL** 13/14 — **`window_catalog_win32_acceptance_test`** `:908` `FindTopLevelWithTitle(pid, L"Window 2") == child2` (после второго закрытия reopened Window 1; 3.59 s). Retry: отдельно **3×** **PASS**, полный набор `-j 4` **2×** **PASS** 14/14. **Причина первого FAIL не установлена** |
| Linux materialization + model runtime + minimal headless | **PASS** 3/3 |
| Linux **`linux_minimal_app`** L3 two-cycle | **PASS** |
| WASM minimal + lifecycle (**8931**) | **PASS** |
| WASM catalog (**8933**) | 1-й: **FAIL** — `OSError: [Errno 28] No space left on device` (диск C: 0 байт free, затем освободился до ~36 GB); retry **PASS** (binary не линкует `apptraverse_ui_web`) |
| Android **`connectedDebugAndroidTest`** incl. **`PresenterLifecycleInstrumentedTest`** | **PASS** 13/13 |

## WASM WindowCatalog initial smoke — «Loading…» investigation (post-`371c92e`, 2026-11-03)

**Base:** `371c92e`. Scope: **`tools/wasm_window_catalog_browser_smoke.py`** (+ PROGRESS); **production не менялся**.

### Smoke tooling (доказанный дефект smoke)

- При любом отказе скрипт печатает собранные **PAGEERROR**, **CONSOLE.ERROR**, **REQUESTFAILED**, маркеры **`IDBFS_LOADED` / `WASM_READY` / `INITIAL_RENDER_OK`** и снимок DOM **до** teardown; **PASS** — только после всех проверок.
- Раньше диагностика часто не попадала в stderr (ранний `return` / **`PAGEERROR`** после **`finally`**), что маскировало зависание на **«Loading…»**.
- **f9b9427 follow-up:** накопленные ошибки проверялись **до** **`screenshot()`** → **`PASS`** при **`console.error`** во время screenshot; исправлено: итоговая проверка **после** screenshot (probe **`SMOKE_LATE_ERROR_PROBE`** → exit **1**, без **PASS**).

### Исходное зависание (post-Prepare slice, serve **8903**)

- Симптом: timeout на **`#catalog-title` == "Window Catalog"**, HTML оставался **«Loading…»**; minimal/lifecycle WASM в том же прогоне **PASS**.
- **Prepare / catalog WASM не доказаны** причиной: **«Loading…»** — начальное состояние shell до **`WebCatalogWindowPresenter::OnLoad`**; без маркеров отказ мог быть на любой стадии до publication.

### Воспроизведение (incremental **`web_window_catalog_demo`**, dedicated **`serve_wasm.py`** **8911**, COOP/COEP подтверждены curl)

- Артефакты: **`build/wasm-ninja-debug/examples/surfaces_demo/web/web_window_catalog_demo.{html,js,wasm,worker.js}`** (mtime **2026-11-03** post-Prepare rebuild).
- **5×** catalog smoke с обновлённым скриптом: **PASS** (нет FAIL для сбора stderr).
- **Статус:** зависание **не воспроизведено**, **корневая причина не установлена**.

## WindowCatalog Prepare: drop redundant required-graph fatals (post-`e5543a0`, 2026-11-03) — PASS

**Base:** `e5543a0`. Scope: **`examples/surfaces_demo/common/window_catalog_prepare.cpp`**, **`tests/window_catalog_win32_acceptance_test.cpp`**.

### Production

- Removed Prepare fatals for list/locale/chrome presenters/buttons/localized texts and button→catalog casts; removed **`window_catalog_actions.h`** / **`noninteractive_crt.h`** includes.
- **Preserved order:** **`LoadPresentedWindow(main_window)`** → all **`Load()`** (list, locale, five presenters, four buttons, five texts) → **`MaterializeCatalogStrings`** → **`RefreshChromeButtonEnabledState`** → **`RestoreLoadedCatalogWindows`**.

### Tests

- Removed **`*_ref`** scenarios and ref-only helpers in list/locale, chrome presenter/button, localized text groups.
- **Kept `*_object`** cases + **`missing_root` / `missing_presenter_object` / schema / ClassId**; object negatives expect **`fatal: LoadApplication failed`** only (no Prepare-fatal alternate).

### Regressions (incremental Debug, `apptraverse-wt-web5`, 2026-11-03)

| Check | Result |
| --- | --- |
| Rebuilt **`win32_window_catalog_demo`**, **`win32_window_catalog_demo_load_only`**, acceptance | **PASS** |
| **`apptraverse_window_catalog_win32_acceptance_test`** (full) | **PASS** (~20s) |
| Windows §W2 (14 ctest) incl. catalog model/headless/overlay | **PASS** 14/14 |
| minimal headless + embedded export | **PASS** |
| Linux materialization + headless + L3 | **PASS** |
| WASM minimal + lifecycle (**8901**) | **PASS** |
| WASM catalog smoke (**8903**): 1st **FAIL** (`#catalog-title` stuck **Loading…**); **retry** (**8905**) **PASS** |
| Android **`connectedDebugAndroidTest`** | **PASS** 13/13 |

## Linux/Android ListContainer SyncFromMirror aligned with Web (post-`c9afeb3`, 2026-11-03) — PASS

**Base:** `c9afeb3`. Scope: **`SyncFromMirror` only** in `linux_ui_list_presenter.cpp` and `android_ui_list_presenter.cpp`.

### Change

- Removed silent return on missing **`list_box_`** / **`view_refs_`** / falsy **`container`**; removed redundant **`Load()`** and Linux fallback **`language_index = 0`** / conditional locale.
- Active language via **`window_presenter->locale->language_index()`**; row text via **`ResolvePresentedListRowPrimaryTexts`**; selection / **`applying_mirror_sync_`** / **`notify_list_selection_change`** / row rebuild order unchanged.

### Regressions (incremental Debug, `apptraverse-wt-web5`, 2026-11-03)

| Check | Result |
| --- | --- |
| **`apptraverse_minimal_app_headless_test`** + **`apptraverse_minimal_app_embedded_export_test`** (Win) | **PASS** (§W2) |
| Windows §W2 (14 ctest) | **PASS** 14/14 |
| Linux **`linux_ui_list_presenter.cpp`** rebuild + materialization + headless + L3 | **PASS** |
| WASM ×3 (`serve_wasm.py` **8891**, **8893**; lifecycle **`apptraverse_web_presenter_lifecycle_test.html`**) | **PASS** |
| Android **`connectedDebugAndroidTest`** (rebuilt **`android_ui_list_presenter.cpp`**) | **PASS** 13/13 |
| Full CTest / clean rebuild | **NOT RUN** (by instruction) |

## WindowCatalog Win32 acceptance: drop LoadPresentedWindow consumer fatals (post-`2d79624`, 2026-11-03) — PASS

**Base:** `2d79624`. Scope: **`tests/window_catalog_win32_acceptance_test.cpp`** only.

### Confirmed mismatch (pre-fix)

- Incremental binaries: **`build/win64-ninja-msvc-debug/tests/apptraverse_window_catalog_win32_acceptance_test.exe`** (same dir as test via `DemoExe`); load-only child **`…/examples/surfaces_demo/windows/win32_window_catalog_demo_load_only.exe`** (fallback `ExeDir/../examples/surfaces_demo/windows/`).
- **First failing scenario:** `RunRequiredMainWindowNegativeTests` → **`missing_main_window`** — expected `fatal: LoadPresentedWindow required window missing`; child stderr was **`ObjPtr` assert** (`Dereferencing invalid object`), exit ≠ 0. Production removed those fatals in **`76ed215`**; stale expectations on **`2d79624`**, not stale demo binaries.

### Test cleanup

- **Removed scenarios:** `missing_main_window`, `missing_presenter_ref` and helpers **`WriteProfileMissingMainWindow`**, **`AssertMainWindowRefAbsent`**, **`WriteProfileMissingPresenter`** (zero mandatory ref fixtures for cancelled consumer validation).
- **Kept loader negatives:** `missing_root`, **`missing_presenter_object`** (now **`ExpectFatalLoadOnly` → `fatal: LoadApplication failed`** only), chrome/list/locale/text/schema/ClassId cases unchanged except **`ExpectUnsupportedSchemaLoadOnly`** — no alternate **`LoadPresentedWindow required presenter missing`**; loader must fail with **`LoadApplication failed`**.

### Regressions (incremental Debug, `apptraverse-wt-web5`, 2026-11-03)

| Check | Result |
| --- | --- |
| **`apptraverse_window_catalog_win32_acceptance_test`** (full acceptance + all load-only negatives) | **PASS** |
| Windows §W2 (14 ctest) | **PASS** 14/14 |
| Other backends (Linux/Android/WASM @ `2d79624`) | **NOT RE-RUN** (tests/docs-only slice) |

## Linux/Android PushButton SyncFromMirror aligned with Win/Web (post-`76ed215`, 2026-11-03) — PASS

**Base:** `76ed215`. Scope: **`SyncFromMirror` only** in `linux_ui_push_button_presenter.cpp` and `android_ui_push_button_presenter.cpp`.

### Change

- Removed silent return when native **`button_`** / **`view_refs_`** missing; removed redundant **`Load()`** and label fatals.
- Mirror sync now matches Win/Web: **`window_presenter->locale->language_index()`**, **`button->label->Resolve(...)`**, enabled from mirror. **`OnLoad` / `OnUnload` / listeners / presentation order** unchanged.

### Call sites (unchanged)

- **`OnLoad`** after control creation; **`OnModelChanged`** after mirror updates — same as Windows/Web.

### Regressions (incremental Debug, `apptraverse-wt-web5`, 2026-11-03)

| Check | Result |
| --- | --- |
| **`apptraverse_minimal_app_headless_test`** + **`apptraverse_minimal_app_embedded_export_test`** (Win) | **PASS** |
| Windows §W2 (14 ctest) | **13/14 PASS** — **`apptraverse_window_catalog_win32_acceptance_test` FAIL** (1st + retry: load-only negative child, `ObjPtr` assert; not PushButton Sync) |
| Linux runtime + materialization + headless + L3 | **PASS** |
| WASM ×3 (`serve_wasm.py` **8881**, **8883**; lifecycle on **`apptraverse_web_presenter_lifecycle_test.html`**) | **PASS** |
| Android **`connectedDebugAndroidTest`** | **PASS** 13/13 |
| Full tree build / full CTest | **NOT RUN** (surfaces_demo link flake unrelated to slice) |

## UI LoadPresentedWindow + list row primary text: drop redundant graph validation (post-`2a25408`, 2026-11-03) — PASS

**Base:** `2a25408`. Scope: **`include/apptraverse/ui/ui_presented_window_lifecycle.h`**, **`src/ui/ui_list_container.cpp`**, **`tests/ui/ui_control_materialization_test.cpp`** only (not project-wide).

### Removed (no replacement validation)

- **`LoadPresentedWindow`:** fatal/abort when **`window`** or **`presenter`** missing after **`Load()`**.
- **`ResolvePresentedListRowPrimaryText`:** **`FatalPresentedListRowLabel`** and row / **`primary_text`** existence checks.

### Preserved materialization

- **`LoadPresentedWindow`:** **`window.Load()`** then **`window->presenter.Load()`**; **`SavePresentedWindow` / `UnloadPresentedWindow`** and compile-time **`static_assert`** unchanged.
- **`ResolvePresentedListRowPrimaryText`:** **`row.Load()`** then **`row->primary_text->Resolve(language_index)`**; one row → one text, list order unchanged.

### Tests

- **`TestPresentedListRowPrimaryTextsMatchRowIndices`:** kept; after first resolve, **`primary_text.Reset()`** on each row (valid-but-unloaded refs, reference-contract probe); **`ResolvePresentedListRowPrimaryTexts`** still returns **`Row A` / `Row B`** in order.
- Removed **`missing_primary_text_fatal`** child mode, **`TestPresentedListRowMissingPrimaryTextRefusesPartialList`**, and Win32 death-test helpers (obsolete re-validation contract). Serialization / localization load error tests unchanged.

### Regressions (incremental Debug, `apptraverse-wt-web5`, 2026-11-03)

| Check | Result |
| --- | --- |
| **`apptraverse_ui_control_materialization_test`** | **PASS** |
| **`apptraverse_minimal_app_headless_test`** + **`apptraverse_minimal_app_embedded_export_test`** (Win) | **PASS** |
| Windows 14 exe via `ctest -R` (§W2 scope) | **PASS** 14/14 |
| Linux runtime + materialization + headless (WSL `build/linux-x64-debug`) | **PASS** |
| Linux full **`linux_minimal_app_l3_smoke.py`** | **PASS** |
| WASM **`wasm_minimal_app_browser_smoke.py`** / catalog smoke (`serve_wasm.py` **8871**, **8873**) | **PASS** / **PASS** |
| WASM **`wasm_web_presenter_lifecycle_test.py`**: 1st run with **`web_minimal_app.html`** → **FAIL** (timeout; wrong page for lifecycle harness); **retry** with **`apptraverse_web_presenter_lifecycle_test.html`** (`serve_wasm.py` **8874**) → **PASS** |
| Android **`connectedDebugAndroidTest`** (13 tests) | **PASS** 13/13 |
| Full CTest / clean rebuild | **NOT RUN** (by instruction) |

## MinimalApp Prepare / LoadAuxiliary: drop redundant graph validation (post-`fe1b982`, 2026-11-03) — PASS

**Base:** `fe1b982`. Scope: `minimal_app_prepare.cpp`, `minimal_app_window_lifecycle.cpp`, headless test.

### Removed (no replacement validation)

- **`PrepareMinimalApplicationForRun`:** fatal/abort blocks for missing apply/row/presenters/opener, invalid/missing apply and opener **label** refs, missing **window_title** wiring, missing list **primary_text**, missing window presenter **locale**; redundant explicit **label.Load()** before materialization ( **`operator->`** on guaranteed refs already loads).
- **`MinimalApplication::LoadAuxiliaryPresentation`:** fatal/abort for missing auxiliary controls/presenters after Load.

### Preserved materialization

- **`LoadPresentedWindow(main_window)`**; Load of main apply/row/presenters/opener; **`ActiveLanguageIndex`** via presenter **locale**; **`MaterializeLanguage`** for main (and open Detail) labels/titles; **`SealAuxiliaryWindowClosed`** / Emscripten branch unchanged; auxiliary path still **`LoadAuxiliaryPresentation`** then aux **`MaterializeLanguage`**.

### Test

- **`TestMinimalAppPrepareUnloadedRequiredApplyLabelRef`:** distill/cold load, then **`apply_button->label.Reset()`** (reference-contract probe, not natural cold load); **`PrepareMinimalApplicationForRun`** materializes active EN, inactive RU stays unloaded, closed Detail stays unloaded.

### Regressions (incremental Debug, `apptraverse-wt-web5`, 2026-11-03)

| Check | Result |
| --- | --- |
| **`apptraverse_minimal_app_headless_test`** + **`apptraverse_minimal_app_embedded_export_test`** (Win) | **PASS** |
| Windows 14 exe via `ctest -R` | **PASS** 14/14 |
| Linux runtime + materialization + headless + full L3 | **PASS** |
| WASM ×3 (`serve_wasm.py` 8861, 8863) | **PASS** |
| Android **`connectedDebugAndroidTest`** (13 tests) | **PASS** 13/13 |
| Full CTest / clean rebuild | **NOT RUN** (by instruction) |

## MinimalApp Prepare: active locale only before publication (post-`6644667`, 2026-11-03) — PASS

**Base:** `6644667`. Scope: `minimal_app_prepare.cpp`, `minimal_app_headless_test.cpp`.

### Reproduction before the fix

**`TestMinimalAppPrepareMaterializesActiveLanguageOnly(0|1)`** (new): bilingual EN/RU `UiString` bodies saved with `kUnloadedByDefault`, active language set on shared `UiLocaleSettings` via `SetUiLanguageIndexEvent`, standard distill → cold load → **`PrepareMinimalApplicationForRun`**. With the previous **`MaterializeLabelTree`** (Load every `label->strings[]` entry), after Prepare **both** slots on apply/opener/row/title labels were loaded — **`CheckActiveLanguageSlots`** would fail on `!inactive.is_loaded()`. Single-language fixtures did not catch this.

### Fix

- **`PrepareMinimalApplicationForRun`:** replace per-label tree load with **`UiLocalizationString::MaterializeLanguage(language_index)`** where **`language_index`** comes from **`presenters.apply_button->window_presenter->locale->language_index()`** (no literal `0`, no fallback).
- Materialize main-window apply/opener/row primary texts and **main window presenter title** together; when **`auxiliary_presented_open`**, **`LoadAuxiliaryPresentation`** then the same index for auxiliary apply/rows/closer/**aux window title**. Closed Detail still **`SealAuxiliaryWindowClosed`** (non-Emscripten); no prefab load for localization.

### Headless contract checks (new)

- Active language **0** and **1**: after Prepare active slot loaded, inactive valid-but-unloaded, inactive UTF-8 preserved (probe via temporary Load/Reset), title **`Resolve(active)`** matches window presenter title; repeat Prepare does not load inactive; initial publication + UI mirror show active text; **`auxiliary.window`** stays unloaded when sealed.
- **`TestMinimalAppPrepareActiveLanguageOpenDetail`:** open Detail before Prepare with bilingual aux controls; active RU materialized on aux apply/title only.

### Regressions (incremental Debug, `apptraverse-wt-web5`, 2026-11-03)

| Check | Result |
| --- | --- |
| **`apptraverse_minimal_app_headless_test`** + **`apptraverse_minimal_app_embedded_export_test`** (Win) | **PASS** |
| Windows 14 exe via `ctest -R` (prior scope) | **PASS** 14/14 |
| Linux runtime + materialization + headless + full L3 | **PASS** |
| WASM ×3 (`serve_wasm.py` 8851, 8853) | **PASS** |
| Android **`connectedDebugAndroidTest`** (13 tests) | **PASS** 13/13 |
| Full CTest / clean rebuild | **NOT RUN** (by instruction) |

## MinimalApp Open/Close Detail owner wiring (post-`39b1528`, 2026-11-03) — PASS

**Base:** `39b1528`. Scope: `minimal_app_controls`, `minimal_application.h`, `minimal_app_graph_builder.cpp`, headless tests; embedded supplies regenerated at build via standard exporters (not committed; per-target `generated_distilled/`).

### Change

- **`MinimalAuxiliaryOpenerButton` / `MinimalAuxiliaryCloserButton`:** reflected field **`ae::ObjPtr<MinimalApplication> application`** (typed owner link). Set in **`BuildMinimalApplicationGraph`** before distillation (`auxiliary_opener->application = application`, `auxiliary.closer->application = application`). Persisted through normal S0 Load/Save/reflection; no **`Declare`/`Find(MinimalApplicationRootId)`**, silent return, Prepare repair, or fixed-ID lookup in handlers.
- **Handlers:** commit the same **`MinimalOpenAuxiliaryWindowEvent` / `MinimalCloseAuxiliaryWindowEvent`** on **`application`** directly. Open/Close still **presenter → proxy → model handler → Event → publication**.

### Headless checks (new / extended)

- **`CheckOpenCloseOwnerWiring`:** after distill/cold load (model domain, before Prepare) opener and closer **`application.id()`** match domain root; closer verified when the object is present in the domain (UI mirror after initial publication may omit sealed auxiliary nodes — opener wiring checked; closer when present). **`auxiliary.window`** stays unloaded (no Detail materialization).
- Existing **Open → Detail change → Close → USER save → cold reload → Open** path (**`TestMinimalAppAuxiliaryCloseEmbeddedUserShutdownColdLoad`**) extended with owner wiring after embedded USER load; state/title/Apply behavior unchanged.

### Regressions (incremental Debug, `apptraverse-wt-web5`, 2026-11-03)

| Check | Result |
| --- | --- |
| **`apptraverse_minimal_app_embedded_export_test`** + **`apptraverse_minimal_app_headless_test`** (Win) | **PASS** |
| Windows 14 exe via `ctest -R` (prior scope) | **PASS** 14/14 |
| Linux runtime + materialization + headless + full L3 | **PASS** |
| WASM ×3 (`serve_wasm.py` 8841, 8843) | **PASS** |
| Android **`connectedDebugAndroidTest`** (13 tests; 1st run **`SaveColdReloadInstrumentedTest.sameProcessResumeAfterBackgroundSaveAcceptsInput`** log timing flake, **retry PASS**) | **PASS** 13/13 |
| Full CTest / clean rebuild | **NOT RUN** (by instruction) |

## Android presenter teardown: listeners + View ownership (post-`efba74a`, 2026-11-03) — PASS

**Base:** `efba74a`. Scope: `src/platform/android/android_ui_{push_button,list,window}_presenter.cpp`, `AppTraverseAndroidUi.java`, Android instrumented test + test-only fixture library.

### Reproduction before the fix

**`PresenterLifecycleInstrumentedTest`** (real **`ApplicationUiSession`** over the MinimalApp graph, existing **`ModelObjectProxy`** with an enqueue counter, Java refs to the old button / rows / client mount held, **`RemountPresentation`** into the same `content_root`; presenters are still alive at remount, so old-View clicks are safe). Unfixed production code, one run:

- `content root children=2 expected 1` and `old client mount still attached` — window **`OnUnload`** freed the global ref but left `client_mount` in `content_root`;
- `old button: sent 1 command(s), expected 0`, `old button keeps OnClickListener` — button **`OnUnload`** detached the View but kept its listener;
- `old row B: sent 1 command(s), expected 0`, `old row A/B keeps OnClickListener` — list **`OnUnload`** kept row listeners.
- New button / row each sent exactly one command (no violation).

### Fix (UI thread; listeners → owned Views → JNI global refs)

- **`AppTraverseAndroidUi`:** `releasePushButton` (listener `null`, detach), `releaseListContainer` (rows via `clearListRows(list, 0)`, detach), `releaseClientMount` (detach the mount; host `content_root` untouched). **`clearListRows`** clears each removed row's listener before **`removeViewAt`**; kept rows keep their callbacks.
- Presenter **`OnUnload`** calls the matching release, then **`ClearGlobalRef`**. Children unload before the window via the existing descending presentation-load order. No live-pointer registry, alive flags, JNI checks or graph revalidation.

### Test (`PresenterLifecycleInstrumentedTest.presenterTeardownReleasesListenersAndViews`)

- Remount: old Views send nothing and have no listeners, old mount detached, exactly one mount, new button/rows one command each.
- List shrink through the normal path (model **`RemoveUiListRowEvent`** → root structural publication → **`ApplicationUiSession::ApplyStructural`**): removed row detached, no listener, sends nothing; remaining row keeps its View and callback.
- 3 more remount cycles; session end → `content_root` empty, every saved View without listeners; second create/destroy on the same root — one mount, controls work, empty after.
- Fixture/counter/JNI entries: `app/src/androidTest/cpp/presenter_lifecycle_fixture.cpp` + `PresenterLifecycleFixture.java`; CMake target **`apptraverse_minimal_app_test_fixture`** listed only in the **debug** build type targets (re-registers the `AppTraverseAndroidUi` natives for its own presenter copy). Release packaging not built/checked.

### Regressions (incremental Debug, `apptraverse-wt-web5`, 2026-11-03)

| Check | Result |
| --- | --- |
| Android **`connectedDebugAndroidTest`** (`Aether_NDK_Smoke_x86_64`, 12 existing + lifecycle) | **PASS** 13/13 |
| Windows 14 exe via `ctest -R` (headless, §W2, WindowCatalog model / headless / win32 / embedded overlay, …) | **PASS** 14/14 |
| Linux runtime + materialization + minimal headless | **PASS** |
| Linux full **`linux_minimal_app_l3_smoke.py`** | **PASS** |
| WASM minimal / presenter lifecycle / window catalog (`serve_wasm.py` 8831, 8833) | **PASS** ×3 |
| Full CTest / clean rebuild | **NOT RUN** (by instruction) |

## UiListContainer row removal by reference identity (post-`51844e0`, 2026-11-03) — PASS

**Base:** `51844e0` (`integration/unified-application`). Scope: **`src/ui/ui_list_container.cpp`** + existing tests.

### Reproduction before the fix (same tests, unfixed component)

- **`ui_control_materialization_test`** → `CHECK failed: RowIds(*list) == {row0,row2,row3}` (`:524`): removing a valid-but-unloaded row (`ObjPtr::Reset()`) left it in **`rows`** — the predicate used **`operator bool`** (valid **and** loaded).
- **`application_model_runtime_test`** → `CHECK failed: incremental_at_marker == 1` (`:1191`): a lone Remove command for a loaded row changed **`rows`** but did not call **`NoteMaterializedChange`** → no incremental publication.

### Fix

- **`UiListContainer::Apply(RemoveUiListRowEvent)`:** match on **`row.id() == event.row_id`** (reference identity; no Load / Declare / Domain lookup / Ensure); **`NoteMaterializedChange()`** only if **`rows`** actually shrank. Selection and **`RemoveUiListRowEvent`** (S0, `row_id`) unchanged; no selection auto-correction, no new presenter API.

### Tests

- **`TestListRemoveRowByReferenceIdentity`** (`ui_control_materialization_test`): 4 rows, `rows[1]`/`rows[3]` made unloaded via **`Reset()`** (reference-contract check, **not** a natural cold-load scenario). Unloaded row removed without materializing (`rows[2]` stays unloaded), loaded row removed, remaining order preserved, notify count +1 per actual removal, repeat Remove of a missing id → no notify; selection stays `-1`. UI mirror unchanged before publication, equal to model after **`ApplyStructuralPublication`**.
- **`TestListRowRemoveCommandPublishesIncremental`** (`application_model_runtime_test`): Remove is the only command (proxy → `Commit` → Event, no selection/title event); marker work queued after it observes exactly one incremental publication; UI list rows match model after **`ApplyStructuralPublication`**; proxy enqueue count stays 1 (no reverse model command from the mirror).

### Regressions (incremental Debug, `apptraverse-wt-web5`, 2026-11-03)

| Check | Result |
| --- | --- |
| Windows 14 exe via `ctest -R` (headless, §W2, WindowCatalog model / headless / win32 / embedded overlay, runtime, materialization, …) | **PASS** 14/14 |
| Linux runtime + materialization + minimal headless (WSL `build/linux-x64-debug`) | **PASS** |
| Linux full **`linux_minimal_app_l3_smoke.py`** | **PASS** (two-cycle embedded USER) |
| WASM **`wasm_minimal_app_browser_smoke.py`** / **`wasm_web_presenter_lifecycle_test.py`** (`serve_wasm.py` 8821) | **PASS** / **PASS** |
| WASM **`wasm_window_catalog_browser_smoke.py`** (8823) | **PASS** |
| Android **`connectedDebugAndroidTest`** (`Aether_NDK_Smoke_x86_64`, `buildCMakeDebug` executed) | **PASS** 12/12 |
| Full CTest / clean rebuild | **NOT RUN** (by instruction) |

## Model runtime notifier lifetime + live graph walk (post-`8e1953c`, 2026-11-03) — PASS

**Lease base:** `8e1953c075ea2c511ea6d644527f2cbb6397dc9c`. Earlier partial commits: `a23ae44` (shutdown/clear narrative did not match code), `453a413` (double inheritance walk, wrong WASM diagnosis).

### Cause

- **Live walk, inheritance:** concrete-class dispatch reflected the embedded **`base_`** as plain **`ae::Obj&`** (no edges) → derived fields (e.g. **`MinimalAppList::window_title`**) were visited, base **`UiListContainer::rows`** skipped. `453a413` then walked the base twice (reflected **`AE_REF_BASE`** + explicit **`T::Base`** recursion).
- **Live walk, unloaded edges:** **`CollectReachableObjectsFromLiveFields`** followed every valid edge via **`Domain::Find(edge.id())`**. **`ObjPtr::Reset()`** drops the cached object but keeps the ObjId, so an object still held by another owner (structural keepalive, external holder) stayed «live».
- **Live walk, order:** stack DFS pushed edges in field order and popped them reversed → presenters activated in reverse declaration order.
- **Notifiers:** bind at **`ApplicationModelRuntime::Run`** start did not match the clear at stop; nodes that got a notifier mid-session (**`CopyMaterializedChangeNotifierFrom`**) could outlive **`Run`** with **`PendingDirtyNodes*`** of a destroyed context.

### WASM MinimalApp FAIL (root cause)

- Served files checked: HTML/JS/WASM/worker from **`tools/serve_wasm.py`** byte-identical (SHA-256) to `build/wasm-ninja-debug/examples/minimal_app/web`.
- **Title is not the failure:** `data-apptraverse-window-title="Minimal App"` is present on the window root after load. The `453a413` note («title never set») came from a probe that queried a descendant element — wrong.
- **First failure:** `run_dual_window_lifecycle` → Detail **closer** click → wait for aux list DOM removal times out. Probe: after close both windows and controls `7202/7201/7203` stay in DOM, no console error.
- **First loss on the path:** model is correct (**`SealAuxiliaryWindowClosed`**: `auxiliary_presented_open=false`, aux refs **`Reset()`**), publication/mirror apply correct; **`UpdatePresentersAfterStructuralPublication`** computes the live presenter set with **`CollectLiveReachableObjects`**, which resolved the reset aux refs by ObjId while **`StructuralPresentationKeepalive`** still held those objects → closed-window presenters counted live → never unloaded.
- After that fix the next failure: cold reload with both windows open mounted **Detail before main** (`.first` window at 280,120, expected 120,80) — reverse DFS order above.

### Fix (common)

- **`graph_walk.h`:** one mechanism — **`ForEachLiveGraphEdgeFieldsOnType`** recurses through the base chain only via reflected **`AE_REF_BASE`**; the **`Node`** layer (historical **`base`/`journal`**) is skipped; explicit **`WalkLiveGraphEdgeHierarchy`** removed.
- **`CollectReachableObjectsFromLiveFields`:** an edge is live only if the reference holds its object (**`is_loaded()` → `cached()`**), no **`Domain::Find`** by id, no Load; preorder in field declaration order.
- **`PendingDirtyNodes`:** subscription registry (**`Subscribe` / `OnNotifierCopiedTo` / `ClearAllSubscribedNotifiers`**) — stop clears every subscribed instance, no graph walk.
- **`ModelRuntime`:** **`notifier_subscribers_`** filled by **`AttachNode`**, **`BindAllModelNodeNotifiers`** and **`TrackMaterializedChangeNotifierCopy`**; destructor clears by registry. **`CollectReachableNodeRefs`** unchanged for save-finalize.

### Tests

- **`application_model_runtime_test`:**
  - **`TestLiveGraphWalkTraversesBaseClassRowsField`:** row reachable only via base **`rows`**; edge counts per object (**base `rows[0]` + derived `pinned_row` → 2 edges to one row**, list total 2, root total 2 — Node `base`/`journal` not emitted); lazy row unloaded; **`Reset()`** ref to an object still in Domain is not live.
  - **`TestRuntimeNotifierOrphanHeldAfterDetachFromLiveAndHistory`:** link removed (`ptr{}`, not `Reset()`); on the model thread before stop child absent from live **and** persistent walks while an external owner holds it; after stop no notifier, child Event OK.
  - Wait predicates (`initial_ready`, `session_linked`, `detached`, coalesce flags) written under the same mutex as the **`condition_variable`** wait.
  - **`RuntimeChildBumpEvent`** for **`RuntimeTestChild`**; original / detached / session-added child cases retained.
- Negative control for the edge-count check against the double walk: **NOT RUN** (expected 2 instead of 1 visit by construction).

### Regressions (incremental, `apptraverse-wt-web5`, 2026-11-03)

| Check | Result |
| --- | --- |
| **`apptraverse_application_model_runtime_test`** + **`apptraverse_minimal_app_headless_test`** (Win) | **PASS** |
| Same two (Linux, WSL Ubuntu `build/linux-x64-debug`) | **PASS** |
| Windows 14 exe via `ctest -R` (§W2: command routing, native push button, application host, minimal + catalog win32 acceptance; UI session; WindowCatalog model / headless / embedded overlay; minimal embedded export; overlay user backing; ui control materialization) | **PASS** 14/14 |
| Linux full **`linux_minimal_app_l3_smoke.py`** | **PASS** (two-cycle embedded USER) |
| WASM **`wasm_minimal_app_browser_smoke.py`** (`serve_wasm.py` 8811) | **PASS** |
| WASM **`wasm_web_presenter_lifecycle_test.py`** (8811) | **PASS** |
| WASM **`wasm_window_catalog_browser_smoke.py`** (8813) | **PASS** |
| Android **`connectedDebugAndroidTest`** (`Aether_NDK_Smoke_x86_64`, native `buildCMakeDebug` executed) | **PASS** 12/12 |
| Full CTest / clean rebuild | **NOT RUN** (by instruction) |

## MinimalApp embedded USER finalize ownership (post-`9f322b2`, 2026-11-03) — PASS

**Lease base:** `1acb0b8451d2512ca68e4461d01b161668f21e6c` (fast-forward from `9f322b2`, docs-only).

### Root cause (first wrong layer)

- **Win32 acceptance `LoadApplication failed id=127991809`:** cycle 1 (resize only, Detail not opened) USER held main window **`127991810`** with **uncompacted journal** (2 Events, one Event layer absent) → probe load failed after root.
- **4e1448e** switched finalize to **`CollectLiveReachableNodes`**: its walk calls `ForEachGraphEdgeObjPtrOn(ae::Obj&)` → **static** reflection of `ae::Obj` only; without a registered `LiveGraphEdgeAppender` (MinimalApp has none) it returns **root only** (headless probe: reachable=24, live=1). Finalize compacted only root; every other Node was saved with journal. Linux L3 did not exercise that load path.
- **Original Linux SIGSEGV (79b65cb):** finalize iterated raw `Node*` from **`CollectReachableNodes`**. After Detail close, aux Nodes (`127991818/820/822/824/827`) are owned only by root's historical **base/journal**; compacting root first released them (probe: alive=0 after root compact) → `CompactJournal` on freed Nodes (`domain_storage()` garbage). **Ownership defect in the collection contract**, not "invalid backing".
- **9f322b2** masked the consequences: session flag + `EnsureMinimalApplicationAuxiliaryLazyRefs` (Declare by known ID) + separate sealed aux flush + aux field filter; its headless test used plain `DirectoryDomainStorage` (no BACKING), unlike the real embedded topology.

### Fix (common layer)

- **`CollectReachableNodeRefs`** (`object_serialization`): same Node set as `CollectReachableNodes`, entries hold `ae::Ptr` so Nodes survive owner compaction.
- MinimalApp finalize uses it; **removed** 9f322b2 lazy-ref rebind, session flag, aux flush and filter (`examples/minimal_app` = 4e1448e except finalize collection).
- Headless Detail test now uses **BACKING (distilled RAM) + USER overlay**; checks cold-loaded root / main window / aux window / aux list / aux title journals empty and Detail title «Applied» after reopen. Negative control (4e1448e live collect) → **FAIL** at `main_window->journal.empty()`.

### Regressions (incremental, `apptraverse-wt-web5`)

| Check | Result |
| --- | --- |
| **`apptraverse_minimal_app_headless_test`** (Win + Linux) | **PASS** |
| Windows §W2: 10 exe incl. **`apptraverse_minimal_app_win32_acceptance_test`** + WindowCatalog overlay | **PASS** |
| Linux full **`linux_minimal_app_l3_smoke.py`** (WSL Ubuntu) | **PASS** |
| WASM ×3 (minimal 8766 / lifecycle 8770 / catalog 8786) | **PASS** |
| Android **12/12** | **PASS** |
| Full CTest | **NOT RUN** (by instruction) |

**NEXT:** `CollectLiveReachableNodes` / `CollectReachableObjectsFromLiveFields` still walk static `ae::Obj` reflection (only appenders see derived edges); remaining callers (`ClearLiveReachableNodesMaterializedChangeNotifier`, WindowCatalog) should be audited against the persistent walk.

## MinimalApp Detail close + embedded USER shutdown (post-`4e1448e`, 2026-11-03) — superseded

**Lease base:** `4e1448e17887ffe7e3292516b923b3a6ab413fd1`. Ownership analysis and fix in this section are **superseded** by «embedded USER finalize ownership» above.

### Lifetime / ownership (why backing looked «invalid»)

- **`SealAuxiliaryWindowClosed`:** `UnloadPresentedWindow` сбрасывает lazy **window** ref; **`Reset()`** на aux controls/presenters снимает runtime handles, **объекты остаются в `Domain`** (stable ObjIds из graph builder).
- **`CollectReachableNodes` (scratch):** `SaveObjectGraphToScratch` обходил **все** id, ещё связанные с root через сериализацию, включая **sealed** aux subgraph → **`CompactJournal`** на узлах с **unload** presenter/window → **`RamDomainStorage::Load`** с битым **`this`** (SIGSEGV @ Linux L3 shutdown).
- **`CollectLiveReachableNodes` alone:** после seal refs на `auxiliary.*` часто **invalid** — finalize не трогал aux, но **`SaveRoot`** без rebind **не писал** materialized aux state в USER → cold load терял «Applied» на Detail.
- **Fix (layer):** (1) finalize = live reach **минус** aux field subgraph when **`!auxiliary_presented_open`**; (2) **`EnsureMinimalApplicationAuxiliaryLazyRefs`** + **`SaveSealedAuxiliarySubgraphForPersist`** when **`auxiliary_subgraph_materialized_in_session`** (Detail был открыт); (3) session flag **не** в Save/Load bytes.

### Headless: close → USER save → cold load → reopen Detail

**`TestMinimalAppAuxiliaryCloseEmbeddedUserShutdownColdLoad`** in **`minimal_app_headless_test`:** presenter open → aux/main Apply → native close → **`PersistMinimalApplicationEmbeddedUserOverlay`** → **`LoadApplication` + `Normalize…`** → reopen → **main/applied title «Applied»**, Detail title **«Applied»**, aux list selection **-1**, apply disabled — **PASS**.

### Regressions (incremental, `apptraverse-wt-web5`)

| Check | Result |
| --- | --- |
| Linux full **`linux_minimal_app_l3_smoke.py`** | **PASS** |
| **`apptraverse_minimal_app_headless_test`** (incl. new Detail test) | **PASS** |
| §W2 subset (7 exe, без minimal win32 acceptance) | **PASS** |
| **`apptraverse_minimal_app_win32_acceptance_test`** | **FAIL** — `fatal: LoadApplication failed id=127991809` (embedded two-cycle; 1-й прогон post-change; повтор не гонялся) |
| WindowCatalog overlay + headless | **PASS** |
| WASM ×3 (8766 / 8770 / 8785) | **PASS** |
| Android **12/12** | **PASS** |

**NEXT:** разобрать **`LoadApplication` 127991809** в win32 embedded acceptance (USER/BACKING merge vs aux persist flush); повтор acceptance после диагностики.

## MinimalApp Linux L3 detail close SIGSEGV (post-`96c98a6`, 2026-11-03) — PASS

**Lease base:** `96c98a61218c0b94ea98731115a5998ce97df216`.

### Symptom / stack (WSL Ubuntu, GDB)

`tools/linux_minimal_app_l3_smoke.py` — **`l3_detail_close exit -11`** (SIGSEGV) after **`MINIMAL_APP_L3_DETAIL_NATIVE_CLOSE_OK`**, during shutdown persist (not during native close).

**First fault (model thread):** `PersistMinimalApplicationEmbeddedUserOverlay` → `FinalizeMinimalApplicationNodesForEmbeddedUserOverlay` → `Node::CompactJournal` → `RamDomainStorage::Load` with invalid **`this`** on backing storage (corrupt map / use-after-teardown on detached auxiliary subgraph still visited by finalize).

**Trigger:** auxiliary open/close in smoke preamble, then embedded USER finalize at exit.

### Fix (product)

1. **`FinalizeMinimalApplicationNodesForEmbeddedUserOverlay`:** **`CollectLiveReachableNodes`** instead of **`CollectReachableNodes`** — skip sealed/detached auxiliary nodes still visible to scratch-graph collect after **`SealAuxiliaryWindowClosed`**.
2. **Publication lifecycle:** UI hosts apply structural from **`TakePublishedCopyForUiApply`** (hold **`in_ui`** through apply); **`NotifyPublicationConsumed`** → **`ReleaseConsumer`**; model **`Run`** waits on **`stop_`** until **`!is_publication_busy()`**.
3. **Auxiliary mid-session:** removed redundant **`Save()`** from open/close impl ( **`NoteMaterializedChange`** only).
4. **Android join (probes):** **`AbandonUnconsumedPublicationsForTeardown`** when **`!ui_session_`** so empty-bridge instrumentation can **`JoinModelThreadForRelease`** without JNI UI.

### Verification (incremental Debug, `apptraverse-wt-web5`)

| Check | Result |
| --- | --- |
| **Linux** full `linux_minimal_app_l3_smoke.py` (WSL **Ubuntu**, `linux-x64-debug`) | **PASS** — detail close + two embedded USER cold-load cycles |
| **§W2 + Headless + WindowCatalog overlay** (9 exe) | **PASS** |
| **WASM ×3** (8766 / 8770 / 8785) | **PASS** |
| **Android** `connectedDebugAndroidTest` **12/12** (`Aether_NDK_Smoke_x86_64`) | **PASS** (1-й прогон после Android join fix) |

**NEXT:** headless fixture — auxiliary close + embedded USER shutdown finalize (parity with Linux L3 preamble).

## MinimalApp embedded USER overlay — verification (post-`79b65cb`, 2026-11-03) — PASS

**Product SHA:** `79b65cb9d5d9aa8f9cdfc2297d35dbe3694525ea` (`integration/unified-application`).

### Root cause (Win cold load, save-side bug)

Embedded USER save вызывал **`FinalizeUiNodeState`** (UI publication): снимал **`Node::base`** без **`CaptureBaseStateInto`**. **`SaveRoot`** записывал materialized слои с неконсистентной base-цепочкой; **`LoadApplication` / `LoadRoot`** падал **до normalize**.

**ClassId `2178182515`:** **`apptraverse::Node`** — `Node::kClassId` из **`APPTRAVERSE_OBJECT(Node, …)`** (`crc32::from_literal("apptraverse::Node")` в `object_macros.h`; тот же id в embedded supply headers).

**Первый отказ при Win cold load:** слой **`Node` (`2178182515`) version=1** — loader не находит historical Node v1 в USER/BACKING overlay (типично на **`MinimalApplicationRootId` `127991809`** или base id из wire после save); v0 часто есть, v1 — нет → **`Unable to find object … version=1`**, затем **`fatal: LoadApplication failed`**. Не первичный баг **`DirectoryDomainStorage`**.

**Fix (`minimal_app_persistence.cpp`, `79b65cb`):** **`CompactJournal`** + временная **`JournalRetentionPolicy{.max_events = 0}`** (иначе default unlimited → compact no-op); abort если journal остался; load без изменений (**`Normalize…` → `FinalizeDistilledGraph`**).

### Journal / replay (embedded USER overlay)

**`CompactJournal` с `max_events = 0`** перед USER save **сворачивает journal в base/materialized слои и не пишет Events на диск** — **replay сохранённой event-истории из USER после cold load не поддерживается** (by design для этого path). После reload: materialized state + **новые** Events до следующего shutdown compact. **Не** заявлять полную journal replay semantics на embedded USER overlay.

**Канон (отдельно, без расширения scope):** audit § «Events / journal после cold restart» согласован с materialized-only USER; полный **historical journal replay** как у directory checkpoint — **out of scope** для embedded overlay save.

### Cold-load E2E

| Platform | Result | Notes |
| --- | --- | --- |
| **Win** `apptraverse_minimal_app_win32_acceptance_test` | **PASS** @ `79b65cb` | frame → list B+Apply → `load_only` + list A + commit probe |
| **Linux** L3 two-cycle cold | **PASS** | `wsl -d Ubuntu`; incremental `linux-x64-debug` rebuild `linux_minimal_app`; phases **`l3_save` → `l3_cold_verify` → `l3_cycle2_select_row_b` → `l3_cycle3_cold_select_row_a`** (markers OK, exit 0). **`tools/linux_minimal_app_l3_smoke.py` целиком:** **FAIL** `l3_detail_close exit -11` (SIGSEGV после `MINIMAL_APP_L3_DETAIL_NATIVE_CLOSE_OK`; preamble auxiliary, не overlay two-cycle). |
| **WSL default** | **BLOCKED** | `wsl -e …` без `-d Ubuntu`: `getpwuid(0) failed 2`, `Failed to mount C:\`, `execvpe(…) failed` (default distro `docker-desktop-data` Stopped). **Настройки WSL не менялись.** |

**Regressions @ `79b65cb` (без повторного product diff):** §W2 + Headless **PASS**; **WASM ×3 PASS**; **Android 12/12 PASS** (см. push `79b65cb`).

**Lease push base:** `79b65cb9d5d9aa8f9cdfc2297d35dbe3694525ea`.

**NEXT:** разобрать **`l3_detail_close` exit -11** в `linux_minimal_app_l3_smoke.py` (shutdown/auxiliary); опционально headless overlay two-cycle fixture.

## MinimalApp Win32 embedded two-cycle acceptance (post-`7450392`, 2026-11-02) — superseded

**Fix (product):** `minimal_app_lifecycle.cpp` (Windows) — **`NormalizeMinimalApplicationAfterEmbeddedUserLoad`** в `prepare` (parity с Linux). **List/Apply cold load** — см. секцию выше (post-`05d5a12`).

## MinimalApp embedded USER overlay — finalize audit (post-`fc75e7e`, 2026-11-02) — PASS (contract OK)

**Lease review base:** `fc75e7e787364fc59d5a7f82ae9d6b8cb91f942d`. Product persist/normalize **без изменений** (нарушения контракта не найдено).

### Save — `PersistMinimalApplicationEmbeddedUserOverlay`

1. **`FinalizeMinimalApplicationNodesForEmbeddedUserOverlay`** — обходит все достижимые **`Node*`**; для узлов с **непустым `journal`** — **`CompactJournal`** с **`max_events = 0`** (full collapse when guards allow): **`CaptureBaseStateInto`** + persist owner layers; **journal cleared — event history not stored in USER**. **Не** **`FinalizeUiNodeState`** (UI publication only; clears base without storage snapshot).
2. **`SavePresentedWindow(main_window)`** (+ auxiliary при open) — flush unloaded/lazy presented state в storage.
3. **`SaveRoot`** — пишет динамику в **USER-only** `DirectoryDomainStorage`; **BACKING** (exe / RAM embed) не трогается.

### Load — `NormalizeMinimalApplicationAfterEmbeddedUserLoad`

**`FinalizeDistilledGraph(application)`** — для каждого достижимого **`Node`**: если **`base` уже есть** или **`journal` не пуст** — skip; иначе **`InitializeRuntimeNode`**: новый same-class **base `ObjId`**, **`CaptureBaseState()`** из текущего materialized, пустой journal. Это восстанавливает инвариант **`TryInsertEvent`** (требует valid **`base`**) после save-time finalize, **без** повторного **`FinalizeUiNodeState`** на load (который снова снял бы base).

### Events / journal после cold restart

- После save journal на затронутых узлах **пуст**; динамика — в **materialized persistent слоях USER**, не в event log на диске (**нет replay** сохранённой journal history).
- После load + normalize **`Commit` / presenter→Event** снова работает (Linux L3: **`InsertEvent`** не падает).
- **Новые** Events после reload журналируются in-memory до следующего shutdown **`CompactJournal`** finalize.

### Two-cycle verification

| Platform | Cycle 1 | Cycle 2 (другая UI-команда) | Cold load 3 + Commit | BACKING |
| --- | --- | --- | --- | --- |
| **Linux** (`linux_minimal_app_l3_smoke.py`) | empty USER → Row B + Apply → save | profile → Row A selected → **select Row B** → save | profile → Applied title + sel B → **select Row A** → `MINIMAL_APP_L3_CYCLE3_POST_COMMAND_OK` | unchanged (embedded supply) |
| **Windows** | `apptraverse_minimal_app_win32_acceptance_test`: resize → save → `load_only` reload (1 cycle) | **NOT RUN** (авто 2-й UI-кадр); тот же **`PersistMinimalApplicationEmbeddedUserOverlay`** + **`Normalize…`** в `minimal_app_lifecycle` | §W2 + headless **PASS** (shared hooks не менялись) | **`ExpectEmbeddedBackingUnchanged`** in acceptance |

**Regressions (incremental, post-audit commit):** §W2 + Headless + overlay **PASS**; **Linux L3 two-cycle smoke PASS**; **WASM ×3 PASS**; **Android 12/12 PASS**. **Product code:** NOT CHANGED.

**Lease push:** `fc75e7e787364fc59d5a7f82ae9d6b8cb91f942d`.

**NEXT:** Win32 **session-automation** two-cycle (frame или list) в parity с Linux smoke; опционально headless overlay two-cycle test через стабильный overlay load fixture (без полного `ApplicationModelRuntime` thread в одном процессе с distill export).

## MinimalApp Linux build-time embedded BACKING/USER (2026-11-02) — PASS

**Supply:** `apptraverse_export_minimal_distilled_graph_linux` → per-target `embedded_distilled_minimal_app_supply.h` (Linux presenter **ClassId** layers; bytes ≠ Win32 header). Shared builder: `BuildLinuxMinimalApplicationGraph` → `BuildMinimalApplicationGraph` + Linux presenter factories (`minimal_app_graph_linux.cpp`).

**Load:** `linux_minimal_app` with `APPTRAVERSE_MINIMAL_APP_USE_EMBEDDED_DISTILL` — `RunEmbeddedApplicationModelSession` + `embedded_distilled::minimal::kEmbeddedGraph`; пустой `--state-dir` → **`MINIMAL_APP_LOADED_FROM_EMBEDDED_BACKING`**, geometry из BACKING, **без** runtime distill bootstrap в USER.

**Save:** `PersistMinimalApplicationEmbeddedUserOverlay` — journal finalize перед USER `SaveRoot`; BACKING в exe не меняется.

**Cold reload (USER overlay):** `NormalizeMinimalApplicationAfterEmbeddedUserLoad` → **`FinalizeDistilledGraph`** (восстановление **Node::base** после journal-finalize save; не `FinalizeUiNodeState` на load). **`tools/linux_minimal_app_l3_smoke.py`** (WSL Ubuntu, incremental `linux-x64-debug`) — empty USER → UI (Row B + Apply) → shutdown persist → новый процесс → **`MINIMAL_APP_LOADED_FROM_PROFILE`** + Applied mirror + Row A GTK selection → **`MINIMAL_APP_L3_POST_COMMAND_OK`** — **PASS** (1-й прогон).

**Regressions (lease `20b46b9…`, worktree `apptraverse-wt-web5`, incremental, без clean/full CTest):**
- **Linux L3:** **PASS**
- **§W2 + Headless + WindowCatalog overlay (9 CTest exe):** **PASS**
- **WASM ×3 (8766 / 8770 / 8785):** **PASS**
- **Android `connectedDebugAndroidTest` 12/12** (`Aether_NDK_Smoke_x86_64`) — **PASS** (1-й прогон)
- **`apptraverse_minimal_app_embedded_export_test`:** **PASS**
- **NOT RUN:** Web/Android product migration; profile migration; MinSizeRel embedded exes

**Lease push:** `20b46b945fa67de49f61609c22553d7c90fe6606`.

**NEXT:** Win32 cold L3 parity smoke (optional) или embedded **MinSizeRel** acceptance для WindowCatalog/MinimalApp без Debug runtime-distill footnote.

## MinimalApp Windows build-time embedded BACKING/USER (2026-11-02) — PASS

**Load:** `RunEmbeddedApplicationModelSession` + `embedded_distilled_minimal_app_supply.h` (cmake `apptraverse_embed_distilled_supply` / `apptraverse_export_minimal_distilled_graph`); пустой `--state-dir` → cold geometry **120/80/640/480** из BACKING, без bootstrap distill в USER.

**Save:** `PersistMinimalApplicationEmbeddedUserOverlay` — journal-bearing nodes `FinalizeUiNodeState` перед записью; `SavePresentedWindow` + `SaveRoot` в **USER-only** `DirectoryDomainStorage` (overlay в session); BACKING в exe не меняется.

**Cold reload E2E:** `apptraverse_minimal_app_win32_acceptance_test` — UI resize → WM_CLOSE → probe (BACKING+USER overlay) → `win32_minimal_app_load_only` восстанавливает frame; graph negatives skipped при embedded.

**Exporter:** уникальный temp dir per output header (fix parallel embed race).

**Regressions (SHA `02b9626`, worktree `apptraverse-wt-web5`, 2026-11-02):**
- **Win (incremental `win64-ninja-msvc-debug`, ранее на этом SHA):** `apptraverse_minimal_app_embedded_export_test` **PASS**; `apptraverse_minimal_app_win32_acceptance_test` **PASS**; `apptraverse_minimal_app_headless_test` **PASS**; §W2 восемь Win/Headless **PASS**.
- **Linux L3:** `tools/linux_minimal_app_l3_smoke.py` на `build/linux-x64-debug/.../linux_minimal_app` (WSL **Ubuntu**, incremental build) — **PASS** (1-й прогон).
- **WASM ×3 (`wasm-ninja-debug`, incremental):** `wasm_minimal_app_browser_smoke.py` serve **8766** **PASS**; `wasm_web_presenter_lifecycle_test.py` serve **8770** **PASS**; `wasm_window_catalog_browser_smoke.py` serve **8785** **PASS**.
- **Android:** `examples/minimal_app/android` `connectedDebugAndroidTest` **12/12** на **Aether_NDK_Smoke_x86_64** (Android 14) — **PASS** (1-й прогон, 0 failed).
- **FAIL / BLOCKED / повторы:** нет.

## WindowCatalog Windows build-time embedded — audit (SHA `5b2bb90`, 2026-11-02) — PASS (scope)

**Кто использует embedded BACKING (без runtime distill bootstrap):** только **`win32_window_catalog_demo`** и **`win32_window_catalog_demo_load_only`** при сборке **`MinSizeRel`** (`APPTRAVERSE_WINDOW_CATALOG_USE_EMBEDDED_DISTILL` в `examples/surfaces_demo/windows/CMakeLists.txt`). **`RunEmbeddedApplicationModelSession`** + **`generated/window_catalog_distilled.h`** (`embedded_distilled::window_catalog::kEmbeddedGraph`; supply constants **ObjId 15/16** = WindowPrefab / WindowPrefabPresenter). Exporter: **`apptraverse_export_window_catalog_distilled_graph`** → target **`apptraverse_generate_window_catalog_distilled_header`**.

**Debug `win64-ninja-msvc-debug` exe (текущий preset):** **`win32_window_catalog_demo`** — **`APPTRAVERSE_ENABLE_DISTILLATION`** (runtime distill в USER при пустом profile); **`win32_window_catalog_demo_load_only`** — без embedded и без distill (только существующий USER). Это **не** build-time embedded path.

**Покрытие (существующие тесты, incremental Debug @ `5b2bb90`, без дублирования):**
- **`apptraverse_window_catalog_embedded_overlay_test`** — cold load пустой USER; **`AddWindow`** (Clone) → USER persist → cold replay; **origin W/V**; **lazy `window_prefab`**; **`RunEmbeddedApplicationModelSession`** roundtrip (2 sessions). **PASS**.
- **`apptraverse_window_catalog_model_test`** — prefab Clone / disk layers (directory graph). **PASS**.
- **`apptraverse_window_catalog_win32_acceptance_test`** — Win32 UI E2E на **Debug** demo (runtime distill), не MinSizeRel embedded exe. **PASS**.

**Изменения product-кода:** нет. **Linux L3 / WASM×3 / Android 12/12:** **NOT RUN** (нет изменений кода; последний полный прогон — **`5b2bb90`** PROGRESS выше).

**Подтверждённый HEAD (unified, U1):** **`8835d26`**. **Object Core срез до merge:** **`df536e6`** (C0.2.2); docs C0.3 — **`9e91f4d`**. Application docs — [application-versioning.md](../integration/application-versioning.md), [window_catalog_demo.md](../window_catalog_demo.md).

**Baseline SHA (старт tracking fixtures):** `19d7da61954ddbab02ef098eec540ac17e936fcf`

**Порядок Object Core:** **C0 PASS** → **12R/12B PASS** → **13 IN_PROGRESS** (**13.1–13.2 PASS**, **13.3–13.6 TODO**) → **14–16 TODO**; **17–22** — roadmap ([PLAN §порядок](./PLAN.md#порядок-работ-после-11)).

**Активный порядок (2026-11-04):** **P3.T3.Web6 PASS** (§[P3.T3.Web6](#p3t3web6--web-embedded-backing--idbfs-user-2026-11-04--pass)); **NEXT:** только **Android embedded**; Apple — не NEXT. *(Исполнено: §[P3.T3.AE](#p3t3ae--android-embedded-backing--user-2026-11-04--pass); NEXT — macOS MinimalApp.)*

**Активный порядок (2026-11-02, historical):** **P3.T3.L3 PASS** (§P3.T3.L3). **WindowCatalog обязательный граф** — правки продолжаются (§[WindowCatalog required links](./PROGRESS.md#windowcatalog-required-links-fatal-2026-11-02)). **P3.T3.M (macOS)** — **не NEXT**. Полная Android lifecycle matrix — **не начинать**. **P0/P1 DONE** ([PLAN §P0–P6](./PLAN.md#активный-план-мультиплатформенный-windowcatalog-p0p6)).

**Object Core (исторический roadmap, DEFERRED):** **13 IN_PROGRESS**; прежний **NEXT: 13.3** не является активным заданием. **11** **IN_PROGRESS** (частичный PASS). **01–10** **PASS** в заявленном scope. **14–22** — **TODO**. **T16/T17** — фрагменты. Полная Object Core regression на unified не заявлена; снятый U6 не возвращается в активную очередь.

**OPEN:** динамический источник Clone **без** устойчивого const **P** — явное решение, не silent fallback ([PLAN §13](./PLAN.md#13--универсальный-clone)).

**Нормативные правила Object Core:** [PLAN §нормативные правила](./PLAN.md#нормативные-правила-object-core-v03--01-plan) (здесь не дублируются).

**Fixture vs библиотека:** **`CloneCanonicalRoot`** — **13.2 PASS** (один объект); **P2.e.1 PASS** — remap штатного Save + двухобъектный graph (**13.3** частично); **P2.e.1R** — общий BACKING + cold **`Load(C)`**; **P2.e.2 PASS** — loaded Node R→D, **`CanonicalClone`**, собственные base/journal; **P2.e.2R PASS** — fresh history по remap-набору (Obj-корень + вложенный Node); **P2.e.3 PASS** — unloaded **`ObjPtr`** child через **`NotifyCloneReference`**, lazy сохранён; **P2.e.4 PASS** — **`UiDesktopWindow`** ↔ **`UiDesktopWindowPresenter`**, **`stable_prefab_ids`**, двойной Clone W→A→B; **P2.e.5 PASS** — **`WindowCatalog::AddWindow`** через **`CloneCanonicalRoot`**, **`WindowCatalog/S1`**; MinSizeRel manual (prefab Add/Open/Close/geom/locale/restart) **user confirmed**; дефект Z-order → **P2.e.5R PASS** — **`WindowCatalog/S2`** (`desktop_z_order`), **`SetWindowCatalogDesktopZOrderEvent`**, shutdown HWND→ObjId capture, **`WinApplicationHostCallbacks::after_initial`**. **P2.f.1 PASS** — MinSizeRel **`WindowCatalogModelSession`**: **`InstallEmbeddedDistilledBacking`** + USER + **`OverlayDomainStorage`**. **P2.f.1R PASS** — **`kSupplyConstantIds` {15,16}** (WindowPrefab / WindowPrefabPresenter); MinSizeRel **`win32_window_catalog_demo_load_only`**; W/V не материализуются в USER. Полный lazy closure / **13** — **TODO**; holder/title builder — **TODO**; **P2.f.2/P2.f.3 PASS** — общий lifecycle API; активный NEXT указан выше.

Общие правила разработки: [.cursor/rules/apptraverse-coding-agent.mdc](../../.cursor/rules/apptraverse-coding-agent.mdc). Корневой журнал других подсистем: [Progress.md](../../Progress.md).

---

<a id="p3t3w1--windows-presenter-command-routing-2026-10-29-pass"></a>

## P3.T3.W1 — Windows presenter command routing (2026-10-29) — PASS

**Scope (landed on `2629e86`, extended through W1 completion):** `RouteChildCommand` → один вызов child `OnCommand`; `WinPushButtonPresenter::OnCommand` (`BN_CLICKED` → `NotifyClickedFromUi`); `WinListContainerPresenter::OnCommand` → `HandleListNotification`.

**Published graph (S0 test roots, `win_ui_command_routing_test_nodes.*`):** `CommandRoutingButtonRoot` / `CommandRoutingListRoot` — `UiDesktopWindow` + `WinDesktopWindowPresenter` (`ComposePresentedWindow`, locale, title), control (`TestDisableOnClickButton` / `TestActivationList` + rows), child presenter (`WinPushButtonPresenter` / `WinListContainerPresenter`), initial frame via `SetUiDesktopWindowFrameEvent`; `SerializeInitialPublication` → **`ApplicationUiSession`** (`InitializePresenters` / `ApplyStructural`, teardown в dtor). UI mirror: typed refs с корня; **без** ручного `OnLoad`/`OnUnload`/`presentation_loaded` в сценариях.

**Native E2E (`apptraverse_win_ui_command_routing_test`, Debug, `main`):** `TestEditRouting` (legacy isolated edit route); **`TestPushButtonNativeClickViaProxyAndPublication`** — drain post-HWND frame work; **`SetWindowPos`** + `win_ui_frame.h` → deferred proxy → `ReportUiDesktopWindowFrameFromNativeEvent` на model (mirror lag до `ApplyStructural`); **BM_CLICK** → один proxy → `SetUiPushButtonEnabledEvent` → structural → mirror/HWND disabled; desktop + child **`model_proxy`**. **`TestListNativeSelectionViaProxyAndPublication`** — LBN_SELCHANGE → selection event → structural mirror; LBN_DBLCLK → `last_activated`. **`TestActivationListStructuralMirrorHeadless`**. **Fixture fix (ранее):** `TestWinPushButtonResolveChildLayoutFromHost` — locale/label.

**Устранённое ограничение тестов:** split publication (control-only + UI-side desktop без proxy) заменён полным model graph + session lifecycle; frame route и WM_COMMAND проверяются на одном связном зеркале.

**WindowCatalog prototype schema (2026-10-29):** единый прикладной **S0** — `window_prefab` + `desktop_z_order` в Load/Save S0; S1/S2 удалены. Default paths: Win **`surfaces_catalog_s0_proto_state`**, Web **`/persistent/window_catalog_s0_proto_state`**. **Catalog acceptance:** root unsupported-schema **S0→S1**; диагностика до CHECK.

**Regressions (win64-ninja-msvc-debug, 2026-10-29):** **PASS** — `apptraverse_win_ui_command_routing_test`, `apptraverse_win_ui_native_push_button_test`, `apptraverse_win_application_host_test`, `apptraverse_minimal_app_win32_acceptance_test`, `apptraverse_window_catalog_win32_acceptance_test`, `apptraverse_minimal_app_headless_test`, `apptraverse_window_catalog_headless_test`, `apptraverse_ui_control_materialization_test`. **WASM (post-`ea6a599`, 2026-10-29):** `CMAKE_HOME_DIRECTORY` = `apptraverse-wt-unified`, `EMSCRIPTEN=1`; target `web_window_catalog_demo` (link via `em++`, `--shell-file=…/window_catalog_shell.html`, `OUTPUT_NAME` + `.html`) — артефакты **`build/wasm-ninja-debug/examples/surfaces_demo/web/`**: `web_window_catalog_demo.html`, `.js`, `.wasm`, `.worker.js`. **Причина ложного «нет HTML» после W1:** поиск артефактов не в каталоге link output (корень `build/wasm-ninja-debug` / пустой recursive glob) + «ninja: no work to do» при сборке только static-lib targets без проверки `examples/surfaces_demo/web/`. Incremental `--target web_window_catalog_demo` — **build PASS** (без перелинковки). **Browser smoke PASS:** `python tools/serve_wasm.py build/wasm-ninja-debug/examples/surfaces_demo/web --port 8781` + `python tools/wasm_window_catalog_browser_smoke.py --url http://127.0.0.1:8781/web_window_catalog_demo.html` → `WINDOW_CATALOG_INITIAL_RENDER_OK`, screenshot `docs/smoke_artifacts/window_catalog_web_initial.png`. Исторические WASM smoke PASS (другие SHA) сохранены.

---

<a id="p3t3w2--windows-list-selection-sync-from-mirror-2026-10-29-pass"></a>

## P3.T3.W2 — Windows list selection sync from mirror (2026-10-29) — PASS

**Library:** `WinListContainerPresenter::SyncFromMirror` (`win_ui_presenters.cpp`) — helper `ApplyPublishedListSelection` при `notify_list_selection_change == true`: после синхронизации строк применяет опубликованный `selection_index`, сравнивая **фактический** `LB_GETCURSEL` с HWND (не кеш модели); `LB_SETCURSEL` только при расхождении (в т.ч. после rebuild, когда native selection мог сброситься); `-1` снимает выделение только если `LB_GETCURSEL != LB_ERR`. **`previous_sel` не восстанавливается** поверх mirror. `notify_list_selection_change == false` — прежняя семантика (`previous_sel` при rebuild). `previous_top` сохранён; без proxy/model из SyncFromMirror. *(Ранее в W2 упоминался production-вызов `LB_GETCURSEL` с выброшенным результатом — это не была проверка; контроль `-1` остаётся в тестах.)*

**Scroll follow-up (W2, тот же PASS):** до idempotent fix — `TestListTopIndexPreservedWhenTitlePublicationUnchangedSelection` (**FAIL**, exit **3**): после title-only structural apply `LB_GETTOPINDEX` ≠ сохранённому (лишний `LB_SETCURSEL(0)` при неизменённых строках/selection). После idempotent `ApplyPublishedListSelection` — **PASS** (top index сохраняется, title обновлён, proxy пуст).

**Native E2E:** `TestListNativeSelectionViaProxyAndPublication` — три сценария selection/text/remove; **`TestListTopIndexPreservedWhenTitlePublicationUnchangedSelection`** — 26 строк (`ExtendCommandRoutingListGraphForScroll`), selection 0 + `LB_SETTOPINDEX`, publication только title (`SetUiLocalizationSlotUtf8Event` на `window_presenter->title`). Границы mirror/HWND до/после `ApplyStructural`.

**Regressions (win64-ninja-msvc-debug, scroll follow-up):** **PASS** — тот же набор §W2 + `apptraverse_win_ui_command_routing_test`. **История:** `apptraverse_window_catalog_win32_acceptance_test` — отдельный прогон W2: 1-й run timeout `IsIconic`, повтор **PASS** (причина не установлена). **WASM:** build + browser smoke (`serve_wasm.py`, `examples/surfaces_demo/web`, port **8785**) **PASS**.

**Windows scope (точно):** библиотечные **Window / List / Button** presenters + **MinimalApp** lifecycle/acceptance/headless; семь backend и переносимость профиля **не** заявлены.

---

<a id="p3t3web1--web-minimalapp-initial-publication-2026-10-29-pass"></a>

## P3.T3.Web1 — Web MinimalApp initial publication (2026-10-29) — PASS

**Library:** `apptraverse_ui_web` — `WebApplicationHost` (model worker → `ProxyingQueue::proxyAsync` → main: `TakePublishedCopy` / `NotifyPublicationConsumed` / `ApplicationUiSession` / `ApplyStructural`), `WebDesktopWindowPresenter` (DOM via `EM_JS` + `EM_VAL`, geometry from mirror 120/80/640×480, empty title/locale OK), `WebPresentationHost::mount`.

**Example:** `examples/minimal_app/web/` — `web_minimal_app` (`BuildWebMinimalApplicationGraph`, RAM bootstrap + `RunApplicationModelSession`, `emscripten_exit_with_live_runtime`).

**WASM (wasm-ninja-debug):** build `web_minimal_app` + `web_window_catalog_demo`; browser smoke **`tools/wasm_minimal_app_browser_smoke.py`** (COOP/COEP, one presenter window, `MINIMAL_APP_INITIAL_RENDER_OK`) **PASS**; catalog initial smoke **PASS**. **WindowCatalog Web** on new backend **not** migrated.

**Regressions (win64-ninja-msvc-debug):** **PASS** — §W2 набор (`apptraverse_win_ui_command_routing_test`, `apptraverse_win_ui_native_push_button_test`, `apptraverse_win_application_host_test`, `apptraverse_minimal_app_win32_acceptance_test`, `apptraverse_window_catalog_win32_acceptance_test`, `apptraverse_minimal_app_headless_test`, `apptraverse_window_catalog_headless_test`, `apptraverse_ui_control_materialization_test`).

**Web1 limits:** no IndexedDB/IDBFS page reload persistence; no shutdown/join on main; incremental handler wired, incremental/UI-input smoke **not** PASS; Web2 not started.

<a id="p3t3web2--web-minimalapp-button-list-ui-commands-2026-10-30-pass"></a>

## P3.T3.Web2 — Web Button/List + MinimalApp UI commands (2026-10-30) — PASS (R1+R2)

**Library (`apptraverse_ui_web`):** `WebPushButtonPresenter`, `WebListContainerPresenter` (DOM via `EM_JS`/`EM_VAL`, `EMSCRIPTEN_KEEPALIVE` UI callbacks, owned-handle release on unload; shared `AppendDomChild` in `web_ui_dom_em`); `WebDesktopWindowPresenter` — no destructor teardown, `ReadyForPresentation` gate, `ClientMountHandle` for child layout parent.

**Framework:** `InitializeNewPresenters` assigns `presentation_host` / `model_proxy` before `ReadyForPresentation` (Web mount/host gating); child presenter Ready waits on loaded parent + client mount.

**Common MinimalApp:** `MinimalApplicationPresenters` on graph root (distill/cold-load reachability), `PrepareMinimalApplicationForRun`, Apply/Row A/B + Events; profile namespace `minimal_app_s1_controls_state`.

**E2E:** `apptraverse_minimal_app_headless_test` (UI-command sequence + existing cold-load); `tools/wasm_minimal_app_browser_smoke.py` (click/select/dblclick → incremental publication, DOM stability). **WASM:** `web_minimal_app` + catalog initial smoke **PASS** (`8785` serve when `8765` busy).

**Regressions (win64-ninja-msvc-debug):** **PASS** — §W2 набор + `win32_minimal_app` / `win32_minimal_app_load_only` build.

**Web2 limits:** no browser storage save/cold reload (**Web3**); WindowCatalog Web migration not started; shutdown/join on main not implemented.

**Web2 status:** **PASS** (R1+R2). **Web3:** **PASS** (§P3.T3.Web3). **P3.T3.Web** / **P3** — **IN_PROGRESS**; **NEXT: Web4**.

---

<a id="p3t3web3--browser-storage-save-cold-reload-2026-10-30-pass"></a>

## P3.T3.Web3 — browser storage save + cold reload MinimalApp (2026-10-30) — PASS

**Base:** `ba31333160b1e88ea63fa06d3fd18b31264c3340` (Web2R2).

**Library (`apptraverse_ui_web`):** `StartWithPersistentStorage(mount)` — IDBFS mount + `syncfs(true)` → `Start()`; `RequestSave` → model `persist_model` via `Post` (FIFO after UI work) → `syncfs(false)` on main; save blocks mount input + duplicate Save; `web_idbfs.*` (no app types).

**Common:** `BootstrapMinimalApplicationProfileIfAbsent`, `PersistMinimalApplication` (BeginSaveSession / SavePresentedWindow / root Save / SaveSessionHadFailure); Windows lifecycle reuses persist helper.

**MinimalApp Web:** profile `/persistent/minimal_app_web_s0_controls_state`; dev bootstrap only when profile empty after populate (`MINIMAL_APP_DEV_BOOTSTRAP` vs `MINIMAL_APP_LOADED_FROM_PROFILE`); Save UI + `MINIMAL_APP_IDBFS_SAVED` after confirmed flush.

**CMake (`web_minimal_app`):** `-sFORCE_FILESYSTEM=1`, `-lidbfs.js`, IDBFS keepalive exports + `FS` runtime method.

**E2E:** `wasm_minimal_app_browser_smoke.py` — UI sequence, Save, **two** `page.reload()` cycles with profile-load markers and state checks; `serve_wasm.py` (COOP/COEP).

**Regressions (incremental Debug):** lifecycle browser **PASS**; catalog initial smoke **PASS**; §Web2R1 Win/Headless eight + load-order/ui_session **PASS**; `win32_minimal_app` / `win32_minimal_app_load_only` **build PASS** (`APPTRAVERSE_MINIMAL_APP_LOAD_ONLY` — no bootstrap).

**Limits:** full `WebApplicationHost` shutdown/join **not** implemented; cross-backend profile portability **not** claimed.

**NEXT:** **P3.T3.Web4** — несколько представлений + lifecycle на общем Web host.

---

<a id="p3t3web4--dual-window-lifecycle-2026-10-30-pass"></a>

## P3.T3.Web4 — dual window lifecycle on one Web host (2026-10-30) — PASS

**Base:** `550575569333c3bd5bc2e25498e7b4bb0f9f2da7` (Web3).

**Common (MinimalApp S0):** второе окно в `MinimalAuxiliaryWindowState` + `auxiliary_presented_open`; `ComposeMinimalWindowPanel`; Open/Close через `MinimalOpenAuxiliaryWindowEvent` / `MinimalCloseAuxiliaryWindowEvent` и `LoadAuxiliaryPresentation` / `SealAuxiliaryWindowClosed` (сброс presenter/control refs с корня при Close); dev bootstrap закрывает auxiliary через `SealAuxiliaryWindowClosed`.

**Web host:** `DirectoryDomainStorage` создаётся в model-thread `run_model` после `syncfs(true)`; Save блокирует ввод через `#app-mount` **`inert`**; persist берёт live root через `domain.Find`; `DomainGraph::SaveRoot` + cold load через `LoadRoot`; profile `/persistent/minimal_app_web_s1_dual_window_state`.

**E2E:** `wasm_minimal_app_browser_smoke.py` — Web3 два save/reload + dual window Close/Open, независимость controls, cold reload с reopen Detail при lazy presentation; `wasm_web_presenter_lifecycle_test` — dual-window teardown/stale click; catalog initial smoke **PASS**.

**Headless:** `TestMinimalAppAuxiliaryCloseOpenHeadless`, `TestMinimalAppAuxiliaryPersistColdLoad` (DirectoryDomainStorage round-trip).

**Limits:** после cold reload auxiliary presentation может быть lazy (reopen через UI); cross-backend profile portability **не** заявлена.

---

<a id="p3t3web5--web-application-host-async-shutdown-2026-10-30-pass"></a>

## P3.T3.Web5 — WebApplicationHost async shutdown (2026-10-30) — PASS

**Base:** `982b47366bca343f78dc6275fdac7b6eaceb84fb` (Web4).

**Library (`WebApplicationHost`):** `RunModel` → `int` (результат `RunApplicationModelSession`); `RequestShutdown()` — блок Save/ввод, ожидание in-flight Save, `RequestStop`, drain model queue; на worker — final persist; на main — `proxySync` UI teardown после drain публикаций, `syncfs(false)` с подтверждением, `pthread_tryjoin_np` через `emscripten_async_call` (без blocking `join`/`std::thread` на browser main); `Stopped` только после persist+flush+join; destructor не запускает shutdown и не блокирует main.

**MinimalApp Web:** `RunApplicationModelSession<MinimalApplication>` + prepare/persist/bootstrap hooks; shell **Stop** + статус; `ApptraverseMinimalAppRequestShutdown()`.

**E2E:** `wasm_minimal_app_browser_smoke.py` — Web3/Web4 сценарии + Stop без предварительного Save, DOM teardown, cold reload с profile load и UI-команда после reload.

**Regressions (incremental Debug, `apptraverse-wt-web5`):** `web_minimal_app` extended smoke **PASS**; `apptraverse_web_presenter_lifecycle_test` + browser runner **PASS**; `web_window_catalog_demo` initial smoke **PASS**; §Web2R1 восемь Win/Headless + `apptraverse_presenter_load_order_test`, `apptraverse_application_ui_session_test`, `apptraverse_application_model_runtime_test`, `apptraverse_application_contract_headless_test` **PASS**; `win32_minimal_app` / `win32_minimal_app_load_only` **build PASS**.

**Limits:** auxiliary presentation после reload может быть lazy (Web4); cross-backend profile portability **не** заявлена.

**NEXT:** **P3.T3.A** — первый срез по действующему плану (не начат в этой итерации).

---

<a id="p3t3web5r1--web-shutdown-cleanup-emval-smoke-2026-10-30-pass"></a>

## P3.T3.Web5R1 — shutdown cleanup + emval/stale DOM smoke (2026-10-30) — PASS

**Base:** `e92f9939ed10b47e05c8fc29b999a3bf985aa443` (Web5).

**Host:** `TryJoinModelThreadOnce` — `kStopped` до completion callback; после callback host не используется.

**MinimalApp Web:** `g_durable_storage` освобождается на model worker после `RunApplicationModelSession`; на следующем turn main — `g_host` teardown, release owned mount EM_VAL, `Stopped` + `MINIMAL_APP_SHUTDOWN_STOPPED` только после cleanup.

**Smoke:** capture `.apptraverse-list-row` + Apply button; после Stop — row click/dblclick + button click; `count_emval_handles` startup (post-initial-render) vs 0 после cleanup (pre-stop > 0); признак успеха только из console `MINIMAL_APP_SHUTDOWN_STOPPED`, не из DOM status.

**Regressions:** те же §Web5 (extended minimal smoke, presenter lifecycle browser, catalog initial, §Web2R1 Win/Headless, minimal Win builds).

---

<a id="p3t3minimalappr1--minimalapp-winheadless-on-82d2b25-2026-10-30-pass"></a>

## P3.T3.MinimalAppR1 — Win/Headless checks on Web5R1 baseline (2026-10-30) — PASS

**Base (lease):** `82d2b2568a6a41d85e1eea2ad792754f277d3efd` (**P3.T3.Web5R1**; Web scope **PASS** без изменений).

**Фактические FAIL на `82d2b25` (до правок тестов):**

| Тест | Симптом | Причина |
|------|---------|---------|
| `apptraverse_minimal_app_headless_test` (`TestMinimalAppAuxiliaryCloseOpenHeadless`, ~558) | `UiAuxiliaryRowList.selection_index == -1` после reopen | Сценарий не соответствовал продукту: после выбора строки 1 во **Detail** нажимался только **главный** Apply (`UiApplyButtonPresenter` → `MinimalApplyButtonPresenterId()`), затем close/reopen; ожидался результат **Detail Apply** (selection `-1`, title `Applied`) без клика `auxiliary.presenters.apply_button`. |
| `apptraverse_minimal_app_win32_acceptance_test` (`RunAcceptance`, ~448 / ~467) | `WaitUntil` timeout: `CountChildWindows(main) == 2` | На main уже три named controls (`7101` Apply, `7102` List, `7103` Open Detail из `minimal_app_controls.h`); условие «ровно 2 дочерних HWND» никогда не выполнялось. |

**Win32 (на repro до fix):** дочерние HWND main с `control_id` **7101**, **7102**, **7103** (классы native button/list/opener presenters), parent = main desktop window; отдельно одно видимое `AppTraverseUiDesktopWindow` на PID.

**Исправление (только tests):** headless — трёхшаговый сценарий (main Apply → aux Apply через `NotifyClickedFromUi` → closer/opener presenters + `PumpModelWorkAndApplyStructural`); win32 — ожидание ровно по одному HWND на каждый named ID + диагностический dump при timeout.

**Проверки (win64-ninja-msvc-debug, `apptraverse-wt-web5`):** `apptraverse_minimal_app_headless_test` **PASS**; `apptraverse_minimal_app_win32_acceptance_test` **PASS** (save + `load_only` + graph negatives). Библиотечный код не менялся → дополнительный Web/Win regression объём не требовался.

---

<a id="p3t3a1--android-minimalapp-initial-ui-2026-10-30-pass"></a>

## P3.T3.A1 — Android MinimalApp initial UI (2026-10-30) — PASS (historical visual smoke)

**Base (lease):** `ad95a149a99053e77ad65c4a4270149bebf905df` (**P3.T3.MinimalAppR1**).

**Примечание (A1R1):** PASS получен с app-level обходами (`Declare` opener label id, `resolve_push_button_label_utf8` для **7103**). Это **не** канонический путь `presenter → button → label` из distilled графа; см. §P3.T3.A1R1.

**Scope (PASS):** Debug APK на эмуляторе; после initial publication — одна страница «Minimal App», кнопки **Apply** / **Open Detail**, список **Row A** / **Row B** без выбора; Detail закрыт. Команды, Close/Open Detail, recreate Activity, save/cold reload — **вне** PASS.

**Targets:** `apptraverse_ui_android` (library); APK `examples/minimal_app/android` → `apptraverse_minimal_app` + `minimal_app_android` + `minimal_app_common`.

**Publication path:** model worker `RunApplicationModelSession` → `AndroidUiBridge` → main `TakePublishedCopy` → `ApplicationUiSession` / `InitializePresenters`; контейнер `content_root` до `StartModelThread`.

**Runtime:** AVD **`Aether_NDK_Smoke_x86_64`**, API **34**, **x86_64**; logcat `MINIMAL_APP_INITIAL_RENDER_OK`; UiAutomator dump + `InitialUiInstrumentedTest` **PASS**.

**Controls (UiAutomator):** Apply (7101), Open Detail (7103), Row A / Row B (7102 list); selection `-1`; нет «Detail Panel».

**Артефакты:** `docs/smoke_artifacts/minimal_app_android_ui_dump.xml`, `minimal_app_android_a1.png`.

**Regressions:** `apptraverse_minimal_app_headless_test` **PASS**; `apptraverse_minimal_app_win32_acceptance_test` **PASS** (`GetDlgCtrlID` в `HwndControlId`); Win32 headless auxiliary сценарий сохранён. Web5R1 полный набор **не перезапускался** (без clean/full CTest).

**Known gap (снят в A1R1 WIP):** A1 workarounds для **7103** — удалены в ветке A1R1; см. §P3.T3.A1R1.

**NEXT (после A1R1 PASS):** **P3.T3.A2** — Android input через presenter → proxy → Event (не начинать).

---

<a id="p3t3a1r1--android-opener-label-from-graph-2026-10-30"></a>

## P3.T3.A1R1 — Android opener label from published graph (2026-10-31) — PASS

**Base (lease):** `2342029eaf9d8a11eb1fa87f71877bfb56bbd378`.

**Продукт:** без A1 workarounds; label opener **«Open Detail»** из distilled графа → cold load до Prepare → `SerializeInitialPublication` → UI RamDomain → `presenter->button->label` (Android instrumented + Win headless probes).

**Исправленный дефект (shipping, Æther pin `79283b6`):** additive **`aether-objects-reset-object-load-state-v6r1.patch`** поверх **`aether-objects-cold-load-root-flush-v6.patch`** — `DomainGraph::ResetObjectLoadState` сбрасывает `pending_stored_layers` и `cycle_detector.visited_nodes` при rematerialize / `TryLoadStoredLayer::IsErr` / pending без `Find`, чтобы повторный `LoadRoot` не пропускал обязательные stored-слои (в т.ч. **UiPushButton** `1491514641` для opener **127991825**). Алгоритм v6 **не менялся**; diag-патч и trace **не** в shipping chain.

**ClassId (исправленная интерпретация):** `MinimalAuxiliaryOpenerButton` **1145469484**, `MinimalApplyButton` **209895325**, `UiPushButton` **1491514641**; после UI hydrate opener **127991825** → `GetClassId()==1145469484`. **209895325** на том же ObjId в **disk profile** ≠ UI **RamDomainStorage** publication.

**Pre-fix причина первого снятия opener #1 с registry — не доказана на финальном кандидате:** LLDB/WIP указывали второй `ConstructObj` при `Find==null`; на v6+v6r1 **без diag** `Domain::RemoveObject` для opener **0**, `TryLoadStoredLayer is_err` **0**, были `Obj::~Obj` без предшествующего `RemoveObject` (артефакты: `tools/a1r1_diagnostics/`, `tools/a1r1_*.txt`).

**Æther patches (15, без diag):** 14 baseline + **v6** + **v6r1**; `cmake/scripts/verify-aether-patch-chain.sh` **OK**.

**Проверки (incremental Debug, `apptraverse-wt-web5`):** Android **ColdLoadLabel**, **InitialPublicationLabel**, **InitialUi** instrumented **PASS**; §Web5 Win — §W2 восемь + `apptraverse_presenter_load_order_test`, `apptraverse_application_ui_session_test`, `apptraverse_application_model_runtime_test`, `apptraverse_application_contract_headless_test`, `apptraverse_minimal_app_headless_test` **PASS**; Object Core — `apptraverse_checkpoint_load_cycle_persistence_test`, `apptraverse_checkpoint_load_rehydration_test` (+ cached `Find` identity/unsaved locale), `apptraverse_model_persistence_test`, `apptraverse_object_clone_test`, `apptraverse_object_origin_facet_test`, CTest **`apptraverse_object_origin_disk_roundtrip_test`** **PASS**; `win32_minimal_app` / `win32_minimal_app_load_only` **build PASS**; WASM — `wasm_minimal_app_browser_smoke.py`, `wasm_web_presenter_lifecycle_test.py`, `wasm_window_catalog_browser_smoke.py` **PASS** (serve `8766` / `8770` / `8785`).

**NEXT:** **P3.T3.A2** — Android input (не начинать).

---

<a id="p3t3a--android-save-cold-reload-2026-10-31-pass"></a>

## P3.T3.A — Android checkpoint save + resume + cold reload (2026-10-31) — PASS

**Base (lease):** `fc0f89f87afa9f2260101b3bbec0e7b5211fcf62` (исправление поверх первого среза **fc0f89f**).

**Исторический прогон (fc0f89f, superseded):** checkpoint через `RequestStop` + shutdown persist; model session не переживал Home; E2E cold reload опирался на kill без same-process resume. Сохранён в истории коммита **fc0f89f**; не является целевой архитектурой.

**Scope (fix):** `onStop` — **запрос checkpoint**, model session **продолжает работать**; `PersistMinimalApplicationFromDomain` через `ApplicationModelRuntime::Post` (FIFO после UI work); подтверждение на main looper; `model_thread_live_` + `pthread_create` синхронизированы через worker entry + `condition_variable` (без фиктивного live до старта worker).

**Маршрут Save:** `MainActivity.onStop` → `requestBackgroundSave` → `AndroidApplicationHost::RequestBackgroundSave` → `runtime_.Post` → `persist_model` / `PersistMinimalApplicationFromDomain` → `NativeUiBridge.onNativeBackgroundSaveFinished` (main Handler) → latch/observer.

**Same-process resume:** Home → save confirmed → foreground без kill → Row A tap → selection + **Applied** (model/proxy живы).

**Cold reload evidence:** out-of-process `:demo` (debug manifest) — `am kill` + подтверждение смерти PID + новый PID; in-process instrumentation — `clearRuntimeForColdReloadTest` (main thread) + relaunch; disk probe `nativeRunColdReloadApplyStateProbe` + UI **Applied** + log `MINIMAL_APP_LOADED_FROM_PROFILE` (best-effort); post-reload Row A selection change.

**Тесты (Aether_NDK_Smoke_x86_64, Debug, 2026-10-31):** 7/7 instrumented (**SaveColdReloadInstrumentedTest** ×3: same-process, save-in-flight, cold reload) — **PASS** (historical на **fc0f89f**).

**Корректирующий срез (2026-11-01, lease `fae9c1ae…`, superseded частично `cf61692`):**

- **Model thread start:** ожидание на UI — `model_worker_entered_` (+ `model_start_cv_`), не `model_thread_live_`; joinable worker при быстром exit (`model_thread_joinable_`); `JoinModelThreadForRelease` join даже если worker уже не live. Probe `nativeRunModelThreadStartSyncProbe` + `ModelThreadStartSyncInstrumentedTest` (cv, без Sleep).
- **Повторный checkpoint:** `pending_background_save_` при `kSaving`; второй persist через повторный `EnqueueBackgroundSaveWork` (FIFO после UI), не inline-loop в одном `Post`. Persist test gate (`nativeEnable/Open/DisableCheckpointPersistTestGate`) для удержания первого save.
- **E2E overlap:** `overlappingBackgroundSavePersistsLastChangeAfterColdLoad` — Row A через presenter при закрытом gate; disk `nativeRunColdReloadProfileStateProbe(..., sel=0)` до/после cold reload; **ColdReloadPath:** на smoke AVD выполнен только **`IN_PROCESS_RUNTIME_RESET`** (`clearRuntimeForColdReloadTest`, demo PID = instrumentation PID); **NEW_PID_DEMO_PROCESS** в этом прогоне не доказан.
- **Logcat:** `logcat -c` не используется; `resumeWhileSaveInProgress` доказывает пересечение (save не завершён 2s при закрытом gate, ввод Row A, затем gate open + confirmation).

**Проверки (incremental Debug, `apptraverse-wt-web5`, 2026-11-01):** Android **9/9** `connectedDebugAndroidTest` — **PASS**. Headless **`apptraverse_minimal_app_headless_test`**, **`apptraverse_window_catalog_headless_test`**, §W2 кроме catalog acceptance — **PASS**. **`apptraverse_window_catalog_win32_acceptance_test`** — **FAIL** @ `window_catalog_win32_acceptance_test.cpp:1418` (`main != nullptr`, iconic/minimize; исторически flaky, не Android checkpoint). WASM **`wasm_minimal_app_browser_smoke.py`**, **`wasm_web_presenter_lifecycle_test.py`**, **`wasm_window_catalog_browser_smoke.py`** (serve **8785**) — **PASS**.

**Android lifecycle (общий):** **IN_PROGRESS** — checkpoint/resume/cold + race/overlap slice; полная матрица lifecycle/shutdown не закрыта.

**NEXT:** при необходимости cold reload E2E с **NEW_PID_DEMO_PROCESS** на конфигурации `:demo` ≠ instrumentation; иначе расширение lifecycle beyond slice (не начинать полную matrix).

**Доработка checkpoint-среза (2026-11-01, lease `cf61692…`):**

- **`RequestBackgroundSave`:** `pending_background_save_=true` до CAS; при успешном старте save pending сбрасывается; устраняет окно «persist завершился до pending». Подтверждение **последнего** checkpoint — финальный **`NativeUiBridge.onNativeBackgroundSaveFinished` → `MinimalAppApplication.onBackgroundSaveFinished`** (в overlap: latch **2** + `result==0` после второго callback).
- **`StartModelThread`:** без ожидания worker на UI thread; probe + **`ModelThreadStartSyncInstrumentedTest`** (non-blocking start + fast exit join).
- **Overlap E2E:** `nativeWaitForCheckpointPersistGateEntries(1)` после первого home; disk probe и UI selection **раздельно**; после cold reload **Row A selected до кликов**; путь **`IN_PROCESS_RUNTIME_RESET`** только.
- **Win acceptance `main==nullptr` @1418:** не iconic — demo **exit 3** (`LoadApplication failed`): `focus_apply_count.txt` создавался **внутри** `--state-dir`, профиль не empty → bootstrap пропускался. Исправление: sidecar в `%TEMP%/apptraverse_test/…`; **`apptraverse_window_catalog_win32_acceptance_test` PASS**.

**Проверки (incremental Debug, 2026-11-01):** Android **10/10** instrumented; Headless + §W2 (incl. catalog acceptance); WASM minimal + presenter lifecycle + catalog (**8785** serve) — **PASS**.

**NEXT:** NEW_PID cold reload E2E при отдельном задании; не начинать полную lifecycle matrix.

**Checkpoint join / early onStop (2026-11-01, lease `d6917ca…`):**

- **`JoinModelThreadForRelease`:** `model_teardown_requested_` + всегда `RequestStop` + `model_start_cv_` notify; worker может выйти до `run_model` / до `model_thread_live_`; hook `before_model_thread_join` для gate-probe. **`StartModelThread`** по-прежнему без блокировки UI.
- **Ранний `onStop`:** `RequestBackgroundSave` ставит `pending` даже до `model_thread_live_`; **`TryStartBackgroundSaveFromPending`** при входе worker — **`EarlyBackgroundSaveInstrumentedTest`**.
- **`PendingSaveRace` probe:** один первый save после `run_model` ready (cv), второй запрос **после** gate entry=1, без retry/polling loop — **`PendingSaveRaceInstrumentedTest`**.
- **Model thread probes:** gate join до long `RunMinimalApplicationModel` + immediate join stop — в **`ModelThreadStartSyncInstrumentedTest`**.

**Проверки:** Android **11/11** instrumented; Headless + §W2; WASM ×3 (**8785** catalog) — **PASS**.

**NEXT:** NEW_PID cold reload E2E (отдельное задание); полная lifecycle matrix не начинать.

**Teardown flag sync (2026-11-01, HEAD `af42cc4`, base lease `d88b38f…`):** все чтения/записи `model_teardown_requested_` под `model_start_mu_` (в т.ч. post-save проверка в worker и сброс в `JoinModelThreadForRelease`); **`RunConcurrentStartJoinProbe`** в `ModelThreadStartSyncInstrumentedTest`. Android **11/11**, Headless + §W2, WASM ×3 — **PASS**.

**NEW_PID cold reload E2E (2026-11-01, base lease `5302ab5…`, follow-up `fd2a0d8…` → `67c52ed…`):**

- **Процессы:** debug — только `MainActivity` в `:demo`; test APK — `:instrumentedtests`; JNI в instrumentation через **`InstrumentedTestNative`**.
- **`NewPidColdReloadInstrumentedTest`:** UI → profile-scoped save log + disk checkpoint → **debug `EXIT_DEMO_PROCESS` broadcast** (без `am kill` пакета) → relaunch → cold load probe + UI.
- **Evidence (разные утверждения):** (1) **Перезапуск процесса `:demo`** — после exit broadcast (+ optional `kill -9`) `:demo` отсутствует в `/proc` в окне ожидания теста; relaunch доказывает cold load E2E. (2) **Числовой PID** — `pidBefore`/`pidAfter` в instrumentation status только для диагностики; **`pidAfter ≠ pidBefore` не требуется** (reuse возможен после verified death gap). Исторические PASS-блоки (9/12, более ранние срезы) **не** переписываются этим уточнением.
- **Корневые причины suite FAIL (исправлено):** (1) **приложение** — `ensureNativeRuntime` не пересоздавал runtime при смене `stateSubdir` в живом `:demo`; (2) **тест** — save log без `subdir=` принимал чужой `background save finished` из logcat; `logcat -c` и `am kill`/`dumpsys` давали ложные сигналы; (3) **тест/AVD** — liveness `:demo` через `/proc`, не `dumpsys`; остановка только `:demo`, не instrumentation host `com.apptraverse.minimal`.
- **Инфра:** cross-process checkpoint gate (файл); log baseline по `background save finished subdir=<profile> result=0`; `DemoProcessExitReceiver` (debug); `waitForProfileStateOnDisk` для overlap.
- **Проверки (`apptraverse-wt-web5`, incremental Debug, 2026-11-01 ~21:18–21:35 UTC):**
  - **Headless + §W2 (12 exe):** **PASS**
  - **WASM (8766 / 8770 / 8785):** **PASS** — три browser smokes
  - **Android (`connectedDebugAndroidTest` 12/12):** **PASS** — ColdLoadLabel, EarlyBackgroundSave, InitialPublicationLabel, InitialUi, ListApplyInput, ModelThreadStartSync, **NewPidColdReload**, PendingSaveRace, backgroundSaveSurvivesColdReload, overlappingBackgroundSave, resumeWhileSave, sameProcessResume (два подряд полных прогона **12/12** после fix stop-wait; один прогон под двойной нагрузкой AVD — **10/12**, ListApply/NewPid UI timeout — **причина:** эмулятор/очередь UI после back-to-back suite, не продукт).
- **Linux/macOS/iOS:** **NOT RUN**

**NEXT (historical):** **P3.T3.L3** — см. §P3.T3.L3.

---

<a id="p3t3l2--linux-minimalapp-ui-commands-2026-11-02--pass"></a>

## P3.T3.L2 — Linux MinimalApp UI commands (2026-11-02) — PASS

**Base (lease):** **`42ac43c4b70a0b51c37f00d94d44f598a98441bd`** (P3.T3.L1).

**Scope:** GTK input → neutral presenter API → `ModelObjectProxy` → model Events → publication → UI mirror → GTK sync (**без** прямой мутации model/mirror из callbacks). Persistence — **L3**, не начиналась.

**Маршрут команд:**
- **List row:** `GtkListBox::row-selected` → `LinuxListContainerPresenter::SubmitListRowSelectionChangedFromUi` → `UiListContainerPresenter` / proxy → `SetUiListSelectionEvent` → incremental publication → `SyncFromMirror` (`notify_list_selection_change`, `gtk_list_box_select_row` под `applying_mirror_sync_`).
- **Apply / Open Detail:** `GtkButton::clicked` → `LinuxPushButtonPresenter::NotifyClickedFromUi` → `UiPushButtonPresenter` / proxy → model `HandlePushButtonClicked` / Events (Apply → title **Applied**, disable Apply, clear selection; Open Detail → `MinimalOpenAuxiliaryWindowEvent` → auxiliary desktop window через structural publication).

**Runtime E2E (`wsl -d Ubuntu`, `linux_minimal_app`, WSLg, 2026-11-02):** Row B selection после publication; Apply → title **Applied** + disabled Apply + cleared selection; Open Detail → auxiliary **Detail Panel** window — пошагово после каждой publication.

**Регрессии (`apptraverse-wt-web5`, incremental Debug, 2026-11-02):**
- **Headless + §W2 (12 exe):** **PASS**
- **WASM ×3:** **PASS** (catalog serve **8785**)
- **Android `connectedDebugAndroidTest` 12/12:** **PASS** (1-й прогон Gradle **FAIL** без детализации теста; повтор **12/12** **PASS**)

**NEXT (historical):** **P3.T3.L3** — выполнено, см. §P3.T3.L3.

---

<a id="p3t3l3--linux-minimalapp-persistence-lifecycle-2026-11-02--pass"></a>

## P3.T3.L3 — Linux MinimalApp persistence / lifecycle (2026-11-02) — PASS

**Base (lease):** **`a1049d41522412f312203f3449258b1debc98b8c`** (P3.T3.L2).

**Scope:** общий `RunApplicationModelSession` + `DirectoryDomainStorage` / `PersistMinimalApplication` (model thread); GTK publication и teardown на UI thread через `LinuxApplicationHost`; **без** отдельного persistence-формата или второй модели.

**Маршрут сохранения:**
- **Shutdown persist:** `delete-event` → `RequestStop` → drain model queue → `hooks.persist` / `PersistMinimalApplication` на **model thread** (`SavePresentedWindow` + `SaveRoot` в profile `--state-dir`).
- **Cold load:** `LoadApplication` + `BootstrapMinimalApplicationProfileIfAbsent` (no-op при непустом profile) → initial publication → GTK mirror; маркеры `MINIMAL_APP_PROFILE_BOOTSTRAP` / `MINIMAL_APP_LOADED_FROM_PROFILE`.
- **UI после reload:** presenter → proxy → Event → publication → `SyncFromMirror` (тот же маршрут, что L2).

**E2E (`wsl -d Ubuntu`, отдельный temp profile, WSLg):** `tools/linux_minimal_app_l3_smoke.py` — фаза **l3_save** (Row B + Apply через presenter API, shutdown persist) → новый процесс **l3_cold_verify** (Applied mirror, Row A selection, `MINIMAL_APP_SHUTDOWN_STOPPED`).

**Регрессии (`apptraverse-wt-web5`, incremental Debug, 2026-11-02):**
- **Headless + §W2 (12 exe):** **PASS**
- **WASM ×3 (8766 / 8770 / 8785):** **PASS**
- **Android `connectedDebugAndroidTest`:** прогоны 1–4 на нагруженном AVD — **11/12**, **11/12**, **9/12**, **7/12** (UI/logcat timeouts, см. историю §P3.T3.A); **cold boot AVD + 5-й прогон — 12/12 PASS**

**NEXT:** **P3.T3.M** — macOS MinimalApp backend.

**Lifecycle fix (2026-11-02, follow-up на `4bcc34e…`):**
- **GTK unload:** list/button presenters — disconnect signals, `gtk_widget_destroy` только при `GTK_IS_WIDGET`; window presenter отключает `delete-event` до destroy (без `gtk_container_remove` после возможного unref родителем).
- **Shutdown hang:** `LinuxApplicationHost` — `g_main_context_wakeup` после exit model thread.
- **Native close:** `HandleMinimalAppDesktopWindowClose` (common) — main → `RequestStop`; auxiliary → `MinimalCloseAuxiliaryWindowEvent` через proxy (Linux + Windows).
- **L3 E2E:** automation через реальные GTK widgets (`gtk_list_box_select_row`, `gtk_button_clicked`, `gtk_window_close`); smoke добавляет **`l3_detail_close`** (native close Detail без остановки main до проверки mirror).

**Повторные проверки (2026-11-02):** Linux L3 smoke **PASS**; Headless + §W2 **PASS**; WASM ×3 **PASS**; Android **12/12 PASS** (1-й прогон).

---

<a id="windowcatalog-required-links-fatal-2026-11-02"></a>

## WindowCatalog — required links fatal (2026-11-02)

**Scope:** убраны silent `return` на обязательных связях после `PrepareWindowCatalogForRun`; **без** изменения модели/S0/команд.

**Prepare:** проверка `list→catalog` и `catalog` на chrome-кнопках (Add/Open/Delete/Language).

**Команды:** `CatalogOpenButton` / `CatalogLanguageButton` / `CatalogListContainer::HandleListRowActivated` — `WriteFatalStderr` + abort при отсутствии list/locale/catalog. **Сохранены** допустимые no-op: пустой selection (`OpenSelected`), закрытое окно, out-of-range row (`OpenRow`).

**Presenters:** Windows `CatalogListContainerPresenter` и Headless adapter — fatal при отсутствии `catalog` или `model_proxy` (не пустой selection).

**AddWindow:** `assert(false)+return` заменены на fatal; `LoadRoot` / `CloneCanonicalRoot` вне assert.

**Проверки:** `apptraverse_window_catalog_headless_test`, `apptraverse_window_catalog_win32_acceptance_test`, `apptraverse_window_catalog_model_test` **PASS**; §W2 **PASS**; Linux L3 smoke **PASS**; WASM ×3 **PASS**; Android **12/12 PASS** (1-й прогон).

**Follow-up (2026-11-02):** граница инвариантов — **`PrepareWindowCatalogForRun`** (list/locale/chrome/catalog refs) + **`ApplicationUiSession::InitializePresenters`** (`model_proxy` до UI-команд). Удалены повторные fatal в command path (Open/Language buttons, `CatalogListContainer::HandleListRowActivated`, Win/Headless list presenter `Submit*`); lazy **`Load()`** на list/locale/catalog сохранён. Fatal **AddWindow** Load/Clone — без изменений.

**Follow-up (2026-11-02, pass 2):** **`LoadedCatalog`** — убрана повторная проверка `catalog.is_valid()` (chrome `catalog` ref на prepare); **`Load()`** только при `!catalog.is_loaded()`. **`AddWindow`** — убран fatal «missing window_prefab» (ссылка задаётся в **`BuildWindowCatalogGraph`**); fatal **`LoadRoot`** / **`CloneCanonicalRoot`** сохранены.

**Проверки (pass 2, incremental Debug, `apptraverse-wt-web5`):** WindowCatalog headless + win32 acceptance + model **PASS**; §W2 **PASS**; Linux L3 smoke **PASS** (WSL, существующий `linux-x64-debug` exe); WASM ×3 **PASS** (`wasm-ninja-debug`, serve **8766** / **8770** / **8765**); Android **12/12 — BLOCKED** (1-й прогон `connectedDebugAndroidTest`: CMake `apptraverse_surfaces` duplicate target @ android `native/CMakeLists.txt:38`, APK не собран; повтор не запускался).

**Android surfaces CMake (2026-11-02):** **`add_subdirectory(${APPTRAVERSE_ROOT})`** поднимает статический **`apptraverse_surfaces`** (root `CMakeLists.txt`); JNI shared target в `examples/surfaces_demo/android/native/CMakeLists.txt` совпадал по имени → CMP0002. **Fix:** CMake target **`apptraverse_surfaces_jni`**, `OUTPUT_NAME` / `System.loadLibrary` без изменений; Gradle `targets "apptraverse_surfaces_jni"`. **12/12:** `examples/minimal_app/android` `connectedDebugAndroidTest` на **`Aether_NDK_Smoke_x86_64`** — **PASS** (1-й прогон, 0 failed). Surfaces demo APK **build PASS** (instrumented tests в модуле нет). §W2 / Headless / Linux / WASM — **NOT RUN** (только Android-native CMake + Gradle).

**Follow-up (2026-11-02, pass 3):** **`Apply(SetWindowCatalogSelectionEvent)`** — убран `if (list)`; перед записью **`selection_index`**: **`list.Load()`** (replay/cold). **`RefreshChromeButtonEnabledState`** — убраны `if (open_button|delete_button)`; **`buttons.open_button.Load()`** / **`delete_button.Load()`** перед **`MaterializeEnabled`**. Пустой selection без изменений. **Prepare** по-прежнему грузит list и chrome buttons до UI.

**Проверки (pass 3):** WindowCatalog headless + win32 acceptance + model **PASS**; Linux L3 **PASS**; WASM ×3 **PASS**; Android **12/12 PASS** (1-й прогон).

**Follow-up (2026-11-02, pass 4):** **`FindListHolderByIdentity`** — убраны `if (!catalog.list)` и `if (!row) continue`; **`catalog.list.Load()`**, для каждой строки **`row.Load()`** перед сравнением **`obj_id`** (not found → **`nullptr`**). **`FindHolderForWindow(ObjId)`** — **`list.Load()`**, **`row.Load()`** на итерации; match по **`window.id()`** без требования loaded (closed/lazy окно). CanApply / Open no-op без изменений.

**Проверки (pass 4):** WindowCatalog headless + win32 + model **PASS**; §W2 **PASS**; Linux L3 **PASS**; WASM ×3 **PASS**; Android **12/12 PASS** (1-й прогон).

**Follow-up (2026-11-02, pass 5 — row index / open path):** аудит **`RowIndexForHolder`**, **`OpenRow`**, **`SubmitSelectionFromUi`**. **`row_index >= rows.size()`** — допустимый no-op / сброс selection (UI). **`!rows[i]`** / **`!rows[row_index]`** — убраны: при cold/replay строка может быть **valid-but-unloaded**; вместо skip — **`list.Load()`** + **`row.Load()`**. **`RowIndexForHolder`**: not-found → **`rows.size()`** (как раньше). **`OpenSelected`/`DeleteSelected` `!selected`** — без изменений (пустой selection).

**Проверки (pass 5):** WindowCatalog headless + win32 + model **PASS**; §W2 **PASS**; Linux L3 **PASS**; WASM ×3 **PASS**; Android **12/12 PASS** (1-й прогон).

**Follow-up (2026-11-02, pass 6):** **`ObjPtr::operator bool` = valid ∧ loaded** → **`if (!row)`** в **`CanApply(AddWindow…)`** пропускал valid-but-unloaded дубликат; заменено на **`row.is_valid() && row.id() == event.holder.id()`** без **`Load()`** (применимость Event не материализует модель). **`Apply(Remove…)`** — поиск/сброс selection по **`ObjId`**, **`list.Load()`**; адресное **`&*row`** не требуется. **`MaterializeCatalogStrings`** — **`list.Load()`**, **`row.Load()`** перед **`MaterializeHolderListText`** (locale после cold/replay).

**Проверки (pass 6):** WindowCatalog headless + win32 + model **PASS**; §W2 **PASS**; Linux L3 **PASS**; WASM ×3 **PASS**; Android **12/12 PASS** (1-й прогон).

**Audit (2026-11-02, pass 7 — save/restore rows):** **`SaveWindowCatalogPersistentSnapshotImpl`** / **`RestoreLoadedCatalogWindows`** — **код без изменений**. Обход **`catalog.list->rows`** не использует **`if (!row)`**; **`list->`** / **`*row`** на **`ObjPtr`** вызывают **`Load()`** (aether **`operator->`/`operator*`**). **Restore:** **`if (!holder.window.is_loaded()) continue`** — только **закрытое** child-окно (lazy prefab link сохранён), не пропуск holder-строки. **Prepare:** **`list.Load()`** + **`MaterializeCatalogStrings`** (pass 6 **`row.Load()`**) до **`RestoreLoadedCatalogWindows`**. Прямые вызовы Restore/Save из тестов опираются на implicit Load при dereference.

**Проверки (pass 7):** код не менялся — полная backend-регрессия **NOT RUN** (scope pass 7 = audit-only).

**Follow-up (2026-11-02, selected ObjPtr):** **`ObjPtr::operator bool` = valid ∧ loaded** ломал **`Apply(SetWindowCatalogSelectionEvent)`** (`event.holder ?` → **`selection_index == -1`** при valid-but-unloaded), **`OpenSelected`** / **`DeleteSelected`** (`if (!selected)` no-op). **Fix:** **`event.holder.is_valid()`** / **`selected.is_valid()`** + **`Load()`** перед dereference; пустой selection без изменений. **Тест:** **`TestCatalogUnloadedSelectedPtrOpenDeleteAndApplyIndex`** — штатный **`selected.Reset()`** после cold-path SelectHolder (RAM; тот же контракт ObjPtr после replay).

**Проверки:** WindowCatalog model + headless + win32 + embedded overlay **PASS**; §W2 **PASS**; Linux L3 **PASS**; WASM ×3 **PASS**; Android **12/12 PASS** (1-й прогон).

**Cold/replay selection (2026-11-02):** **Headless E2E** — list presenter **`SubmitListRowSelectionChangedFromUi(1)`** → proxy → **`SubmitSelectionFromUi`** → save → **`LoadApplication` + Prepare**: **`selection_index == 1`**, **`selected.id()`** согласован с row[1], Open/Delete **enabled**; пустой selection после **`SelectHolder({})`** → **-1**, кнопки disabled. **Model** — disk cold + **`TryReplayFromBase`**: **`selection_index` / `selected.id()`** до dereference; **`OpenSelected`** если **`!selected.is_loaded()`** (ветка на прогоне **не срабатывала**). **Вывод:** после **`LoadApplication`** **`selected` уже loaded** (`cold_selected_unloaded_before_prepare=0`); valid-but-unloaded в этом cold path **не воспроизведён** — контракт **`Reset()`**-теста остаётся отдельным. **Код продукта:** без изменений (db8589a достаточен).

**Проверки (cold selection audit, 2026-11-02 повтор):** headless + model + overlay + win32 **PASS**; §W2 **PASS**; Linux L3 **PASS**; WASM ×3 **PASS** (8766/8770/8765); Android **12/12 PASS** (1-й прогон). **Lease push:** `db8589a509f73a1e2d7128172929c8253f456a61`.

**Cold/replay selection probe (2026-11-02, pass 2):** сразу после **`LoadApplication` / `LoadPersistedWindowCatalog`** (до **`list.Load()`** в headless) — **`selected.is_valid()`**, **`selected.id()`** совпадает с persisted holder; **`!selected.is_loaded()`** **не наблюдался**; **`RefreshChromeButtonEnabledState`** → Open/Delete **enabled** в модели до Prepare. **Replay RAM** после **`TryReplayFromBase`** — то же. **Valid-but-unloaded `selected` естественно не возникает** на disk cold / replay; **`TestCatalogUnloadedSelectedPtrOpenDeleteAndApplyIndex`** (`Reset()`) — только контракт ObjPtr. **Продукт:** без изменений (**base `db8589a`**).

**Проверки (probe pass 2):** headless + model **PASS**; overlay / win32 / §W2 / Linux / WASM / Android — **NOT RUN** (только тесты).

**Lifecycle / prepare audit (2026-11-02):** **`RunApplicationModelSession`**: **`LoadApplication`** → **`PrepareWindowCatalogForRun`** → **`runtime.Run`** (initial + incremental publication) → shutdown **`SaveWindowCatalogPersistentSnapshot`**. **Prepare:** `LoadPresentedWindow(main)` → chrome **`Load()`** (cache no-op if loaded) → **`MaterializeCatalogStrings`** (chrome + все holder list text через **`row.Load()`**) → **`RefreshChromeButtonEnabledState`** → **`RestoreLoadedCatalogWindows`** (только **`holder.window.is_loaded()`**: `LoadPresentedWindow` + title materialize + notifiers; **закрытые** окна **skip** — lazy prefab link сохранён). **Save:** primary_text Save/Reset per row; **`SavePresentedWindow`** только для loaded child; catalog **`Save()`**. **Fix:** убран второй обход **`list->rows`** с повторным **`MaterializeHolderListText`** после **`MaterializeCatalogStrings`** (дублирование работы, не повторный Load). **Restore**-второй проход по **открытым** окнам — нужен (presentation после cold).

**Проверки (lifecycle):** headless + model + overlay + win32 **PASS**; §W2 **PASS**; Linux L3 **PASS**; WASM ×3 **PASS**; Android **12/12 PASS** (1-й прогон). **Lease push:** `dd755544a8c761a457bef3ffbd10d13d4fa4f35a`.

**MinimalApp embedded BACKING/USER (2026-11-02):** **Bootstrap prod** — `BootstrapMinimalApplicationProfileIfAbsent` пишет distill в **USER** (`RunApplicationModelSession` + `DirectoryDomainStorage`). **Embedded slice** — `TestMinimalAppEmbeddedBackingUserHeadlessE2e` в **`apptraverse_minimal_app_headless_test`**: runtime distill **`BuildHeadlessMinimalApplicationGraph`** → **`EmbeddedDistilledGraph`** (тот же SaveRoot/bases pipeline, что build-time exporter) → **`RunEmbeddedApplicationModelSession`**: session 1 — presenter **`SubmitFrameFromNative`** → proxy → publication → **`PersistMinimalApplication`** в **USER-only** dir; **BACKING** snapshot неизменен; session 2 — тот же USER, fresh embedded BACKING, **без** bootstrap distill — geometry **140/90/800/600** в prepare. **USER** = динамика; **BACKING** = константные слои. Продукт/backends не менялись.

**Проверки (MinimalApp embedded E2E):** `apptraverse_minimal_app_headless_test` **PASS**; WindowCatalog headless/model/overlay/win32 **PASS**; §W2 **PASS**; Linux L3 **PASS**; WASM ×3 **PASS**; Android **12/12 PASS** (1-й прогон). **`apptraverse_minimal_app_embedded_export_test`** — **NOT RUN** (Win CTest; compile-time header exporter). **Lease push:** `a7534415875f18ef6f3ab9eb5a8a8650c6908d3c`.

**MinimalApp build-time embedded supply (2026-11-02):** **Первый отказ:** `apptraverse_export_minimal_distilled_graph` **exit -1073741819** (access violation) при **`StabilizeDistilledPersistentSnapshot` → `SaveDistilledRoot`** на **`BuildMinimalApplicationGraph`** (воспроизводится и с RAM-only, и с directory roundtrip); **без Stabilize** export **exit 0**. **Не** подтверждено, что Stabilize сам по себе «ломает» surfaces (surfaces export **PASS**); для MinimalApp export граница как у **`export_window_catalog_distilled_graph`**: **`FinalizeDistilledGraph` + `SaveDistilledRoot`**, temp **`DirectoryDomainStorage` → `LoadDirectoryDomainStorageIntoRam` → `WriteEmbeddedDistilledSupplyHeader`**. **Header:** CMake `embedded_distilled_minimal_app_supply.h` (~38 KB) из **`BuildMinimalApplicationGraph`**; consumer **`apptraverse_minimal_app_embedded_export_test`** — cold load **`embedded_distilled::minimal::kEmbeddedGraph`**, title/locale refs, USER overlay persist. **Тест:** ожидания cold load + noop USER при load-only.

**Проверки (build-time supply):** exporter + **`apptraverse_minimal_app_embedded_export_test`** **PASS**; headless + WindowCatalog + §W2 + Linux L3 + WASM ×3 + Android **12/12 PASS** (1-й прогон). **Lease push:** `4ab24722396b83ab1f090d17916f179900f65e8b`.

---

<a id="p3t3l1--linux-minimalapp-initial-gtk-screen-2026-11-02--pass"></a>

## P3.T3.L1 — Linux MinimalApp initial GTK screen (2026-11-02) — PASS

**Base (lease):** **`7c89f3e5bacb36e1862eb19b250e3259285d91ff`** (P3.T3.L0 planning).

**Scope:** initial publication → `ApplicationUiSession` → GTK3 chrome (**без** UI-команд и **без** persistence slice). Единая модель `minimal_app_common` + `minimal_app_graph_builder`; **не** вторая прикладная модель.

**Библиотека:** `apptraverse_ui_linux` — `LinuxApplicationHost` (model thread + `g_idle_add` publication на UI thread), `LinuxDesktopWindowPresenter` / `LinuxListContainerPresenter` / `LinuxPushButtonPresenter`, `LinuxPresentationHost`.

**Потребитель:** `examples/minimal_app/linux/` — `minimal_app_main.cpp`, `minimal_app_graph_linux.cpp`, `minimal_app_lifecycle.cpp` → target **`linux_minimal_app`**.

**Runtime (WSL `Ubuntu`, `build/linux-x64-debug`, 2026-11-02):** **`linux_minimal_app`** — WSLg/GTK окно **«Minimal App»**, list rows **Row A** / **Row B**, кнопки **Apply** / **Open Detail** (labels из mirror; clicks **не** маршрутизируются — L2).

**Регрессии (`apptraverse-wt-web5`, incremental Debug, 2026-11-02):**
- **Headless + §W2 (12 exe):** **PASS**
- **WASM:** `wasm_minimal_app_browser_smoke.py`, `wasm_web_presenter_lifecycle_test.py`, `wasm_window_catalog_browser_smoke.py` (serve **8785**) — **PASS**
- **Android `connectedDebugAndroidTest` 12/12** (`Aether_NDK_Smoke_x86_64`, Android 14) — **PASS**

**NEXT (historical):** **P3.T3.L2** — выполнено, см. §P3.T3.L2.

---

<a id="p3t3l0--linuxgtk-toolchain-probe-2026-11-02--planning-only"></a>

## P3.T3.L0 — Linux/GTK toolchain probe (2026-11-02) — planning only

**Worktree:** `apptraverse-wt-web5`. **Base lease:** **`6f5298e62f8311ea6a593ad8830102df4bcad0c4`** (Android **12/12** + Headless/§W2/WASM×3 **PASS** на том SHA; Linux-код **не** начинался).

**Probe:** read-only; без установки пакетов и без изменения окружения.

| Check | Result |
| --- | --- |
| Default `wsl` (implicit distro) | **BLOCKED** — `execvpe(/bin/sh) failed`, `getpwuid(0) failed`, `/etc/fstab` mount failures, Windows PATH translate errors |
| `wsl -d Ubuntu` | **OK** — WSL2 x86_64 |
| `gcc` / `g++` | **OK** — Ubuntu 13.3.0 |
| `cmake` / `ninja` | **OK** — 3.28.3 / 1.11.1 |
| `pkg-config gtk+-3.0` | **OK** — 3.24.41; `libgtk-3-dev` installed |
| `DISPLAY` / `WAYLAND_DISPLAY` (probe shell) | **OK** — `:0` / `wayland-0` |
| PyGObject `Gtk.Window()` create/destroy | **OK** — `gtk_init_ok` |
| `xvfb-run` | **NOT RUN** — not on PATH (`which` → missing) |
| `gtk3-demo` | **NOT RUN** — binary not installed |
| Configure/build MinimalApp Linux target | **NOT RUN** — нет target/исходников в репозитории |
| Re-run Headless / §W2 / WASM / Android this task | **NOT RUN** — prior **PASS** at `6f5298e` unchanged |

**Документация:** scope **P3.T3.L1–L3** — [PLAN §P3.T3.L](./PLAN.md#p3t3l--linuxgtk-minimalapp).

**NEXT (historical):** **P3.T3.L1** — выполнено, см. §P3.T3.L1.

---

<a id="p3t3a2r1--presented-list-row-labels-2026-10-31-pass"></a>

## P3.T3.A2R1 — Presented list row labels (2026-10-31) — PASS

**Base (lease):** `82006f4b3afb850464bd22e48d62477bf9c9440e` (**P3.T3.A2 PASS**).

**Граница materialization:** для **presented visible list** (`SyncFromMirror` / native row bind) каждая запись `container->rows` обязана иметь `primary_text`, доступный через `Load()` на UI mirror (`ResolvePresentedListRowPrimaryText` / `ResolvePresentedListRowPrimaryTexts` в `ui_list_container`). Valid-but-unloaded `ObjPtr` не ошибка до `Load()`; `Declare`/recovery по ObjId убраны. Lazy/closed окна вне этой границы (headless auxiliary lazy — без изменений).

**Изменено:** Android/Win/Web list presenters вызывают общий resolver; удалены Android `Declare`+`continue` и Win silent `continue`. Тесты: `ui_control_materialization_test` — две строки сохраняют текст и индекс; precondition для missing link.

**Проверки (incremental Debug, 2026-10-31):** **PASS** — Headless (`minimal_app_headless`, `window_catalog_headless`, Object Core subset incl. `ui_control_materialization`), Android 4 instrumented, WASM (incremental build `web_minimal_app` + lifecycle + catalog smokes на HEAD). **Windows — не полный PASS:** `apptraverse_window_catalog_win32_acceptance_test` — **FAIL** @ iconic wait после `SW_MINIMIZE` (диагностика: `IsWindow=1`, `IsIconic=0`, `IsVisible=1`, `showCmd=1`; PID/HWND в stderr); один прогон §W2 batch дал **PASS** для того же exe — **наблюдение**, не объяснение FAIL. Остальные §W2 + `minimal_app_win32_acceptance` — **PASS**.

**Iconic FAIL (исходный):** гипотеза — гонка второго Open `after_incremental` (`SW_RESTORE`) с тестовым `SW_MINIMIZE`; **подтверждена только** при stderr `iconic_wait_failed` (`IsIconic=0`, `showCmd=1`). Повторные PASS (в т.ч. ×3 на `8fb0cf5`) — не доказательство причины.

**Синхронизация второго Open:** foreground `== child2` **не** годится (после первого Open child2 уже foreground). Test-only счётчик `APPTRAVERSE_WC_FOCUS_APPLY_COUNT_FILE` в demo `after_incremental` (без env — no-op); acceptance ждёт count `1` → второй Open → count `2`, затем minimize / третий Open / restore.

**Negative test:** subprocess `missing_primary_text_fatal`.

**Проверки (2026-10-31):** acceptance ×5 + `ui_control_materialization` после counter-sync; Android/Web не перезапускались (demo env-hook only).

**NEXT:** **P3.T3.A** — Android lifecycle/save (не начинать).

---

<a id="p3t3a2--android-minimalapp-input-e2e-2026-10-31-pass"></a>

## P3.T3.A2 — Android MinimalApp input E2E (2026-10-31) — PASS

**Base (lease):** `9db1fcdc6a6bc2a24cbaefef626c9ac6754951d3` (**P3.T3.A1R3 PASS**).

**Scope:** один E2E на `Aether_NDK_Smoke_x86_64` (Android 14): после initial publication — UiAutomator tap **Row B** → mirror selection; tap **Apply** → activity title **Applied**, Apply disabled, selection сброшен. Без прямой мутации model/mirror из Java/JNI и без второй publication-очереди.

**Маршрут команды:** `View.OnClickListener` (UI thread) → JNI `nativeOnListSelection` / `nativeOnPushButtonClick` → `AndroidListContainerPresenter::HandleRowClickFromJava` / `AndroidPushButtonPresenter` → `UiListContainerPresenter::SubmitListRowSelectionChangedFromUi` / `UiPushButtonPresenter::NotifyClickedFromUi` → `ModelObjectProxy::Invoke` → model thread Event → incremental publication → `NativeUiBridge.onNativePublication` (main looper) → `ApplicationUiSession::ApplyStructural` → `OnModelChanged` → `SyncFromMirror` / `SyncTitleFromMirror`.

**Исправление (library):** при повторном `SetPresentationRoot` (новый Activity/content root в том же процессе) presenters держали global refs на снятые views — `AndroidApplicationHost::SetPresentationRoot` вызывает `ApplicationUiSession::RemountPresentation()` (`UnloadPresenters` + `InitializePresenters`, те же presenter identities). Shell: `Theme.Material.Light` (action bar), чтобы `setActivityTitle` от window presenter был виден в UiAutomator.

**Тест:** `ListApplyInputInstrumentedTest` + прежние ColdLoadLabel / InitialPublicationLabel / InitialUi — **4/4 PASS**.

**Регрессии (incremental Debug, 2026-10-31):** §W2 восемь + presenter_load_order, application_ui_session, application_model_runtime, application_contract_headless, checkpoint cycle/rehydration, model_persistence, object_clone, object_origin_facet, origin disk roundtrip/matrix; WASM MinimalApp Web5R1 + presenter lifecycle + WindowCatalog smoke — **PASS**.

**Не начиналось:** полный Android lifecycle/save, Linux/macOS/iOS.

**NEXT:** **P3.T3.A** — Android lifecycle/save slice (не начинать).

---

<a id="p3t3a1r3--graph-load-architecture-2026-10-31-pass"></a>

## P3.T3.A1R3 — Graph load architecture (2026-10-31) — PASS

**Base (lease):** `23b6e8e66dc9a7c2da95fe065089e153868c8ac4` (**P3.T3.A1R2 IN_PROGRESS**). Контроль: `ad95a14`, `2342029`.

**Старая проблема:** v6/v6r1/v6r2 добавляли в `Domain` очередь `pending_stored_layers` / `loading_root_ids`, завершение в деструкторе `LoadRootScope` после вычисления результата и 8-проходный `HydrateInjectedPublicationGraph`. Метки `cycle_detector` переживали освобождённый экземпляр с тем же ObjId (новый объект пропускал слои), а `Serializer<ObjPtr>` сбрасывал `cached_` до `LoadRoot`, поэтому повторное чтение ссылки пересоздавало живой объект. Под cdb подтверждён UB в v6 `CompletePendingStoredLayersFor` (итератор `unordered_map` через reentrant `LoadRoot`) — ~30% `0xC0000005` headless. v6r2 CPM фактически не применялся (patch cache по именам), поэтому FAIL @561 на v6r2 **не** атрибутирован.

**Новый контракт:** один `DomainGraph::LoadRoot` строит весь eager граф (циклы разрешаются через `Find` на частично построенный экземпляр); корневой `ae::Ptr` владеет графом, `Domain` — только weak. Метки загрузки принадлежат экземпляру: `LoadRoot` перед `ConstructObj` снимает метки ObjId. Ошибка обязательного слоя — `LoadRoot` error → `InitialPublicationLoadStage::LoadRoot`, `root` пуст, presenters не создаются. Structural apply перечитывает слои в живые mirror-экземпляры (`LoadExistingObject` с проверкой каждого), identity presenters/controls сохраняется. Обычный Load держит `cached_` живого экземпляра → unsaved changes и identity сохраняются. Lazy prefab/closed окна не загружаются.

**Заменено:** удалены `aether-objects-cold-load-root-flush-v6`, `-reset-object-load-state-v6r1`, `-deferred-load-completion-v6r2`; добавлен **`aether-objects-graph-load-instance-identity.patch`** (`DomainCycleDetector::Remove`, `cycle_detector.Remove(obj_id)` в `LoadRoot`, `cached_` reset только для lazy/invalid). Цепочка — **15 patches** списком `APPTRAVERSE_AETHER_OBJECTS_PATCH_NAMES` в `cmake/aether_version.cmake`; `verify-aether-patch-chain.sh` читает его же. App: удалены `HydrateInjectedPublicationGraph`, `FlushIncompleteStoredLayers` в `LoadApplication`, `MandatoryLayersPending` / `FinalFindRoot` / `LoadExistingObject` stages, `InitialPublicationPayloadRemoveLayer`, `FormatDomainStorageClasses`.

**Тесты (в существующих):** `minimal_app_headless` — cold load: общие ссылки `row_list`, цикл window↔presenter, presenter→button identity, label; auxiliary lazy не загружен; auxiliary list selection presenter→proxy→Event→publication, mirror==model, identity presenter. `application_ui_session` — corrupt mandatory layer и отсутствующий обязательный объект отказывают до presenters. `checkpoint_load_rehydration` — saved → unsaved → повторный LoadRoot (identity + change).

**Проверки (incremental Debug, `apptraverse-wt-web5`, 2026-10-31) — PASS:**
- `verify-aether-patch-chain.sh` — 15 patches на pin `79283b6`; Win/WASM/Android CPM `aether-objects/2915` содержат `cycle_detector.Remove(obj_id)`, без `pending_stored_layers`.
- Win (`win64-ninja-msvc-debug`): §W2 восемь (win_ui_command_routing, win_ui_native_push_button, win_application_host, minimal_app_win32_acceptance, window_catalog_win32_acceptance, minimal_app_headless, window_catalog_headless, ui_control_materialization), presenter_load_order, application_ui_session, application_model_runtime, application_contract_headless, checkpoint_load_cycle_persistence, checkpoint_load_rehydration, model_persistence, object_clone, object_origin_facet; CTest `apptraverse_object_origin_disk_roundtrip_test` + `apptraverse_object_origin_disk_matrix`. Повторы: minimal_app_headless 20/20, window_catalog_headless / checkpoint_load_rehydration / minimal_app_win32_acceptance 10/10. Сборки `win32_minimal_app`, `win32_minimal_app_load_only`.
- WASM (`wasm-ninja-debug`): `wasm_minimal_app_browser_smoke.py` (8766), `wasm_web_presenter_lifecycle_test.py` (8770), `wasm_window_catalog_browser_smoke.py` (8785).
- Android (`Aether_NDK_Smoke_x86_64`, Android 14, `connectedDebugAndroidTest` без clean): `ColdLoadLabelInstrumentedTest`, `InitialPublicationLabelInstrumentedTest`, `InitialUiInstrumentedTest` — 3/3.

**Не прогонялось:** полный CTest, `win64-vs2022`.

**NEXT:** **P3.T3.A2** — Android input (не начинать).

---

<a id="p3t3a1r2--deferred-load-completion-catalog-smoke-2026-10-31"></a>

## P3.T3.A1R2 — Deferred load completion + catalog smoke (2026-10-31) — IN_PROGRESS

**Base (lease):** `751c14e5d8aa645feb47b38783a2a6ec646c1b4d` (**P3.T3.A1R1 PASS**).

**Дефекты до исправления:**
- `HydrateInjectedPublicationGraph` / `TryLoadInitialPublication` возвращали успех после 8 проходов при непустой `pending_stored_layers` (обязательная загрузка не завершена).
- `CompleteAllPendingStoredLayers` делал `snapshot` + `layers.clear()` — при `TryLoadStoredLayer` ошибке mid-loop необработанный хвост pending терялся вместе с очередью.
- `TryLoadStoredLayer` — `assert(layer && layer->load)` на повреждённых/неизвестных class id в corrupt publication tests (abort вместо controlled fail).
- Catalog smoke (`tools/wasm_window_catalog_browser_smoke.py`) был приведён к polling/`data-apptraverse-control-id` и не совпадал с опубликованным shell adapter (`#catalog-title`, `data-control-id`, пустой список, 4 кнопки).

**Fix (shipping):**
- **`aether-objects-deferred-load-completion-v6r2.patch`** (поверх v6+v6r1): reentrant-safe `CompleteAllPendingStoredLayers` (work snapshot без wipe pending); `TryLoadStoredLayer` fail-closed без assert для missing factory (defer, не abort).
- **App:** `TryLoadInitialPublication` — после финального `FlushIncompleteStoredLayers` отказ при `HasIncompleteStoredLayers()` → `MandatoryLayersPending`; helpers `InitialPublicationPayloadReplaceLayerBytes` / `RemoveLayer` / `RemoveObjectLayers`; regression tests в `application_ui_session_test` (corrupt deferred layer + mandatory incomplete до presenters); cached-state rehydration через повторный `LoadRoot` same ObjId в `checkpoint_load_rehydration_test`.
- **Catalog smoke:** восстановлен контракт P3.2a (`window_catalog_shell.html`), без `wait_for_timeout` polling.

**Æther patches (16, без diag):** 14 baseline + **v6** + **v6r1** + **v6r2**; `cmake/scripts/verify-aether-patch-chain.sh` — `work.emplace_back` + queue fix.

**Проверки (incremental Debug, `apptraverse-wt-web5`, 2026-10-31):** **PASS** — `apptraverse_application_ui_session_test` (в т.ч. corrupt/mandatory publication до presenters), `apptraverse_checkpoint_load_rehydration_test`, subset §A1R1 Win/Headless/Object Core (ui_session, checkpoint rehydration, model_persistence, application_contract_headless, application_model_runtime, object_clone). **FAIL / не зелёный полный §A1R1:** `apptraverse_minimal_app_headless_test` — `TestMinimalAppAuxiliaryCloseOpenHeadless` @561 (`UiAuxiliaryRowList` selection sync после structural apply; **воспроизводится только с v6r2 queue fix**, на `751c14e` **PASS**); `apptraverse_object_origin_disk_matrix` (disk roundtrip matrix, отдельная диагностика). **Не прогонялись в этой итерации:** Android 3 instrumented, WASM catalog serve `8785`, полный §W2 восемь.

**Отложено (не в shipping v6r2):** bool-API `Flush/Ensure/Complete*` и явный `LoadRoot` finalize до return — регрессия `minimal_app_headless_test` auxiliary structural sync; отдельный follow-up.

**PASS:** не выставлен — полный §A1R1 regression + Android/WASM smoke не зелёные.

**NEXT:** завершить A1R2 (auxiliary sync + оставшиеся прогоны) → затем **P3.T3.A2** (не начинать).

---

<a id="p3t3web2r2--web-dom-teardown-2026-10-30-pass"></a>

## P3.T3.Web2R2 — Web DOM teardown (2026-10-30) — PASS

**Base:** `de3c5a3a5f29eb22560952133cc82da9472d2cf2` (Web2R1).

**Дефекты до исправления (воспроизводились lifecycle regression):**
- `ReleaseOwnedEmvalHandle` → `Emval.decref` (нет в SDK **3.1.64**) — emval count не возвращался к baseline;
- `replaceWith(cloneNode(false))` на unbind — пустая DOM-копия control + живые listeners на исходном элементе.

**Fix:** `emscripten::val::take_ownership` в `web_ui_dom_em.cpp`; bind/unbind хранит функции на элементе (`__apptraversePushClick` / list click+dblclick), `OnUnload`: `removeEventListener` → `RemoveDomNode` → `ReleaseOwnedEmvalHandle`; window — shared `RemoveDomNode`.

**Teardown order:** multipass unload — children (button/list) до parent window; session dtor → `UnloadPresenters` descending.

**Regression:** `apptraverse_web_presenter_lifecycle_test` (WASM) + `tools/wasm_web_presenter_lifecycle_test.py` — capture client/button/list, destroy `ApplicationUiSession`, DOM absent, detached client без control-копий, stale events без proxy work, второй цикл без дубликатов, `Module.count_emval_handles()` → baseline (mount borrow неизменен).

**Проверки (incremental Debug):** lifecycle browser **PASS** (`serve_wasm.py` `:8770`); `wasm_minimal_app_browser_smoke.py` **PASS**; catalog initial smoke **PASS** (`8785`); §Web2R1 Win/Headless восемь регрессий + load-order/ui_session — **PASS**.

**Limits (unchanged):** full `WebApplicationHost` shutdown / browser persistence — не в scope R2.

**NEXT:** **P3.T3.Web3** — browser storage save + cold reload MinimalApp.

---

<a id="p3t3web2r1--mandatory-graph-contract-2026-10-30-pass"></a>

## P3.T3.Web2R1 — mandatory graph contract (2026-10-30) — PASS

**Base:** `d5ea6986ebaeed1714a9789ba1e1caeff50c557d` (исторический Web2 PASS сохранён).

**ReadyForPresentation:** `UiPushButtonPresenter` / `UiListContainerPresenter` — только `window_presenter->presentation_loaded`; Web child presenters без override; `WebDesktopWindowPresenter` без отдельного readiness (инварианты на `OnLoad` + `WebApplicationHost::HandlePublicationOnMainThread` mount).

**InitializeNewPresenters:** multipass сохранён; финальный `assert` заменён на `WriteFatalStderr` + `abort` для live presenter без `presentation_loaded`.

**Silent skips removed:** Web `SyncFromMirror` / geometry / title (обязательные links после load); `WireMinimalAppControlContext`, `MinimalApplyButton::HandlePushButtonClicked`, `MinimalAppList::HandleListRowActivated`; инварианты в `PrepareMinimalApplicationForRun`.

**E2E:** `wasm_minimal_app_browser_smoke.py` — после выбора строки `expect` `aria-selected` до Apply/activation.

**Проверки (incremental Debug):** `apptraverse_presenter_load_order_test`, `apptraverse_application_ui_session_test`; §W2 Win/Headless восемь регрессий — **PASS** (`window_catalog_win32_acceptance_test` — **PASS** на повторе); `win32_minimal_app` / `win32_minimal_app_load_only` — **build PASS**; WASM `web_minimal_app` + `wasm_minimal_app_browser_smoke.py` — **PASS**; `web_window_catalog_demo` + catalog initial smoke — **PASS** (`8785` serve).

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
| **P2.e.5R** — desktop Z-order S2 + Win32 restore | **`6c34008`**, §P2.e.5R | **PASS** (Debug/MinSizeRel: `apptraverse_window_catalog_model_test`); MinSizeRel manual Z-order **user confirmed PASS** |
| **P2.f.1** — WindowCatalog BACKING+USER overlay (MinSizeRel) | §P2.f.1 | **PASS** (после **P2.f.1R**) |
| **P2.f.1R** — W/V supply constants + MinSizeRel load_only embedded | §P2.f.1R | **PASS** — overlay test; MinSizeRel load_only два запуска **exit 0** |
| **P2.f.2** — `RunApplicationModelSession` (Load→Prepare→Run→Persist) | §P2.f.2 | **PASS** — `apptraverse_application_model_runtime_test`; demo targets build |
| **P2.f.3** — embedded BACKING/USER через `RunEmbeddedApplicationModelSession` | §P2.f.3 | **PASS** — `TestEmbeddedModelSessionRoundTrip` |
| **P2.f** — библиотечный lifecycle | §P2.f | **IN_PROGRESS** |
| **P3.1a** — WindowCatalog common core/actions (без Win32 graph) | §P3.1a | **PASS** — `surfaces_demo_window_catalog_common`; model + overlay tests |
| **P3.1b** — общая композиция graph builder; Windows/Headless adapters; Headless E2E | §P3.1b | **PASS** (win64-ninja-msvc-debug, локально) — `apptraverse_window_catalog_headless_test`; model + overlay tests; demos build. |
| **P3.2a** — Web initial WindowCatalog chrome (mirror + DOM) | §P3.2a | **PASS (исторически `2c73b99`)** — smoke с ручным CPM cache `7295` (origin-facet append снят). **Актуально на `9d4edb2` + полная patch chain: smoke FAIL** (`LoadApplication id=1`) — см. **P3.T1**. |
| **P3.2 (оставшееся)** — Web actions + persist | PLAN §P4 | **Перенесено в P4.C2/Web**; не DONE, не текущий NEXT |
| **P3** — библиотечный template + controls, семь backend | PLAN §P3.T | **IN_PROGRESS**; прежние PASS не подтверждают новую цель |
| **P3.T0** — границы и порядок template → catalog → admin | уточнение пользователя, 2026-10-28 | **DONE (doc-only)**; новых code PASS нет |
| **P3.T1** — воспроизводимый Æther/WASM patch | §P3.T1 ниже | **PASS** — **14 baseline** + graph-load-instance-identity = **15 patches** (§P3.T3.A1R3; v6/v6r1/v6r2 удалены); nested Obj deseri no-op **только** `ae::Obj`; contract/ui_session + Win regressions + WASM smoke **PASS** (historical) |
| **P3.T2a** — MinimalApp common builder + Headless session E2E | §P3.T2a ниже | **PASS** |
| **P3.T2b** — нейтральные Window/List/Button contracts | §P3.T2b ниже | **PASS** — `SetUiListSelectionEvent`; `apptraverse_ui_control_materialization_test` |
| **P3.T2** — минимальный template scope (T2a+T2b) | PLAN §P3.T | **PASS** |
| **P3.T3.H** — Headless MinimalApp lifecycle/save/cold-load | §P3.T3.H ниже | **PASS** |
| **P3.T3 / P3** — backend slices / библиотечный template | PLAN §P3.T | **IN_PROGRESS**; **NEXT P3.T3.W** |
| **P4.C1 / P4.C2** — WindowCatalog как тонкий потребитель / семь E2E | PLAN §P4 | **TODO** |
| **A1** — администрирование | PLAN §A1 | **TODO**, после P3 и P4 на всех семи backend |
| **P2.e** — graph Clone prefab | §P2.e | **IN_PROGRESS** (полный lazy closure **TODO**; holder/title builder) |
| **P2 (остальное)** | — | **IN_PROGRESS** |

**Application scope:** Windows WindowCatalog **manual PASS** (U2, **`8835d26`** era); Headless и Web initial render — только в scope P3.1b/P3.2a. **Полный Web E2E, общий template на семи backend и cross-backend profile compatibility не заявляются**. Embedded **9D/9E** — перенесено в **P2** (template), не отдельная очередь.

**Открыто:** атомарность профиля и corrupt-checkpoint hardening **не** в scope прототипа; **U5.1** exit code — факт **`8c863bc`**, failure branch в CTest не инжектировался.

---

## Перепланирование: сначала библиотечный template (2026-10-28)

Основание — прямое уточнение пользователя после отчёта **`2c73b99`**. Новый порядок: **P3.T1 → P3.T2 → P3.T3.H/W/Web/A/L/M/I → P4.C1/C2 → A1**. Семь backend: Windows, Linux, macOS, Android, iOS/iPadOS (один), Web, Headless. Администрирование после первого non-Windows UI больше не является активным приоритетом: оно ждёт template и WindowCatalog на всех семи.

В PLAN зафиксированы границы common/backend/application, независимый MinimalApp, переиспользование существующих runtime/session/proxy, platform lifecycle/storage hooks, критерии library controls и запрет копирования старого Surfaces runtime. Старые PASS сохранены как evidence отдельных сценариев; новый P3.T0 — только изменение плана.

**WASM reproducibility (P3.T1):** pin `79283b6`, **14 patches** (без удалённого lazy #14). Lazy headless regression — **исправлено** в `d3b590d`. **WASM cold load (2026-10-29):** первый фатальный сбой — `Node::Load(v0)` аргумент **`base_`** (`ae::Obj&`, тот же ObjId): `Serializer<Obj>::Deseri` вызывал `DomainGraph::Load(Obj)` по storage при активном reader родительского Node S0, тогда как `Seri()` пишет только side-effect слои (inline bytes нет) → `Load(Obj)` version-walk оставался на **`missing_version (1000)`**. **Первый fix (577ae1f):** bypass при любом `reader != nullptr` для **всех** `T : ae::Obj` — **риск**: отсутствие inline bytes не отменяет загрузку базовых слоёв для производных типов (`LoadExistingObject`, `CaptureBaseStateInto` / `TryRebuildFromBaseAndReplay` → прямой `DomainGraph::Load`); **исполняемое воспроизведение регрессии не зафиксировано**, contract headless (**`TestPresentedWindowDistilledBaseReplay` первым**) **PASS** на `a3d056b`. **Уточнение patch:** no-op только для **`T == ae::Obj`** (`std::is_same_v`), иначе прежний `DomainGraph::Load`; hunk index **`dbeab19`** (post-13 chain). **Проверки (2026-10-29, incremental):** Win — `apptraverse_application_contract_headless_test` (в т.ч. **`TestNativeFrameReport`**: model 640×480 после pump, mirror sizes до publish), `apptraverse_application_ui_session_test`, catalog headless, clone, origin facet, model persistence, CTest disk roundtrip — **PASS**; WASM — `web_window_catalog_demo` + smoke **`WINDOW_CATALOG_INITIAL_RENDER_OK`** — **PASS** (исторический smoke PASS на broad fix сохранён отдельно; WASM **FAIL** при несовпадении patch hunk / неpatched CPM tree — не product regression).

---

## P3.T1 — воспроизводимый Æther/WASM patch (2026-10-28, **PASS**)

**Pin (без изменений):** `79283b667cabf689da1059d618fe2a22abf51db4`.

**Patches (CMake `PATCHES`):** … → `emscripten-domain-include` → **`nested-obj-archive-deseri`** (**14** patches; lazy #14 **удалён** 2026-10-29).

**Headless lazy fix (2026-10-29):** первый отказ — `ApplicationUiSession` initial mirror, `!window_prefab.is_loaded()`; prefab W/V materialized during bootstrap/distill save из-за #14 + eager `Node::Save` / LocalPersistent capture.

**Проверки (2026-10-29, post-narrow):** `build/win64-ninja-msvc-debug` — `apptraverse_application_contract_headless_test`, `apptraverse_application_ui_session_test`, `apptraverse_window_catalog_headless_test`, `apptraverse_object_clone_test`, `apptraverse_object_origin_facet_test`, `apptraverse_model_persistence_test`, CTest **`apptraverse_object_origin_disk_roundtrip_test`** — **PASS**. `build/wasm-ninja-debug` — `web_window_catalog_demo` + `wasm_window_catalog_browser_smoke.py` — **PASS** (`WINDOW_CATALOG_INITIAL_RENDER_OK`, screenshot `docs/smoke_artifacts/window_catalog_web_initial.png`).

**Root cause (catalog `id=1`):** `Node::Load(v0)` → **`dnv(base_)`** первым; `Serializer<ae::Obj>::Deseri` вызывал **`DomainGraph::Load(Obj)`** из storage при чтении Node S0 blob (reader активен), хотя **`Seri()`** для embedded `base_` не пишет inline bytes — только side-effect `Save`. Version-walk **`Load(Obj)`** без материализованных facet-слоёv → **`missing_version (1000)`** → фатальный `LoadStoredLayers` на Node S0. **`base`/`journal`** не достигались. Origin **`ae::Obj`/v1** missing в factory — отдельно, Ok.

**Fix:** patch **`cmake/patches/aether-objects-nested-obj-archive-deseri.patch`** — при `reader != nullptr` no-op **только** для embedded **`ae::Obj`** (`Node::base_`); производные типы по-прежнему **`DomainGraph::Load`**. Contract test: **`TestNativeFrameReport`** — client width/height в model и mirror (lag до structural publish).

**NEXT (исторически):** P3.T2 — закрыт срезом **P3.T2a** ниже.

---

## P3.T2a — MinimalApp на application sessions (2026-10-29, **PASS**)

**Scope:** `examples/minimal_app/common/minimal_app_graph_builder.*` (ComposePresentedWindow + frame 120/80/640/480); Windows overload → `WinDesktopWindowPresenter`; Headless `BuildHeadlessMinimalApplicationGraph` → stock `UiDesktopWindowPresenter`; Windows-only `minimal_app_lifecycle` в `examples/minimal_app/windows/`; CMake: `minimal_app_common` (только `apptraverse`), `minimal_app_headless`, MinimalApp при `APPTRAVERSE_BUILD_EXAMPLES OR BUILD_TESTING`.

**E2E:** `apptraverse_minimal_app_headless_test` — RAM distill bootstrap, `RunApplicationModelSession<MinimalApplication>`, `ApplicationModelRuntime`, `ApplicationUiSession`, `ModelObjectProxy`; model callback только сигналит kind (mutex/cv); **UI/main thread** — `TakePublishedCopy` → `ApplicationUiSession` / `ApplyStructural` → `NotifyPublicationConsumed`; `SubmitFrameFromNative` после initial apply → `ReportUiDesktopWindowFrameFromNativeEvent` на model thread; mirror lag до apply incremental; `RequestStop`/persist через `LoadPresentedWindow`/`SavePresentedWindow` (cold reload — **P3.T3.H**).

**Проверки (win64-ninja-msvc-debug):** `apptraverse_minimal_app_headless_test`, `apptraverse_application_contract_headless_test`, `apptraverse_application_model_runtime_test`, `apptraverse_application_ui_session_test`, `apptraverse_window_catalog_headless_test` — **PASS**; `win32_minimal_app`, `win32_minimal_app_load_only` — **build PASS** (link headless test без `apptraverse_ui_windows`).

**NEXT (исторически):** P3.T3.H — см. §P3.T3.H ниже.

---

## P3.T3.H — Headless MinimalApp lifecycle (2026-10-29, **PASS**)

**T2b уточнение:** `TestListSelectionViaPresenterProxy` — UI-domain presenters + proxy queue; до `PumpModelWork` model/mirror без Event/notify; после pump — model Event, mirror lag до structural apply; duplicate selection после apply.

**E2E:** `apptraverse_minimal_app_headless_test` — temp `DirectoryDomainStorage` profile; distilled bootstrap once; session 1: `RunApplicationModelSession`, frame via `SubmitFrameFromNative`, `BeginSaveSession` persist; teardown runtime/UI; session 2: **новый** storage/domain/runtime, **LoadApplication only**, cold mirror + presenter ids, второй frame + publication; UI `TakePublishedCopy`/`NotifyPublicationConsumed` на main thread.

**Портирование:** правило в [AGENTS.md](../../AGENTS.md), [.cursor/rules/apptraverse-coding-agent.mdc](../../.cursor/rules/apptraverse-coding-agent.mdc), [PLAN §активный план](./PLAN.md#активный-план-мультиплатформенный-windowcatalog-p0p6).

**Проверки (win64-ninja-msvc-debug):** ui_control_materialization, minimal_app_headless, contract, model_runtime, ui_session, model_persistence, catalog headless, **minimal_app_win32_acceptance_test** — **PASS**; `win32_minimal_app*`, `win32_window_catalog_demo` — **build PASS**. **WASM:** `web_window_catalog_demo` + smoke `:8767` — **PASS** (initial render scope).

**Архитектурные дефекты библиотеки:** не выявлены в этом сценарии.

**NEXT:** **P3.T3.W** — Windows backend шаблона и библиотечные Window/List/Button.

---

## P3.T2b — нейтральные component contracts (2026-10-29, **PASS**)

**List selection (library):** `UiListContainer::SubmitListRowSelectionChangedFromUi` → `HandleListRowSelectionChanged` → `SetUiListSelectionEvent` → `selection_index` + `NoteMaterializedChange` (no-op при том же индексе). `UiListContainerPresenter::SubmitListRowSelectionChangedFromUi` → `ModelObjectProxy::Invoke` (как activation). WindowCatalog adapters по-прежнему override Submit → catalog model (**P4**).

**Input contracts (neutral):** Window — `UiDesktopWindowPresenter::SubmitFrameFromNative` → proxy → `ReportUiDesktopWindowFrameFromNativeEvent`; Button — `UiPushButtonPresenter::NotifyClickedFromUi` → proxy → `HandlePushButtonClicked`; List — selection/activation Submit* → proxy → model `Submit*` / handlers; backend — native presentation only.

**Проверки (win64-ninja-msvc-debug):** `apptraverse_ui_control_materialization_test` (`TestListSelectionViaPresenterProxy`: proxy queue, journal Event, mirror lag, structural apply), `apptraverse_minimal_app_headless_test`, `apptraverse_application_contract_headless_test`, `apptraverse_window_catalog_headless_test` — **PASS**; `win32_window_catalog_demo` — **build PASS**.

**NEXT:** **P3.T3.H**.

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

**Push SHA:** **`6c34008`**.

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


## Windows/headless principles implementation — 2026-11-10

Completed the Windows/headless implementation of the agreed principles on `feature/domain-hierarchy-cloning`, based on `2dadc6b`. Persistent model defaults moved into builders; required presenter-link fallbacks and the desktop Load repair were removed after completing the fixtures. Localization retains inactive languages lazily through compaction and direct history capture. Public manual clone boundaries and application-side prefab eviction were removed. USER uses plain `RamDomainStorage` with explicit single-file load/save functions. Catalog uses OS initial positioning; native geometry/work-area events drive model placement and saved-position restoration.

Final incremental Debug acceptance: **84/84 non-timing tests + 1/1 Close diagnostic PASS**. Child Close medians **204/208/216/118 ms**; main exit after child Close **45–78 ms**. RAM model Close control **4/6 ms**, zero disk bytes. Fresh-window HWND latency is higher than the earlier single-file run; this is recorded without claiming a controlled performance improvement. No MinSizeRel or platform-device build/run was performed.

See [implementation, limitations, all test results and 20 Close samples](../validation/windows-principles-2026-11-10.md). History/replay remain intact; future distributed/platform work stays deferred.


## Required localization graph — 2026-11-10

Removed null/empty/unavailable-slot fallbacks from `ResolveLocalization` and `UiLocalizationString::Resolve`. Resolution directly reads the active language already materialized by model preparation; it does not load a language implicitly. Distillation guarantees the localization object, language index and slot. An explicitly empty UiString remains valid.

Completed the headless application-contract fixture: every presented window now has its active title materialized before initial publication. Removed the same optional-link fallback from its test presenter. Added a positive regression for a real empty UiString and literal `Window {0}` text.

Incremental Windows Debug, AE_NO_DEBUG_LOG=ON: full 85-test run passed 84 and exposed the incomplete application-contract fixture. After the fixture-only fix, that test passed separately (1/1). All 85 tests are therefore covered successfully, including the Close diagnostic; no production changes followed the full run. Logs: `.artifacts/localization-contract-final-build.log`, `localization-contract-final-tests.log`, `localization-fixture-build.log`, `localization-fixture-tests.log`. No MinSizeRel or other platform build was run.


## Remaining Windows/headless graph principles — 2026-11-10

Completed points 1–4 after `46e0c2a`:

- Removed default payload initializers from UI frame/size/language/localization/caret/button events and Catalog open/viewport events (including density). Existing creation sites explicitly assign all affected fields; runtime fields remain initialized.
- Localization Apply trusts the presenter/caller's valid index and the distilled slot reference. Removed silent skips for missing slots in current-graph capture/replay preparation. Lazy loading, unchanged-value handling, journal traversal and historical-base slot availability are preserved.
- Catalog no longer treats a missing holder window as closed or substitutes a generic title when no holder matches a presented child. The latter is an invalid graph and terminates instead of rendering a fallback.
- Catalog text formatting reads model-prepared localization via Resolve; removed its separate language-slot loader and const_cast. Localized templates remain supported.
- Native window coordinates are signed and may be negative regardless of monitor count. The headless initial-position/cold-load test now verifies (-45, -67). Catalog still uses OS placement; no monitor-number field, fullscreen behavior or automatic centering was added to the example.

Validation: incremental Windows Debug, AE_NO_DEBUG_LOG=ON. Full suite **85/85 PASS** (`.artifacts/principles-four-tests.log`). Signed-position test **1/1 PASS** (`signed-position-tests.log`). After removing the last inline viewport-density default, affected Catalog suite **10/10 PASS**, including Windows acceptance and Close timing (`principles-four-final-tests.log`). Build logs use the matching names ending in `-build.log`. No production changes followed the final run. No MinSizeRel/device builds; the Android-named test is host-side headless coverage.

## Shared Node history implementations — 2026-11-10 — validated

Shared remap/capture/replay bodies now use Node references with typed Save
adapters and typed rebuild scratch. Controlled MinSizeRel savings: Catalog
**34 KiB**, MinimalApp **51.5 KiB**. Full Debug **82/82 PASS**, final targeted
MinSizeRel **17/17 PASS**; the corrected native focus test also passes separately
in Debug and three consecutive MinSizeRel runs. Static runtime and distillation
exclusion gates pass. The user subsequently authorized commit/push after the
initial uncommitted review.
See [implementation, intermediate failures, size evidence and limitations](../validation/node-history-template-sharing-2026-11-10.md).

## Canon update and scoped source audit — 2026-11-10

Updated the canon from accepted user decisions and separated historical
observations from current Windows/headless evidence at `06efdcb`. Synchronized
AGENTS, Cursor guidance, PLAN and the outdated Windows MinimalApp README.
Production and test sources were not changed. Fresh focused Windows Debug
execution: **11/11 PASS**, 35.41 s (`.artifacts/canon-audit-debug.log`).

The audit remains open for optional clone tracing, post-clone business-link
repair in the headless mobile-close prefab, unknown ancestor-class factory
rejection (source-confirmed risk without a runtime repro), and stale CI.
Passing existing tests does not cover the two missing boundary fixtures.
[Detailed findings, evidence, checklist and limitations](../validation/canon-conformance-2026-11-10.md).

## Remove permanent runtime tracing — 2026-11-10

Based on `d0739d1`, removed the Clone trace environment switch/source-location
plumbing, initial-publication diagnostic stages/fields/formatter and the old
Surfaces shutdown tracing implementation, markers and trace-only storage wrapper.
Error results, required fatal messages and shutdown Save behavior are unchanged.
UI-session tests retain observable load rejection/no activation assertions.
References to the removed source and the Android probe signature were adjusted
mechanically; no other-platform acceptance is claimed.

Incremental Windows Debug (`AE_NO_DEBUG_LOG=ON`) active targets: build PASS;
focused tests **16/16 PASS**, **108.50 s**, including both Windows application
acceptance tests. No MinSizeRel or timing benchmark. An extra legacy Surfaces
link attempt exposed existing missing registration/presenter implementation
dependencies; its compile passed but executable link did not. No unrelated
registration/CI/domain/prefab fixes were made.
See [follow-up evidence and remaining findings](../validation/canon-conformance-2026-11-10.md#follow-up-remove-permanent-runtime-tracing--2026-11-10).

## WindowCatalog-only canon completion — 2026-11-10

User scope: Windows Catalog, its headless tests and shared dependencies; no CI
or other-example repair. Based on `3fb125c`, completed the Close prefab catalog
link and removed post-Clone business-link repair. Added direct Clone/cold-load
coverage. Fixed ancestor class-factory selection via the recorded 27th Æther
patch, preserving local shadowing and rejecting unknown ancestor classes before
object activation. Moved three test-only queue-binding helpers out of production.

Both new regressions failed on the baseline, then passed with the fixes.
Offline full patch chain **27/27 PASS**; effective dependency matches verified
source (normalized line endings). Incremental Windows Debug build PASS; final
focused suite **18/18 PASS**, **101.63 s**, including Catalog native acceptance
and the 5×4 Close diagnostic. No MinSizeRel, device or other application builds.
No further concrete canon violations were found in the reviewed active Windows
Catalog paths. CI and legacy examples remain excluded, not fixed.
See [full scope, fixes, failures and verification evidence](../validation/window-catalog-canon-2026-11-10.md).


## WindowCatalog five-finding follow-up — 2026-11-10 — PASS

Based on `a2a3352`, fixed loss of restore geometry during native-frame
coalescing, premature holder history capture, and valid-lazy-list handling.
Removed production frame-sync test hooks and moved native stimuli entirely
into tests. The declared VS Debug preset is explicitly Debug-only; a new
configure-only multi-config regression checks actual Catalog target selection.

The three behavior failures were reproduced before fixes. New tests cover
mixed restore/live reports, reports during drain, shutdown `user.bin` cold
geometry, holder replay and lazy ring ordering/closed-window exclusion.
Final incremental Windows Debug (`AE_NO_DEBUG_LOG=ON`) build PASS;
**22/22 tests PASS**, **104.10 s**, including native acceptance and 5×4 Close
observations. The initial configure probe's C1902 was resolved by inheriting
this build's existing `/Z7` flags. No MinSizeRel/full VS2022/other-platform run.
The preceding audit's no-further-findings conclusion was incomplete; this
follow-up records the additional evidence rather than treating its old test
PASS as proof of all contracts.
See [changes, regressions and precise verification scope](../validation/window-catalog-canon-five-fixes-2026-11-10.md).


## WindowCatalog fresh MinSizeRel acceptance — 2026-11-10 — PASS

Validated source `0417ea6` in the existing incremental MinSizeRel build without
source fixes: **24/24 PASS**, **58.02 s**. Includes both runtime-only binary
contracts, normal-EXE fresh/restart native acceptance, the new replay/lazy-ring/
restore-frame regressions, headless/model/USER/Clone/history and native tests.
Both EXEs are **645,120 B (630 KiB)**; imports only KERNEL32/USER32; forbidden
runtime/distillation/registration/factory checks pass. The embedded supply
matches the Debug-prepared header by SHA-256.

Close diagnostic (explicit load-only EXE, 5×4): child destruction medians
**21/45/15/18 ms**, corresponding Main exit medians **31/31/28/30 ms**. Normal
EXE restart is a separate functional test, not those timing observations.
No main merge, upstream Æther publication, other-example/platform or CI work.
See [full evidence and scope](../validation/window-catalog-minsizerel-2026-11-10.md).


## 2026-11-10 — Linux WindowCatalog port prepared; WSL execution blocked

- Base: `e48db8300b16b50d5ec375fa6a1b526b2f709853`, current branch `feature/domain-hierarchy-cloning`. User explicitly expanded scope to Linux through WSL; other example applications/CI remain excluded.
- Updated GTK application composition: one startup registration, separate runtime/builder objects, normal Debug self-distillation and fresh graph, explicit embedded load-only consumer, common RAM USER/single-file shutdown lifecycle. Removed standalone Linux exporter and moved native automation into tests. Added native frame queue wiring, holder-ID list commands, row activation, OS/screen placement reports, restore/live distinction and lazy-safe z-order inspection.
- Fixed a shared blocker found during porting: structural application-root publications now carry pending nested-window frame requests by ObjId from the existing scratch serialization pass. No extra producer Save, no USER/S0/initial-publication format change. Existing presenters apply requests; newly materialized presenters use OnLoad. Ordinary snapshots do not consume presentation intent.
- Regression proof: new `apptraverse_nested_window_frame_publication_test` built and FAILed on old serializer (exit 1, first nested request); PASS after fix. Covers multiple windows, aliases, coalescing, repeat/root publications, snapshots, native/maximized reports without feedback, centered placement via screen Event, and newly loaded presenters.
- Executed: incremental existing Windows Debug, both demo EXEs and selected tests; **24/24 PASS, 101.94 s**, including the prior 22-test scope plus nested-frame and desktop-geometry tests, native acceptance and 5x4 Close. `AE_NO_DEBUG_LOG=ON`; existing dependency/cache state preserved. Python smoke syntax and `git diff --check` PASS. Initial five-test run had three file-test failures with default TEMP; unchanged code passed 5/5 with isolated test TEMP, which was also used for the final run.
- **Linux configure/build/native/headless execution: BLOCKED / NOT RUN.** `wsl --list` returns `Wsl/E_ACCESSDENIED` in this Windows agent session even after the user authorized WSL and filesystem/network access. No claim about installed distro/toolchain/GTK or a running Linux application. Linux smoke is prepared for GTK3/X11 on WSLg, using test-only runners sharing production host composition; native Wayland and Linux release acceptance are outside this iteration.
- Full evidence, new target names and exact remaining acceptance: [Linux port report](../validation/window-catalog-linux-port-2026-11-10.md). Logs: `.artifacts/linux-port-windows-build.log`, `.artifacts/linux-port-windows-tests.log`, `.artifacts/linux-port-windows-tests.xml`, `.artifacts/linux-port-nested-frame-before.log`, `.artifacts/linux-port-nested-frame-after.log`.
- Next: resume with a functioning WSL agent environment, inspect/reuse its existing build/cache, compile the Linux application/runners, run shared and GTK cold-restart tests, then launch the ordinary Linux executable once. This checkpoint publishes prepared code and Windows evidence; it is not Linux acceptance or readiness to merge to main.

## 2026-11-10 — Linux GTK/X11 acceptance; Android/WebAssembly authorized next

- WSL access now works with the agent remaining Windows native. Ubuntu24.04.2, Clang18.1.3 and GTK3.24.41 verified. Existing dependency sources reused; Linux Debug built incrementally after its initial configure.
- **Linux15/15 PASS,46.16s**, native twelve-phase acceptance43.53s on private Xvfb/Openbox. Debug distillation, embedded consumer, headless/model/storage/history/clone, initial placement, ordinary geometry/stacking, Close/Reopen/Delete/localization, matching-supply cold restarts and one-file shutdown USER verified.
- Corrected invalid test pairing fresh Debug USER with an unrelated embedded BACKING; fresh isolation and same-supply restart remain independently checked.
- Fixed ordinary resize restriction: stock GtkFixed kept600x450 request at648x488 due to old child requisitions. Backend client mount now leaves top-level size to native/model geometry. Regression stock FAIL1 -> new mount PASS0. No shared model changes.
- **Maximization DEFERRED by explicit user instruction.** Provisional transition/idle fixes were reverted and maximization removed from acceptance. Its known restore-frame issue is not marked fixed.
- **WSLg1.0.79 limitation:** own X11 WM reports58,38 for move90,70; exact position acceptance fails there. Standard Openbox reports requested coordinates and passes full suite. No magic offset correction added. Ordinary Debug application launched once on WSLg and its Window Catalog X11 window confirmed. Native Wayland/Linux release NOT RUN.
- Windows incremental Debug unchanged scope **24/24 PASS,84.26s**, including native acceptance and5x4Close. Child medians189/208/181/131ms, Main48/38/48/65ms. Existing cache, /Z7, AE_NO_DEBUG_LOG=ON, isolated TEMP retained.
- User authorized autonomous overnight Linux -> Android -> WebAssembly, no continuation questions. Android14/API34 emulator Aether_NDK_Smoke_x86_64 is booted/online (emulator-5554), and user confirmed it works. Android/Web toolchains inventoried; no port PASS claimed yet. macOS/iOS work is in another branch and remains untouched.
- Evidence and limitations: [Linux port report](../validation/window-catalog-linux-port-2026-11-10.md).

## 2026-11-10 — Android WindowCatalog port and shared regressions — PASS

- Android uses the shared application host/model/publication/UI mirror. Stable holder IDs cross the UI queue; swipes and actual viewport/DPI become model commands. Runtime and build-host graph construction are separate, with one application registration entry point.
- USER remains in ordinary RAM during work, including child Close. Android Activity.onStop drains accepted work and writes one user.bin; abrupt termination before that callback is outside the persistence guarantee. Normal Debug starts fresh in an isolated profile, debugLoadOnly verifies restoration over the same supply.
- Fixed SyncPost acceptance/stop race and stack-reference lifetime, prefab-sized mobile Close layout, Activity-remount page/title/loading state, and host-export header ownership across Gradle native build directories. The layout regression FAILed before the model Event fix and PASSed afterward, including replay and cold state. Windows mobile factories remain excluded by exact link-map inspection.
- Final emulator Android14/API34 x86_64: **Debug 5/5 PASS, 40.007s; debugLoadOnly 5/5 PASS, 41.807s**. Native input, localization, rotation, same-process Activity replacement, no working writes, one-file background save, cold restore and fresh Debug file preservation verified. Android runtime symbols exclude builders/distillation; C++ runtime is static.
- Shared regression: focused model/Android embedded **2/2 PASS**; final Linux private Xvfb/Openbox **15/15 PASS, 46.67s**. Windows batch **24/25 PASS, 142.49s**, then the unchanged native foreground test **1/1 PASS, 11.10s** after activating only the test's main window once. Emulator foreground ownership caused the earlier focus failure; no production/test workaround was added. All 25 checks are covered across the batch and focused rerun; not a clean 25/25 batch claim. Final Windows Close 5x4 diagnostic also PASS.
- Existing dependency sources and incremental Debug builds retained. ARM64/physical devices/Release/other examples NOT RUN. Linux's WSLg exact-position limitation and deferred maximization remain unchanged. Apple branch untouched.
- [Detailed implementation, failures, evidence and scope](../validation/window-catalog-android-port-2026-11-10.md); [portable Android reproduction](../../examples/surfaces_demo/android/README.md). Emulator remains running with normal Debug for inspection.

## 2026-11-10 — WebAssembly WindowCatalog port — PASS

- Shared WebApplicationHost/model worker/publication/UI mirror retained. Stable holder IDs cross the queue. Swipes and page selection are model commands; published state selects the page. Model viewport Events own child Close geometry; no CSS layout override.
- RAM USER writes one user.bin only on explicit **Close application**, then awaits IDBFS flush before reporting Stopped. No gesture/child Close/visibility saves. Abrupt browser/tab termination is not a supported asynchronous save boundary. Normal Debug uses a new profile each launch; separate Debug load-only restores the same embedded supply.
- Builder/distillation remain in the Debug Node producer; one registration entry point and runtime-only objects serve both browser consumers. Fixed actual Wasm i32/uint32 ObjId mismatch that made cloned pages unreachable by DOM lookup; no fallback added.
- Final Emscripten3.1.64 Debug build PASS. SDK Node24.19.0 embedded-session **PASS,0.876s**. Chrome157.0.8103.39 real browser acceptance **PASS,9.830s**: native DOM actions, touch swipes, locale, resize, one-file shutdown, two cold restarts, fresh Debug preservation. Runtime Wasm symbols exclude the inspected graph builders/distiller. Python syntax/diff checks PASS.
- No shared-source changes after the Android checkpoint's Windows/Linux/Android regressions. Other browsers/devices, Release, production deployment and other examples NOT RUN. macOS/iOS branch untouched; maximization remains deferred.
- [Detailed Web verification](../validation/window-catalog-web-port-2026-11-10.md); [portable reproduction](../../examples/surfaces_demo/web/README.md). Local server remains at `http://127.0.0.1:8785/web_window_catalog_demo.html`.

## 2026-11-11 — Android/Web ordinary close and restart

- Android's installed ordinary Debug was saving on `onStop` but deliberately ignoring prior USER on process launch. The persistent `debugLoadOnly` APK is now installed for manual launcher use; ordinary Debug's fresh policy and existing profiles are preserved. No Android production lifecycle change was required.
- Web now saves at hidden `visibilitychange` / `pagehide`: accepted model work finishes, the existing RAM USER becomes the same binary `user.bin`, and the browser queues/commits one transaction in its already-open IDBFS database before returning from the event. No sidecar, localStorage copy or working-action file writes. Returning to the page retains the live model. Explicit Close also establishes this boundary before stopping, covering immediate tab close/reload; the previous acknowledged async shutdown remains.
- Extracted the filesystem-free USER byte codec without changing its format. Incremental Windows **25/25 PASS** (24-test batch 120.39 s and native 10.52 s with initial main-window focus); Linux **15/15 PASS, 46.94 s**. Debug Close 5-by-4 completed; no comparative performance claim under concurrent build load.
- Fully repackaged Android load-only **7/7 PASS, 65.636 s**. Normal Debug **6/7 in 94.173 s plus unchanged focused localization PASS, 10.096 s**: the batch missed a horizontal navigation gesture, while both new strict Recents/task-ID cases passed. Full seven-case coverage across runs, not a clean batch claim. Actual task removal is checked separately from process retention; cold restoration uses a different PID.
- Web final Debug build and Node embedded test **PASS**. Chrome157.0.8103.39 functional **PASS, 8.005 s**; real lifecycle **PASS, 10.937 s**: tab close, queued-command reload, navigation, hide/return, native browser-window close/process restart with the same profile, and Close-then-reload. The harness bypasses Playwright focus emulation and does not substitute fake events or CDP browser termination for ordinary close.
- Manual browser persistence URL: `http://127.0.0.1:8785/web_window_catalog_demo_load_only.html`. Normal Debug remains fresh. Crash/no-notification termination, other browsers/devices and Release packaging are outside this proof. WSLg position and maximization limitations remain unchanged; Apple work untouched.
- Final ordinary Android launcher check **PASS**: Back/save, real Recents removal, natural process exit and cold PID21102→22112 restored active Window1, RU and closed Window2. The current persistent APK remains installed/open. A rebuilt Debug producer had changed embedded constant IDs, so the earlier agent-created test USER was archived before preparing a matching profile; all prior user Debug-run files remain unchanged. Cross-supply upgrades remain unsupported, with no runtime repair/reset added.
- [Implementation, test failures, exact results and evidence](../validation/window-catalog-mobile-web-lifecycle-2026-11-11.md). Canon §8/rule12 and platform READMEs now distinguish desktop shutdown from Android/Web lifecycle boundaries.

## 2026-11-10 — isolated Apple branch: macOS WindowCatalog

User-assigned macOS port on `feature/apple-window-catalog`, baseline `b0c808d`;
parallel Windows/Linux/Web/Android/headless work remains on the original branch.
Shared common production code unchanged. Native SwiftUI/AppKit catalog,
thin presenters, native geometry/focus/Z-order, RAM USER and embedded cold-load
consumer implemented. Debug fresh supply uses the same application's
`--distill-only`. Fixed backend schema anchoring, native focus and actual
SwiftUI list tag/selection behavior; verified late geometry after unload.

Executed on Intel macOS 13.7.8, MacPorts clang 20.1.8 + Xcode Swift 5.9.2:
**7/7 common tests PASS**, **2/2 native tests PASS** (final 20.09 s; three catalog
processes, 51 checkpoints). Final visual QA confirms native selection highlight.
No other-platform native or iOS PASS is claimed; iPhone deferred by user.
External Computer Use service requires macOS 14.4 and cannot start on this host;
own-window content capture works. Full scope, failures/fixes and limits:
[macOS validation](../validation/window-catalog-macos-2026-11-10.md).

## 2026-11-10 — macOS actual window-list mouse route

Reproduced the user's list-click defect with real AppKit NSEvent single/double
clicks. Replaced competing SwiftUI row gestures with one native NSTableView
selection/doubleAction route; preserved displayed row identity and confirmed
model selection. Added visible native text geometry and first-mouse checks.
Final Debug/embedded/cold native+lifecycle+WindowCatalog headless: **3/3 PASS**,
83.74 s; six common regression checks: **6/6 PASS**, 35.19 s.
[Full native-click evidence](../validation/window-catalog-macos-native-clicks-2026-11-10.md).

The corrected MinSizeRel executable is 1,199,752 B. Its earlier embedded supply
is byte-identical; the updated output app was relaunched with the user's
existing profile, with saved USER unchanged. Binary/launch evidence is recorded
in the native-click report.

## 2026-11-10 — iPhone WindowCatalog and mobile viewport Event

User expanded this isolated Apple branch to iOS. Added the SwiftUI/UIKit catalog
composition, two thin catalog presenters, shared model/proxy/Events/mirror,
model mobile ring, native table/page input, localized Close and rotation.
Background performs orderly session drain/save/join and foreground resumes
the same RAM profile through a new runtime; Debug starts fresh once per process.
An explicit embedded consumer supports separate cold USER startup.

Common application-only S0 viewport Event keeps mobile Close layout in model
Apply, including DPI changes, tiny viewports and queued closed-window reports.
No core-library or persistent-field/schema migration change. Host selected
tests **9/9 PASS** as recorded above. Both iOS products and acceptance apps
built; iPhone 15 Pro/iOS 17.2 simulator acceptance **3/3 processes PASS**,
47 checkpoints plus three native callback identity checks. Product Debug
installed/launched normally; portrait/landscape captures visually checked.
Physical iPhone/signing and other-platform native execution are NOT RUN.
[iOS architecture and executed evidence](../validation/window-catalog-ios-2026-11-10.md).

## 2026-11-11 — iOS native deactivation and persistent consumer

The user's close/relaunch report exposed the installed Debug product's fresh-
process policy and a view-owned background-only lifecycle route. Verified a
real OS Settings transition saved the latest Debug USER before changes. Added
host-owned native deactivation/background/activation notifications: one orderly
drain/save/join under a background task, resume only after join. No common model,
USER/schema changes or per-command checkpoint. Debug remains fresh and is now
named WindowCatalog Debug; the explicit embedded consumer is WindowCatalog.

Incremental iOS build **11/11 steps PASS**. Native acceptance **5/5 processes
PASS**: original Debug/embedded/cold 21+21+5 checkpoints, plus an actual OS app
switch, single USER, process termination and **5/5** additional cold checks.
Latest user's supply/USER pair backed up together for persistent installation.
Installed the explicit consumer with that exact supply and transferred the
6,092-byte USER. Actual product OS deactivation, terminate and cold launch PASS;
USER remained byte-identical, Window 2 and its three-page ring restored.
[Diagnosis, lifecycle semantics and evidence](../validation/window-catalog-ios-lifecycle-2026-11-11.md).

## 2026-11-11 — Merge completed Apple branch into the shared platform branch

- User authorized merging `feature/apple-window-catalog` (`d55f4bf`) into `feature/domain-hierarchy-cloning` (`1aff49e`). Both histories and all five Apple commits are retained; main is outside scope.
- Resolved independent mobile viewport implementations in favor of the existing child-owned Event/NodeFor and replay contract. Shared production model/layout/registration remains identical to the target parent. iOS now passes explicit dpi160 through the four-argument model proxy; added portable iOS portrait/landscape and tiny-viewport coverage without discarding existing replay/cold/geometry checks. All other Apple platform source is identical to its source-branch tip.
- Incremental Windows **25/25 PASS** (24-test batch91.89s, native16.81s with main-only foreground activation); Linux **15/15 PASS46.72s**, private Xvfb/Openbox. Existing dependency caches retained; no Release builds.
- Unchanged Android persistent artifact: six of seven cases in batch113.107s, remaining unchanged rotation/navigation case PASS13.261s on focused rerun. USER remained byte-identical; ordinary launcher restored current RU Window2. Unchanged Web functional/lifecycle/Node tests PASS; headed lifecycle16.391s. No Android/Web APK or embedded-supply replacement.
- Merged macOS/iOS native build and execution are **NOT RUN** on this Windows/WSL host. Imported MacBook PASS evidence remains historical; a MacBook incremental/native run must validate the merged revision before claiming new Apple runtime acceptance. No toolchain limitation is disguised as PASS.
- [Merge decisions, executed results and evidence](../validation/window-catalog-apple-merge-2026-11-11.md).
