# AppTraverse — прогресс версионирования поставок

## Source of truth

1. Google Doc: [AppTraverse — версионирование состояния и жизненный цикл поставок](https://docs.google.com/document/d/196STRvU1QMbMu0ArGRLahPpgnTb6HQoldUV-7tOIcfo/edit) — **прямая сверка не выполнена** (403 при fetch).
2. Архитектурный контракт и roadmap: [PLAN.md](./PLAN.md) — **AppTraverse Libraries Architecture RU v0.3 (25.10.2026)** + Object Core **`01_Object_Core_Versioning_PLAN.md` (25.10.2026)** ([share](https://chatgpt.com/library/share/libfile_c97b08ed7af88191a916944ac06566f7?account_id=personal)).

**Branch (рабочая):** **integration/unified-application** (merge **4848402**). Baseline: **integration/serialization-surfaces**, **integration/application-versioning**. См. [PLAN §Unified](./PLAN.md#unified-integration-историческая-координация--merge).

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

**Активный порядок (2026-11-02):** **P3.T3.L3 PASS** (§P3.T3.L3). **WindowCatalog обязательный граф** — правки продолжаются (§[WindowCatalog required links](./PROGRESS.md#windowcatalog-required-links-fatal-2026-11-02)). **P3.T3.M (macOS)** — **не NEXT**. Полная Android lifecycle matrix — **не начинать**. **P0/P1 DONE** ([PLAN §P0–P6](./PLAN.md#активный-план-мультиплатформенный-windowcatalog-p0p6)).

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
