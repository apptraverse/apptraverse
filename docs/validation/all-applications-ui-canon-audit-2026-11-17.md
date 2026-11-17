# All remaining non-Apple applications: UI migration audit

Date: 17 November 2026. Initial source baseline: `fd08d202`, branch
`maintenance/rename-window-catalog-example`. Astra ultra owns this source audit;
Sol high owns implementation. Auditor writes are limited to validation/status
documentation; no builds, native interactions, commits or pushes were performed
by this auditor. **All 30 source dispositions are accepted, and the required
scoped migration checks have passed as recorded below, including R6's Android
lifecycle and separate LOCAL cold restore.** Final Windows/GTK/Chrome and Android
existing-consumer evidence is recorded below. An additional unpaced
40-character adb stress probe is NOT PASS, separately from the successful native
lifecycle fixture and 3/15-character input checks. The initial plan and provisional
checklists are retained as historical checkpoints, superseded by the final
source dispositions and evidence addenda.

The current user instruction authorizes all remaining application UI migrations,
including optional legacy examples, superseding older exclusions of other
examples. Apple implementation remains excluded; preserve its source contracts.
Normative sources are [Canon §25](../architecture/AppTraverse_Architecture_RU.md),
[LOCAL](../architecture/ClientLocalEnvironment.md), repository `AGENTS.md`, and
the [prior final 30-rule audit](windows-canon-ultra-audit-2026-11-16.md).

## Complete entrypoint inventory

Nine remaining backend entrypoints, represented by fourteen executable/native
target variants, were identified. Option defaults do not remove an example from
the authorized inventory.

| Family / entrypoint | Targets or package | Required behavior to retain |
| --- | --- | --- |
| `main_window_runtime_demo/windows/main.cpp` | `win32_main_window_runtime_demo`, `_load_only` | Independent main-window model/publication example, native move/resize and cold placement. |
| `dynamic_objects_demo/windows/main.cpp` | `win32_dynamic_objects_demo`, `_load_only` | Add/remove dynamic items, stable identities/order, stale/double removal behavior and cold state. |
| `window_catalog/windows/main.cpp` | `win32_window_catalog`, `win32_window_catalog_v1`, `win32_window_catalog_load_only` | Legacy Surface create/remove/current selection, title/suffix, localization, close and desktop ordering; preserve exposed v1 distinctions. |
| `window_catalog/linux/main.cpp` | `linux_window_catalog`, `_load_only` | Equivalent legacy Surface behavior with GTK native controls and placement. |
| `window_catalog/web/main.cpp` | `web_window_catalog` | Legacy Surface page create/remove/select, localization, viewport and browser lifecycle behavior. |
| `shared_node_chat/main_windows.cpp` | `shared_node_chat` | Native edit/copy/join/send, participants/messages/status, event-driven publications. |
| `shared_node_chat/linux/main.cpp` | `shared_node_chat` | Same chat semantics through GTK. |
| `shared_node_chat/web/main.cpp` | `shared_node_chat_web` / `index.html` | Same chat semantics through DOM controls and browser lifecycle. |
| `shared_node_chat/android/native/jni_bridge.cpp` and `MainActivity.java` | `shared_chat_android` / SharedChat APK | Same chat semantics through native Android controls, keyboard resize and activity lifecycle. |

The first five rows are behind `APPTRAVERSE_BUILD_LEGACY_EXAMPLES`. The chat is
opt-in through `APPTRAVERSE_BUILD_SHARED_NODE_CHAT` and its transport dependency.
Current WindowCatalog (`*_window_catalog_demo*`) and MinimalApp on Windows,
Linux, Web and Android already use the current architecture and are regression
consumers, not omissions from the inventory.

Dormant Android legacy `native_runtime.cpp`, `android_surface_presenter.cpp` and
`surfaces_ui_bridge.cpp` are not compiled by the current WindowCatalog JNI target;
the active target uses `window_catalog_native_runtime.cpp` and
`window_catalog_ui_bridge.cpp`. Classify these as retained compatibility sources,
not an additional running application. Existing macOS/iOS entrypoints and their
adapters are explicitly excluded from implementation/runtime claims.

No additional product GUI was found outside `examples/`. `services/push` is a
headless Java service, key utility and native/protocol/live probes. `tools/` and
`scripts/` contain CLI build/package/launch/automation utilities. Exporters,
`shared_node_chat_headless`, compile checks and tests are headless consumers;
they must retain their purpose, not gain artificial UI merely to change category.
Inventory used repository source/entrypoint and CMake searches, excluding build
trees, generated outputs and dependency patch contents.

## Confirmed baseline deviations and common seams

1. **Legacy lifecycle copies.** Main-window/dynamic lifecycles and
   `window_catalog/common/surfaces_lifecycle.cpp` use `DirectoryDomainStorage`
   and application-specific worker/publication loops. Their hosts separately own
   UI domains and apply publications. Adopt `ApplicationModelRuntime`, common
   model-session hooks, `ApplicationUiSession` and native application hosts.
   Preserve behavior and genuine lifecycle states instead of adding null/default
   repair paths.
2. **Legacy presentation/layout.** Main-window uses a private persistent pixel
   frame and presenter. Dynamic creates HWND controls per item and positions them
   with `4 + index * 28`; Surfaces has bespoke hosts/presenters and native
   presentation-size feedback. Compose `UiPresentedWindow`/desktop or mobile
   components, standard buttons/lists/text controls, declarative constraints and
   the single shared solver. Native measurements and rectangles remain local.
3. **Chat custom control model.** `chat_ui_model.h` serializes
   `ControlState::frame` and a private `Layout`; `RefreshControls()` computes
   pixels from client dimensions. `Resize()` feeds native dimensions into
   `SubmitFrameFromNative`. `ChatControlPresenter` dispatches to a private
   `Presentation` host. Replace these active non-Apple paths with standard
   component model/presenter contracts and native measurement. Native controls
   already exist; moving the same hand positioning into another application
   function would not satisfy the contract.
4. **Chat required links.** Its initial four-argument `ComposePresentedWindow`
   call wires title/locale/presenter, **not** layout/device services. The new
   non-Apple graph needs the six-argument composition with explicitly built
   `UiLayoutDefinition`, `UiLayoutState`, `UiDeviceState` and a marked LOCAL root.
   Keep compatibility overloads needed by excluded Apple sources.
5. **Chat disk state conflicts with the latest instruction.** At the baseline,
   `chat_ui_session.cpp` persists/restores the entire UI graph, including
   connection/messages/editing and history. `src/chat_session.cpp` also calls
   `SaveUserStorageFile`/`LoadUserStorageFile` for SharedNode business state and
   pending receipts. Both contradict current `AGENTS.md`: chat state/history and
   pending receipts stay in RAM, without send/receive/shutdown disk Save/Load.
   The SDK identity path (`profile/sdk`, or its supplied transport profile) is
   separate and may persist. Native UI/device LOCAL may persist independently;
   do not disguise messages/drafts/receipts as device state. Keep checkpoint API
   compatibility only with honest RAM/SDK boundary semantics.
6. **Input and publication.** Text edits, copy feedback, join/send and item
   actions must traverse `ModelObjectProxy` to model Events and then publication
   to the mirror. Do not introduce periodic ChatView refresh or UI polling.
   Transport callbacks remain runtime capabilities, not serialized handles or
   external effects inside Event replay.

Required shared component additions are only those exercised here: native text
display/edit (including read-only/multiline behavior), existing buttons and lists,
and any small common constraint feature required by the actual compositions.
They belong in `include/apptraverse/ui`, `src/ui` and backend libraries, not a
chat-specific backend. Preserve focus/selection while applying text publications;
disabled/read-only state, native scrolling and accessibility remain native.

All new consumers must use the generic LOCAL routing **before Load**, and prepare
the current OS baseline **before the first publication**. USER and LOCAL use
independent ordinary RAM stores and the same native object format. Existing LOCAL
wins over an optional seed in full. Desktop placement is outer-frame 96-DIPs;
Android/Web use their documented logical client-surface coordinates. Geometry
observations must not overwrite portable semantic frames or presenter pixels.
Explicit equal-data `RequestFrame` remains a runtime intent, not a replay effect.

## Surface reuse decision

Reuse of modern WindowCatalog composition is acceptable if a small adapter or
explicit per-demo supply preserves the legacy GUI's meaningful behavior and
target options. A bare executable alias to WindowCatalog or MinimalApp is not a
migration. Keep old model/evolution libraries and CLI fixtures for specialized
versioning tests. In particular, retain the v1 demonstration's exposed suffix and
default differences without silently adding experimental-profile migration to
the current S0 application contract. Record any deliberate behavior change as a
limitation rather than declaring equivalent behavior without a test.

## Final acceptance checklist: all 30 mandatory Canon rules

The initial disposition is **OPEN** for new migration acceptance. `Retain` means
preserve a source constraint; it does not certify execution. Each final report
must distinguish source alignment, executed PASS, failure and NOT RUN.

| # | Rule | Required final evidence / initial disposition |
| --- | --- | --- |
| 1 | Complete graph | OPEN: explicit required title/locale/control/window/layout/device links in every new supply. |
| 2 | All or nothing | OPEN: missing final binding fails at composition/native producing boundary; no silently partial UI. |
| 3 | Explicit persistent defaults | OPEN: builders initialize all new S0 model/Event data before finalization; no constructor substitution. |
| 4 | Genuine runtime conditions | Retain: disabled, hidden, closed/lazy, mounting, stopped and stale-target states remain valid conditions. |
| 5 | Model authors state | OPEN: real native input → proxy → Event → publication → mirror; measured rectangles stay UI-local. |
| 6 | Thin presenters | OPEN: native measurement/projection in reusable backend; one common solver, no app pixel arithmetic. |
| 7 | Common application template | OPEN: all nine entrypoints use common sessions/hosts/components; no WindowCatalog dependency in library/backend. |
| 8 | Registration once | OPEN: application entry registration includes exactly used component dependencies; no per-control/observation registration. |
| 9 | Fresh Debug | OPEN: normal Debug constructs fresh state; load-only explicit, matching supply and fresh LOCAL policy documented. |
| 10 | Release consumption | Retain source separation: shipping consumers exclude builders, retain static runtime policy; new Release runtime NOT RUN unless executed. |
| 11 | Ordinary RAM stores | OPEN: generic USER/BACKING/LOCAL routing; no DirectoryDomainStorage application persistence or new store-manager surrogate. |
| 12 | Lifecycle saves | OPEN: desktop shutdown / Android onStop / Web hidden-pagehide save native LOCAL and authorized USER; chat business/history/receipts never disk-saved. |
| 13 | One serialization | OPEN: native ObjId/ClassId/S/scopes/history; no custom UI JSON/INI/checkpoint state graph. |
| 14 | Object-system ownership | OPEN: roots/domains/stores outlive users; removal/remount/teardown free native resources; no keepalive masking. |
| 15 | Domains and Clone | Retain and test affected paths: stable business identities, correct clone internals/shared references, no application boundary_ids. |
| 16 | Constant semantics | OPEN: supply-owned constraints/defaults remain references; overrides survive compatible supply change and Clear reveals new default. |
| 17 | Lazy graph | Retain: unloaded identity is not absence; closed windows/materialization do not require full activation. |
| 18 | History retained | OPEN: immediate compaction/base/journal/replay remain; RAM-only chat still has history and ACK retry semantics; replay has no external effects. |
| 19 | Close/unload/delete | OPEN: dynamic remove differs from close; reopen/cold load preserve intended identities; chat process restart intentionally loses RAM business state. |
| 20 | Active language | OPEN: legacy localization retained with required links; chat's authorized fixed English requires no new localization feature. |
| 21 | Placement Events | OPEN: LOCAL OS baseline/observations/monitor resolution, native outer-DIP conversion, equal-data request and no semantic-frame feedback. |
| 22 | Same headless model | OPEN: deterministic add/remove/chat/model/layout/storage tests plus separate native route tests per actual backend. |
| 23 | S0 prototype | Retain: no migration of experimental app profiles; preserve distinct specialized library/v1 tests. |
| 24 | Normal environment | Retain: no added recovery/tracing subsystem; real side effects execute outside assert and failures are handled at boundary. |
| 25 | Used components/scope | OPEN: all inventory entries covered, shared controls have real consumers, Apple source compatibility preserved. |
| 26 | Technical policy | Retain: C++20/no compiler RTTI/exceptions, runtime handles excluded from model; Windows 7 static import/toolchain boundary preserved. |
| 27 | Incremental builds | OPEN execution: existing Debug builds reused; no fresh dependency population or clean/release matrix without cause. |
| 28 | Separate future mechanisms | Retain: existing authorized SharedNode/SDK remains separate; no new networking/cache/resource/update scope. |
| 29 | Scoped evidence | OPEN: behavior-preserving source review plus honest per-target runtime results; old PASS is not a new run. |
| 30 | Ownership/publication | Retain: Astra audit, Sol implementation; root aggregates commit/push on current branch; no unrequested main/force publication. |

Minimum execution should cover fresh and explicit cold LOCAL for each newly
migrated backend, dynamic add/remove/order, Surface create/close/language/v1
distinction, and chat edit/join/send plus restart absence of business disk state.
Use meaningful model tests for new component/state boundaries and actual native
route checks for the UI. Run affected existing WindowCatalog/MinimalApp historical
smokes after shared fixes. Headless success alone does not establish native
measurement, focus, lifecycle, or browser/Activity behavior.

Preserve prior limitations: no actual Windows 7 or OS-image-upgrade evidence,
physical monitor/accessibility matrices and Apple runtime are NOT RUN. Existing
Android Recents dismissal failures remain NOT PASS; they are not converted by
unrelated chat or component migration success. Raw profiles, SDK identities,
binaries and logs remain private artifacts, outside source/documentation staging.

## Implementation review checkpoint — 17 November 2026

All nine inventoried entrypoints now select generic compositions/hosts in the
working tree. Review covered their new model/session/entrypoint sources, four
native text presenters, common component changes, RAM chat and rejoin changes,
and the publication hydration correction. **This is a provisional source review,
not final acceptance.** The sources were still changing; Android rapid-input and
final shared-core/native reruns were pending. The auditor executed no tests.

Implementation evidence is recorded separately in
[main/dynamic](main-dynamic-generic-ui-2026-11-17.md),
[Surface](surface-generic-ui-2026-11-17.md),
[chat common/Windows](shared-node-chat-generic-ui-2026-11-17.md) and
[chat non-Apple](shared-node-chat-nonapple-native-2026-11-17.md).
Their intermediate PASS records precede some shared changes and must not be
promoted to final-source evidence.

### Findings sent to implementation owners

- **R1, Canon 16:** `runtime_ui_model.cpp` did not mark its layout definition
  constant; `runtime_ui_main.cpp` exported and ran with an empty constant set.
  Required correction: immutable definition metadata, actual supply constants
  in producer/consumer, USER exclusion and compatible default/override/Clear
  coverage. The root's device/definition historical live references are already
  collected recursively from `UiPresentedWindow` and `UiLayoutState`; an extra
  root override is not inherently required.
- **R2, Canon 10/12:** Surface Web initialized `load=false` in all configurations
  and only read previous native files with `--load-only`. A normal shipping
  consumer would overwrite prior state. Required correction: normal shipping
  restore, fresh Debug unchanged. The ports owner reported this source correction
  and removal of routine Surface shell diagnostics; final review is pending.
- **R3, native lifecycle:** Android chat retained its global `copied` request
  counter when destroying a Session. A new Session starts its model counter at
  zero, so its first Copy could be ignored. The ports owner reported resetting
  the counter on fresh Session/stop while preserving Activity remount behavior;
  same-process finish/relaunch/Copy execution remains pending.
- **R4, Canon 9/10:** main/dynamic CMake defined the Debug bootstrap macro
  unconditionally, so shipping builds also distilled fresh state. Their common
  source also contained the initial builder in load-only consumers. Required
  correction: Debug producer-only builder and non-Debug matching-supply consumer
  branches. A new Release build is not required to inspect this source policy.

Additional known gates are the requested semantic accessibility names for native
text controls, removal of temporary Android input/phase logs and publication-hold
hooks, and final affected-platform regressions. Temporary diagnostics seen during
this checkpoint are not accepted shipping source. Do not use technical control
IDs as accessibility names.

### Common publication batch assessment

`ApplyGraphPublicationPayloadToUiDomain` now uses one operation-local
`DomainGraph` for the root and already-live injected objects. The pinned object
dependency's cycle detector keys visits by `(ObjId, ClassId)`; factory load returns
`nullopt` for an already visited layer. Existing references resolve through the
local Domain and retain canonical instances. A new referenced object loads
through the same graph; the dependency clears visit marks only when constructing
a genuinely new instance. Serialized lazy flags are unchanged. Existing
structural keepalive remains bounded to the apply/update operation, not global.

No source-level objection was found to this batch correction. The new session
test asserts one root/presenter layer load and stable canonical pointers. Final
acceptance still needs the affected close/reopen, lazy/cycle, failure and native
publication regressions. Source reasoning is not runtime proof.

### All 30 rules: provisional implementation dispositions

`Aligned source` below means no confirmed issue in this bounded review; final
source freeze, test/log review and the named open corrections still apply.

| # | Checkpoint disposition |
| --- | --- |
| 1 | Aligned source: explicit typed window/layout/device links; complete native factory set required. Accessibility-name fields are being completed. |
| 2 | Aligned source: partial chat factories rejected; mandatory final native bindings use shared failure boundaries. Final negative execution evidence pending. |
| 3 | Aligned source: current builders explicitly set new persisted controls/events/defaults; new accessibility defaults require final review. |
| 4 | Aligned source: hidden role controls, stale remove, mounting/remount and stopped sessions remain real conditions. |
| 5 | Aligned source: proxy/Event/publication paths; chat native viewport no longer rewrites semantic frame. Android rapid-input execution remains open. |
| 6 | Aligned source: reusable text presenters and native measurement; common constraint engine owns rectangles. |
| 7 | Aligned source: all entrypoints use common model/UI sessions/native hosts; Surface is an independent composition, not an executable alias. |
| 8 | Aligned source: application/component registration is composed at entry; no new registration per observation found. |
| 9 | Correction required: R4 configuration-dependent fresh/consumer behavior; ordinary Debug remains fresh. |
| 10 | Correction required: R2 shipping restore and R4 producer separation. Actual Release packaging remains NOT RUN. |
| 11 | Aligned source: active GUI stores are ordinary USER/LOCAL/BACKING RAM; old DirectoryDomainStorage remains in explicit historical headless fixtures. |
| 12 | R2 correction/final checks pending; chat business/history/receipts now RAM-only, native LOCAL uses accepted platform boundaries. |
| 13 | Aligned source: native serialization remains shared; no new manual UI graph format. Chat control v2 is routing metadata around native SharedNode payloads. |
| 14 | Aligned source pending batch regressions: concrete NodeFor, live owner references and operation-local keepalive; no global object lifetime workaround. |
| 15 | Aligned source: stable item/window identities and stale removal preserved; existing shared clone boundary unchanged. |
| 16 | Correction required: R1 main/dynamic immutable definition/constants. Surface/chat definitions use explicit constant metadata. |
| 17 | Aligned source pending common regressions: lazy flags unchanged by batch hydration; materialized references retain canonical identity. |
| 18 | Aligned source: history/compaction remain, derived control owner fields retained; RAM-only SharedSync ACK/checkpoint obligations remain in native RAM snapshots. |
| 19 | Aligned source with R3 lifecycle check pending: remove/close/remount differ; chat fresh process intentionally loses business state. |
| 20 | Aligned source: Surface RU/EN and mutable label overrides retained; chat fixed English with existing locale links. |
| 21 | Aligned source: standard LOCAL environment/placement services, preparation before first publication and UI-local measurements. Hardware matrix NOT RUN. |
| 22 | Source aligned; execution incomplete: model and native fixtures exist, final Android rapid-input and refreshed platform runs pending. |
| 23 | Aligned source: new generic applications remain S0 and use distinct profiles; historical Surface S1 tools retained separately. |
| 24 | Pending cleanup: temporary profiler logs/publication-hold switch are present during diagnosis and must be removed before delivery. |
| 25 | Aligned scope: real text consumers, existing list reused, services/tools remain headless; Apple source files untouched, runtime excluded. Semantic native accessibility names pending. |
| 26 | Source policy retained: C++20/no compiler RTTI/exceptions, runtime handles excluded; Windows v143/Win7 flags recorded, actual Win7 NOT RUN. |
| 27 | Worker reports incremental Debug with existing caches; narrow owned ILK scratch cleanup documented after disk-full failures. Auditor did not build or delete. |
| 28 | Aligned source: existing authorized SharedNode remains separate; fresh-RAM known-member rejoin uses explicit request identity and frozen snapshot policy. |
| 29 | Pending final source/evidence pairing. Android rapid-input is not PASS; historical A→B→A→B S1 refusal remains BLOCKED/NOT PASS; earlier Recents failures retained. |
| 30 | Aligned: Astra review/document only, Sol implementation; root owns aggregate commit/push. No auditor implementation edit or publication. |

Final sign-off must close R1–R4, inspect semantic accessibility names and remove
diagnostic hooks, then distinguish executed final-slice results from unexecuted
hardware, Apple, Release and historical failure boundaries.

## Final source-conformance review

**R1–R4 are closed in the reviewed source. A fifth pre-delivery defect, R5, was
found and corrected. No remaining confirmed scoped architecture blocker was
identified after those corrections.** This is source-conformance acceptance,
with runtime evidence bounded below; it is not certification of every platform,
generator, hardware or accessibility scenario. No implementation was changed or
test executed by this auditor.

R1 now has a producer-only `runtime_ui_supply.cpp`, explicit constant definition
metadata, collected/exported native constant IDs and consumer overlays using
them. `RuntimeItemLayoutState` repeats immutable Add/Item templates over live
model identities through the standard virtual `EffectiveRoot` interface. It
stores neither measured pixels nor a stale list of removed control slots. Its
typed application owner is a historical live reference. Independent consumers
test absence of any definition class layer under any USER/LOCAL identity,
changed supply defaults, overrides/Clear, topology changes and canonical owner
identity after replay/cold load. R4 separates that producer from consumers and
restores non-Debug supply consumption. The supported Visual Studio Debug preset
explicitly sets `CMAKE_BUILD_TYPE=Debug` and Debug-only configurations; arbitrary
mixed-configuration generators are not accepted by these runs.

R2 now defaults the shipping Surface Web consumer to reading native USER/LOCAL;
normal Debug remains fresh. R3 resets the Android Copy request counter only on
fresh Session creation/stop, retaining it during remount.

**R5:** the last source-formatting pass split embedded JavaScript `===`, `!==`
and `=>` tokens, and damaged the profile-name regular expression, in
`web_ui_text_control_presenter.cpp` and chat `web/main.cpp`. The auditor reported
this immediately. Sol restored the operators/expression and guarded embedded JS
against C++ formatting. Source reinspection found the proper tokens. The first
attempt using the broken formatted browser artifact is not a PASS; final Chrome
execution must use the rebuilt corrected artifact.

New semantic text names are explicit S0 data and are supplied through model
projection to MSAA, ATK, DOM ARIA and Android native accessibility properties.
Windows native EDIT role/value/readonly/disabled semantics remain native.
Temporary phase/input logs, technical-control diagnostic branches, publication
holding and per-send UI-session diagnostic timing have been removed in the inspected final
sources. No conditional recovery or silent factory fallback replaced them.

The final publication correction additionally compares complete native layer
bytes, including end-of-layer length, before injecting **all** received layers.
Only changed already-materialized objects are reloaded; one `DomainGraph` spans
that operation. Generation adoption and equal-data explicit frame-intent
sidecars remain independent of byte equality. Source review found no new global
cache, persistence format, eager-lazy loading or global keepalive. The examined
tests cover changed-root and child-only updates, duplicate publications,
canonical identity, close/reopen, clones and existing lifecycle behavior.

### Verified evidence at this review cutoff

- `.artifacts/main-dynamic-canon-final/ctest.log`: all twelve named tests matched
  their Passed lines, total 12.82s. This includes the three actual GUI families,
  two independent supply consumers, generated consumer build graph, Surface
  model and historical headless fixtures/negative. All seven EXE SHA-256 values
  were independently recomputed and match `binary-manifest.json`.
- `.artifacts/chat-final-core-native-ctest.log`: all nine named Passed lines,
  total 3.75s, comprising seven common/core regressions, RAM chat and the native
  Windows text-control test. The text test asserts native MSAA names, role,
  value, readonly/disabled state and remount in addition to input sequencing.
- `.artifacts/chat-final-model-test.log` and
  `.artifacts/chat-local-isolation-final.log`: final embedded model and separate
  LOCAL checkpoint/restore results are PASS. The LOCAL test excludes chat root,
  controls, participants and draft content from the device partition.
- `.artifacts/chat-final-cloud-test.log`: actual Windows three-peer stages end
  in PASS, including Copy/admission, Unicode delivery, offline traffic,
  same-SDK-identity fresh-RAM rejoin/snapshot, subsequent send, accepted native
  user-frame LOCAL restore and clean shutdown. The similarly named older
  `chat-generic-cloud-final.log` contains a failed control-readiness attempt;
  that failure is retained and is not the evidence for the final PASS.
- Static PE/import reports cover the rebuilt Windows GUI artifacts (main,
  dynamic and Surface subsystem 6.01; chat 6.00) with none of the searched known
  post-Win7 direct imports. This is not actual Win7 execution or proof of CRT
  provisioning. Physical screen-reader behavior is not implied by native MSAA
  property assertions.

Final post-R5 Chrome, GTK and Android evidence and current
WindowCatalog/MinimalApp consumer reruns were still arriving at this source
review cutoff. Their final status must be recorded separately before claiming
complete executed migration acceptance.

### Final dispositions of all 30 mandatory rules

`Aligned` means no remaining confirmed source violation in the examined scope;
the execution qualifications in this report remain part of every disposition.

| # | Final source disposition |
| --- | --- |
| 1 | Aligned: typed required graph links, explicit controls/layout/device composition and native factories. |
| 2 | Aligned: incomplete factories and missing final native bindings fail at producing boundaries; no partial headless fallback. |
| 3 | Aligned: explicit builder/Event data, including semantic accessibility names; runtime-only caches/handles remain separate. |
| 4 | Aligned: real hidden/disabled/lazy/mounting/stale-action states preserved without treating required links as optional. |
| 5 | Aligned: native input → proxy → Events → publication → mirror; native viewport and geometry stay outside portable semantic state. |
| 6 | Aligned: standard native controls, native measurements and one shared solver; no application native-control layout retained. |
| 7 | Aligned: all nine inventoried entrypoints use reusable sessions/hosts; independent main/dynamic/Surface/chat compositions retain their purposes. |
| 8 | Aligned: explicit application/component registration; no new per-input registration. |
| 9 | Aligned after R4: ordinary supported Debug paths produce fresh graphs; load-only remains explicit. |
| 10 | Aligned source after R2/R4: shipping consumes prior matching supply; producer absent from tested consumer compile graphs. Release packaging/runtime NOT RUN. |
| 11 | Aligned: ordinary independent RAM USER/LOCAL/BACKING; retained historical DirectoryDomainStorage tools are headless fixtures. |
| 12 | Aligned source: accepted platform lifecycle persists authorized native partitions; chat business/history/pending receipts stay RAM-only. |
| 13 | Aligned: same native object/layer/history format; no new UI JSON/INI/sidecar graph. |
| 14 | Aligned after R6: concrete NodeFor/live owner references, operation-local hydration graph and Android Activity ownership preserve identity and correct native teardown. |
| 15 | Aligned: prepared topology objects retain identities, stale removals are harmless, clone regressions retained. |
| 16 | Aligned after R1: immutable supply definitions stay outside USER/LOCAL; compatible default/override/Clear checked in independent consumers. |
| 17 | Aligned: byte-skip/batch hydration retains lazy flags and existing object identity; close/reopen tests exercise the changed boundary. |
| 18 | Aligned: immediate compaction/history/replay retained; RAM ACK/checkpoint promises and native snapshot rejoin do not invent business disk durability. |
| 19 | Aligned after R3/R6: close/remount/remove differ; Android configuration remount retains Session, finish/relaunch has fresh RAM and Copy state, stale teardown is rejected. |
| 20 | Aligned: Surface active language/custom labels retained, chat remains fixed English with required localization links. |
| 21 | Aligned: shared LOCAL baseline/monitor/placement services and native measurement; equal-data frame intent survives exact-byte skip. Hardware matrix NOT RUN. |
| 22 | Aligned source with scoped execution: same headless model plus separate native tests; final ports/consumer evidence must not be inferred from Windows. |
| 23 | Aligned: new application schemas S0, matching supplies/fresh profiles, historical S1 tests remain separate. |
| 24 | Aligned after cleanup: no temporary diagnostic/hold subsystem remains; no new recovery/hardening; side effects execute outside assert. |
| 25 | Aligned: components have real consumers, participants reuse the existing list; native accessibility names supplied; Apple source contracts retained, runtime excluded. |
| 26 | Aligned source/toolchain: C++20/no compiler RTTI/exceptions, runtime handles separate, v143 and static import evidence; actual Win7/hardware NOT RUN. |
| 27 | Aligned: incremental existing Debug caches, justified narrowly owned link-scratch cleanup recorded; auditor performed read-only inspections/hashes. |
| 28 | Aligned: authorized SharedNode/SDK stays separate; explicit request-identity fresh-replica rejoin fixes RAM lifecycle without new unrelated mechanisms. |
| 29 | Aligned reporting: source versus execution, failed attempts, final reruns and unexecuted environments stay distinct; pending scopes are not PASS. |
| 30 | Aligned: Astra audit/document only, Sol implementation; root owns commit/push; no auditor code edit, build, commit or merge. |

### Final evidence addendum: rebuilt consumers and quiet chat

The following later records were read and checked by the auditor; the 30 source
dispositions above are unchanged.

- `.artifacts/baseline-windows-final-ctest.log`: current WindowCatalog native
  acceptance, adaptive chrome and MinimalApp native acceptance all PASS, 3/3,
  25.37s (17.41s, 5.12s and 2.66s). The final incremental build log completes
  the rebuilt executables and fixtures. These are affected existing-consumer
  regressions against the corrected shared source.
- `.artifacts/baseline-linux-final-runtime.log`: actual GTK Surface, current
  WindowCatalog L3 and MinimalApp two-cycle native smokes each end in PASS.
  The Surface result includes surviving XIDs, Add/Close/RU, orientation/native X
  and cold USER/LOCAL topology/placement; existing Catalog and MinimalApp retain
  their named historical smoke boundaries under X11.
- After removal of per-send UI-session timing, the rebuilt quiet Windows chat executable
  hashes to `939FE0371323D12F3EAC190F2ECEB4487EA6C9B4C3A55AB913FDCD1D03896F42`,
  independently recomputed by this auditor.
  `.artifacts/chat-final-quiet-cloud.log` records two actual native peers,
  admission, one native cloud send and clean shutdown PASS. The earlier full
  three-peer result retains its earlier binary scope; this focused rerun covers
  the final timing-removal change without falsely calling it a second full
  three-peer matrix. The refreshed root static PE report retains subsystem 6.00
  and zero searched known post-Win7 direct imports.
- The four rebuilt current WindowCatalog/MinimalApp executable hashes were also
  independently recomputed and match
  `../outputs/all-apps-ui/baseline-final-pe-audit.json`. Their static report records
  no searched known post-Win7 direct imports, Catalog subsystem 6.01 and
  MinimalApp subsystem 6.00. Together with the seven migrated legacy variants
  and quiet chat, this is twelve Windows application variants inspected
  statically; none represents an actual Windows 7 run.
- `.artifacts/baseline-web-surface-final.log`,
  `.artifacts/baseline-web-catalog-final.log` and
  `.artifacts/baseline-web-minimal-final.log` each end in PASS after the final
  56-step Web rebuild. Surface covers DOM Add/Close/current/identity, language,
  orientation, hidden USER/LOCAL, cold state and shutdown. Current Catalog covers
  native input, local layout/appearance, child Close, native partitions, two
  cold restarts and fresh Debug. MinimalApp uses the explicit load-only consumer
  for its three cold cycles and verifies emval handles return from 11 to zero
  on stop. An earlier harness run incorrectly used fresh Debug for persistent
  acceptance; that failure remains in
  `baseline-web-minimal-normal-debug-mismatched-harness.log`. No change to correct
  fresh-Debug behavior was made to satisfy that harness.
- `.artifacts/generic-chat-web-runtime.log` records PASS on the rebuilt post-R5
  actual Chrome chat: native Copy/Join, two-peer send, rapid input/caret, Enter
  once, button focus/new draft, native LOCAL, fresh-RAM restart and same-SDK
  identity rejoin/send. This closes R5's browser execution gate; the initial
  malformed-artifact attempt remains excluded from PASS. Its semantic-name
  assertions inspect DOM ARIA properties; they are not a full browser
  accessibility-tree or physical screen-reader test.
- The final `.artifacts/generic-chat-linux-runtime.log` includes the five-step
  rebuild after the ATK label-name correction and ends in GTK native
  keyboard/input/caret/remount/Enter/button, fresh-RAM restart and LOCAL cold
  restore PASS. The fixture directly asserts the native ATK composer name.
  No ATK critical appears in this final run; a window-manager active-window
  query warning is retained in its log. This is native widget/API evidence,
  not a physical screen-reader run.

Final Android results remain separate from these records until their platform
evidence arrives.

The final test-only source review covers the new native Android instrumentation
and two missing `&local_storage` lambda captures in the existing MinimalApp
pending-background-save race probe. Both captures refer to function-owned RAM
storage whose lifetime encloses host shutdown/join; they do not change product
persistence. The instrumentation and launch fixture normalize the font baseline, wait for a
replacement Activity and native controls, and checks Unicode, semantic names,
caret, Send and fresh-session Copy through native APIs. Its optional explicit
recreate fallback does not certify automatic OS-triggered font-change
recreation. The corrected run verified exact two-way Unicode but then timed out
waiting for the remounted composer; this attempt is NOT PASS while its observer
and runtime lifecycle are investigated. Earlier malformed test literals and
observer attempts remain retained failures, not product Unicode regressions or
successful lifecycle evidence.

The subsequent fixture follows the latest created Activity rather than retaining
a stale replacement. That run passes rotation, native SP size increase,
nonzero-revision draft restoration and a new Send received by the external peer.
It then times out at `fresh session Copy enabled` after finish/relaunch; the full
run remains NOT PASS. Final acceptance must resolve that fresh-session boundary
and record a successful completed run rather than combining partial attempts
into a blanket PASS.

### Android ownership follow-up R6

The root requested a bounded comparison after that failure. Chat
`MainActivity.onDestroy` calls `stopNative` for any finishing Activity even when
`current` already references its replacement; JNI `stopNative` ignores the
caller and resets the global Session and host. Consequently an obsolete
finishing Activity can tear down a newly attached Activity's Session if those
callbacks interleave. The actual failed run's ordering is not yet established.
Current Catalog explicitly checks the old Activity/listener before clearing its
reference, and the baseline Application-owned runtimes are not destroyed from
Activity `onDestroy`.

This source ownership path reopens the rule 14/19 lifecycle disposition until
corrected and reviewed. Preventing stale teardown alone is insufficient:
`startNative` also remounts any existing Session, so a new Activity attaching
while its predecessor is already finishing can inherit its business RAM. Final
validation must distinguish configuration remount (retain Session) from finish
and relaunch (fresh Session), in addition to successful native Copy. No auditor
implementation change was made.

Sol's R6 correction was subsequently read and accepted at source level. Before
replacing JNI host references, `startNative` checks whether their old Activity
is finishing. If so, it stops the Session, drains accepted work, saves native
LOCAL, joins the worker and destroys the UI while the old host references remain
valid, then creates a fresh Session. A nonfinishing configuration replacement
still remounts the existing Session. Java current-owner checking and JNI
`IsSameObject` guard teardown; a stale Activity's checkpoint is an explicit no-op.
The rule 14/19 source dispositions are restored by this correction. Its Android
native execution gate remains open; shared desktop/browser source and evidence
are unchanged.

The rebuilt post-R6 APK now closes that native gate. The auditor read
`.artifacts/generic-chat-android-native-runtime.log`, which ends in
`NATIVE_ACCEPTANCE_PASS`, the complete native scope PASS and
`INSTRUMENTATION_CODE: -1`. It records exact two-way Unicode with the external
peer, native semantic names/read-only transcript/list, caret and Enter once,
rotation and font scale 1.0→1.15 with increased native text size, replacement
Activity draft restoration and subsequent edit/Send/focus. Immediate same-process
finish/relaunch has empty business/draft state; explicitly invoking the obsolete
owner's JNI stop is ignored, and the fresh Session's first Copy replaces the
clipboard sentinel with its UID. This closes R3 and R6's executed lifecycle
boundaries. Native API assertions do not imply physical screen-reader or IME
certification. The earlier failures remain retained under their original attempt
logs; final long-burst, separate LOCAL cold restore and existing-consumer Android
checks remain separate pending scopes.

Final test-registration review also closes a bounded generator mismatch:
`runtime_ui_build_policy_test.py` reads `build.ninja`, so its CTest registration
is now guarded by `CMAKE_GENERATOR MATCHES "^Ninja"`. The two independent native
supply-consumer tests remain registered for all supported Windows generators.
The auditor read the existing `build-generic-legacy-win7` cache's
`Testing/Temporary/LastTest.log`: the focused Ninja policy test passed in 0.10s
at 11:47 on 17 November. This changes only test registration/documentation,
without new product binaries, mixed-configuration support or Release claims.

The next Android sequential run repeats native 3- and 15-character adb input
successfully, but its added unpaced 40-character stress probe fails: the observed
focused composer contains `abcdefghijklmno` (15 characters), not the expected
40-character string. `.artifacts/generic-chat-android-final-sequential.log`
preserves that NOT PASS; the wrapper stops before its later LOCAL/Catalog/Minimal
stages. This is an additional stress probe, not a failed rerun of the complete
native lifecycle fixture above. No product rollback, injection expiry or other
root cause is established by this observation. Do not report all Android tests
PASS or treat the 3/15-character results as evidence for forty characters.

The root then ran the existing Android consumers independently of the failed
stress/menu wrappers. The auditor read
`.artifacts/android-window_catalog-root-final-runtime.log`: Catalog 2/2 PASS in
136.422s (`addSwipeCloseReopenDeleteUsesModelAndDoesNotSave` and
`localizationReachesCatalogAndExistingChild`), instrumentation code -1.
`.artifacts/android-minimal_app-root-final-runtime.log` records MinimalApp 2/2
PASS in 51.651s (`rowBSelectionThenApplyUpdatesTitleAndMirror` and
`activityRecreationKeepsActiveDetailAtSameViewport`), also code -1. The summary
JSON agrees. These close the selected final shared-consumer Android regression
scope; they do not rerun historical Recents cases. Chat's separate LOCAL cold
restore is the remaining required Android result.

The final separate Android LOCAL modes now close that required result.
`.artifacts/generic-chat-android-local-prepare.log` and
`.artifacts/generic-chat-android-local-verify.log` both report PASS and
instrumentation code -1. The auditor read both and the launch fixture. It first
verifies that the new UI profile has no LOCAL file, while linking only an owned,
already-tested SDK transport identity inside the application's private files.
It does not seed business state or copy LOCAL data. A real Activity menu action
selects Dark, its published native menu state is checked, a message and draft
exist before finish/onStop, and the ordinary `local/user.bin` is saved without
business files. The verify phase runs in a different process (recorded PIDs 3126
and 3242), with the same nonempty SDK UID, restored native Dark selection, empty
history/draft and no business files. A prior new-SDK-profile attempt timed out
before connection and did not execute LOCAL acceptance.

The combined LOCAL wrapper subsequently stops in the optional long-input mode
at its font-scale prerequisite, before attempting input; this does not invalidate
the two completed LOCAL modes or establish a second forty-character input
failure. The original unpaced adb40 NOT PASS remains explicit and unchanged.
The selected native migrations and shared-consumer regressions are complete;
there is no blanket Android performance, physical IME, accessibility hardware,
Win7, Apple or Release certification.

The last optional long-input diagnostic moved the unrelated font baseline
assertion back to the full font/lifecycle mode; no product source changed.
`.artifacts/generic-chat-android-long-native.log` records `IME40_PASS`: native
`InputConnection.commitText` accepted all forty characters and editor-action
Enter produced one transcript entry and cleared the composer. The next
`Instrumentation.sendStringSync` case observed all forty characters exactly and
retained focus, then timed out at `key forty model/send` after
`sendKeyDownUpSync(KEYCODE_ENTER)`. The overall diagnostic is NOT PASS, code 0.
This neither clears the earlier adb40 failure nor establishes its cause. Both
extra stress failures remain outside the successful required scopes, and no
blanket hardware-key/IME performance claim is made.
