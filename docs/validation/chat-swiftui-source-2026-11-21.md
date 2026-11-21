# Apple chat SwiftUI port — source review, 21 November 2026

Base: freshly fetched `origin/main`, `d621e1fa`. Work is isolated in
`fix/chat-swiftui-canon-20261121`; previous Apple worktrees were preserved.

The user explicitly prohibited building. No configure, build, compiler,
type-check, product launch, simulator operation or runtime test was performed.
All conclusions below are source-review conclusions. Previous build/runtime
evidence is historical and does not validate this revision.

The current main already supplies SwiftUI chat content and shared Apple model
publications, but macOS still manually creates NSApplication, NSWindow,
NSHostingView and NSMenu. The port moves app/window/command ownership into
SwiftUI while retaining native OS hooks at the window/lifecycle boundary.
SwiftUI continues to own Apple content, lists and layout, as required by current
AGENTS.md; the old November 17 custom Apple presenter composition is not restored.

The required read-only GPT-6 Astra ultra audit examines all thirty Canon §25
rules. GPT-6.1 Sol high owns source corrections. The source scope includes the
Apple bridge, shared SwiftUI presenter/content, macOS and iOS lifecycle, shared
chat session and the analogous macOS native placement boundary. Runtime
acceptance and artifact/linkage checks remain NOT RUN.

## Findings and final source review

The audit reread the final changed sources after the implementation was frozen.
No unresolved blocking source defect was identified in the examined scope.
This conclusion does not establish compilation or runtime correctness.

| Finding | Rules | Source correction and review outcome |
| --- | --- | --- |
| macOS manually owned the app, window, hosting view and menus despite the current SwiftUI instruction. | 6, 7 | `macos/ChatMain.swift` now uses `App`, `Window` and `Commands`; the native attachment observes the SwiftUI-owned window without replacing its delegate. SwiftUI retains content and layout ownership. Native focus, outer placement, APNs and termination remain boundary operations. |
| macOS reported native frames into the portable `ChatUi` window, while the session saved only LOCAL roots. Actual placement therefore had no LOCAL persistence path. Both Apple ports lacked an OS baseline in their chat composition. | 5, 12, 21 | `Session::PrepareDeviceEnvironment` accepts a per-session native snapshot before `Start`; prepare applies it after LOCAL loading and before the first publication. macOS observations now use `UiDeviceState` Events and apply its published monitor resolution/outer placement. iOS supplies initial and live SwiftUI settings and logical viewport observations to the same LOCAL object. Native viewport measurements do not change the portable semantic frame. |
| Apple Copy wrote the clipboard before the existing model Copy Event/publication. | 5 | `ChatBridge` invokes `ChatUi::Copy`; `ChatPresenter` consumes the published `copy_request` once and reports the native result through the proxy. Native text revisions still protect pending edits from older publications. |
| Typed transport status silently fell back to English when the required label graph did not match an older supply. | 1, 20, 23 | `ChatUi::AcceptTransport` directly resolves required localized labels for typed statuses. `Unspecified` remains a real diagnostic state. No migration, graph repair or replacement supply format was introduced. |
| iOS had no explicit application termination path, and checkpoint wording still described a business USER save. | 2, 12, 14 | The application delegate forwards explicit termination to session stop/drain/save. Ordinary inactive/background transitions retain the live session and checkpoint LOCAL. Comments and diagnostics distinguish LOCAL from RAM-only chat business state. |
| Replacing the owned macOS window made existing acceptance force unwraps incompatible and exposed delayed acceptance callbacks to teardown. | 4, 14, 22 | The acceptance adapter reads optional native window/content, guards stopped/unmounted states, and uses weak ownership for queued button presses. It still drives the actual SwiftUI controls and retains their accessibility identifiers; execution was not performed. |
| iOS bundle metadata labeled every target `iPhoneSimulator`. | 2, 26 | `ios/Info.plist.in` now uses the existing CMake platform value selected for device versus simulator. No signing, packaging or installation was executed. |
| Current SharedNode prose contradicted the RAM-only chat contract. | 12, 28, 29 | The current contract and chat README now distinguish model localization, native LOCAL persistence, separate SDK identity and RAM-only business/history/delivery state. Historical disk-persistence evidence remains historical. |

The reusable macOS window-observation extraction preserves the existing
coordinate conversion and actual-window DPI calculation. The reusable iOS
settings extraction preserves the existing capability flags, display scale,
text scale, appearance, contrast, reduced-motion and VoiceOver mapping. Their
existing generic backend call sites were reviewed alongside the new chat callers.
The new macOS header explicitly declares `NSWindow`; iOS Swift compilation is
given the include path needed by the shared Objective-C settings declaration.
No schema, generated supply or native serialization format changed.

## Canon §25 dispositions

“Source aligned” means no contradiction was found in this bounded source review;
it is not a runtime PASS. “Source limited” identifies a rule whose complete
acceptance requires evidence outside this review. Current AGENTS overrides are
applied explicitly rather than treating historical Canon wording as a new task.

| Rule | Disposition in the examined scope |
| --- | --- |
| 1. Complete graph | Source aligned after removing the old-supply status fallback. Required model labels, controls and device links are consumed directly; no repair or repeated Ensure path was added. |
| 2. All or nothing | Source aligned. Profile/start failures, LOCAL save failures and encoding failures retain fatal/failed boundaries; device/simulator plist selection is corrected. Failure execution is NOT RUN. |
| 3. Explicit persistent defaults | Source aligned. Existing builders initialize model/Event data; added native observation and session fields are runtime state. No constructor default replaces required serialized values. |
| 4. Genuine runtime states | Source aligned. Loading, stopped sessions, missing native attachment, disabled/read-only/hidden controls, live resize, minimization and fullscreen transitions remain real conditions. Late acceptance callbacks now respect teardown. |
| 5. Model is the sole author | Source aligned after corrections. Input, Join, Send, Copy, clipboard feedback and environment/placement observations go through proxy/model Events; SwiftUI consumes the publication mirror. |
| 6. Thin presenters | Source aligned under the current Apple exception. SwiftUI owns Apple content, lists, commands and layout. AppKit/UIKit remain at the native lifecycle/observation boundary; the earlier non-Apple common-solver instruction is not imposed on this Apple port. |
| 7. Shared application template | Source aligned. Chat continues to use the common model runtime/UI session and shared business model. New native conversions live in reusable backend code, which does not depend on this example. |
| 8. Registration once per application | Source aligned. The bridge composition and existing once-only registration guards remain; per-session environment capture is separate from registration. |
| 9. Fresh ordinary Debug | Source aligned. The existing Debug producer builds BACKING in RAM and uses a fresh run directory; explicit load-only remains separate. No configure/build or fresh/cold launch was performed. |
| 10. Release supply consumer/linkage | Source limited. The existing producer/embedded-supply conditional remains. Release contents, unused registrations/modules and binary/runtime linkage were not inspected in a built artifact: NOT RUN. |
| 11. Ordinary RAM storage | Source aligned. USER, LOCAL and BACKING remain separate ordinary `RamDomainStorage` instances. No new store manager or persistence format was added. |
| 12. Platform save boundary | Source aligned after LOCAL/lifecycle corrections. Only native LOCAL is saved by chat lifecycle; business/history/drafts/receipts remain in RAM and SDK identity stays separate. iOS deactivation checkpoints without teardown; explicit termination stops/drains. Persistence execution is NOT RUN. |
| 13. Native serialization | Source aligned. Model, Events and LOCAL retain the existing object serialization. Objective-C dictionaries are transient presentation values, not an additional persistent graph format. |
| 14. Object-system ownership | Source aligned. Session owns runtime/thread/mirror; stop drains accepted work, persists LOCAL, joins and releases the mirror. SwiftUI owns its window; weak callbacks and guarded late acceptance callbacks do not create a keepalive registry. |
| 15. Domains and Clone | Source limited. This change does not alter lookup, Clone boundaries, aliases or identities. The exhaustive Domain/Clone matrix was not re-audited or executed. |
| 16. Semantic constant/dynamic state | Source aligned. Existing immutable supply captions and distinct mutable title slots remain. Native observations are committed to dynamic LOCAL, not to supply objects. |
| 17. Lazy graph | Source aligned. LOCAL routing precedes root Load; required links are materialized by the existing session preparation. Inactive localization slots retain their lazy policy. No unload/materialization runtime matrix was executed. |
| 18. History retained | Source aligned. Existing immediate UI Event compaction and native history mechanisms remain. No history/replay implementation was removed or replaced; SharedNode's separately authorized RAM synchronization policy is preserved. |
| 19. Close/unload/delete distinctions | Source limited. This single chat window closes through application shutdown; no child-window delete/reopen protocol was introduced. General private-presenter cold close/reopen behavior is outside this change and was not executed. |
| 20. Active language | Source aligned after correction. Published labels/title/commands use the active model language without an old-supply fallback. `MaterializeLanguage` resets inactive slots; rendered localization and accessibility are NOT RUN. |
| 21. Placement through Events | Source aligned after correction. Native outer frame/monitor/DPI and mobile logical viewport go to LOCAL Events. The model resolves monitor availability; the macOS boundary applies that published result while preserving normal geometry across transient states. Physical monitor/resize/fullscreen/cold restore acceptance is NOT RUN. |
| 22. Same headless business model | Source limited. The shared session/model is unchanged in role; the separate native acceptance harness is preserved and adapted to SwiftUI ownership. Headless, proxy/native and cloud scenarios are all NOT RUN for this revision. |
| 23. S0 prototype | Source aligned. No S1/S2, old-profile migration or silent profile repair was added. Required status-label consumption removes an old-supply compatibility fallback; generated supplies were not regenerated. |
| 24. Normal environment/side effects | Source aligned. No new retry/recovery/debug-tracing subsystem was added. Native and save effects execute outside assertions, with boundary outcomes handled. Existing authorized transport ACK/retry is not a new hardening task. |
| 25. Used components | Source aligned in the changed source. No speculative UI component or unrelated platform feature was introduced. Release dead-code/module contents remain part of the unexecuted artifact boundary. |
| 26. Technical policy | Source aligned. C++20/no-RTTI/no-exceptions target policies remain, and native windows, subscriptions and callbacks are runtime-only. iOS target metadata is corrected; compilation/linkage are NOT RUN. |
| 27. Build policy | Complied with the stronger task instruction: no configuration, build, compiler, type-check, clean, dependency population, product launch or runtime test. Reading source/SDK declarations and static text/plist checks are not build evidence. |
| 28. Network/future mechanisms | Explicit current authorization applies. SharedNode/chat/Aether are authorized opt-in work despite the older removal wording. No resource-delivery, cache, migration or production-hardening scope was added. |
| 29. Scoped evidence | Complied. Findings were corrected at the responsible boundaries and analogous call sites reviewed. This report separates source review, historical evidence and NOT RUN acceptance; no new runtime PASS is claimed. |
| 30. Publication | Work was reviewed in the isolated feature branch without losing earlier worktrees. The auditor did not commit or push; the coordinating task handles publication under the current user/AGENTS authorization. Source readiness does not certify unexecuted platform acceptance. |

## Checks and remaining evidence boundary

The coordinating task reports a clean final `git diff --check` and successful
`plutil -lint examples/shared_node_chat/ios/Info.plist.in`. It also read installed
SDK declarations to confirm the used `Window(Text, id:)` and `commandsReplaced`
availability. These are static checks only; no compiler was invoked.

Compilation, linking, generated-supply consumption, normal Debug startup,
load-only LOCAL restart, native typing/caret/clipboard/keyboard commands,
deactivation and shutdown execution, cloud delivery, APNs, accessibility,
display changes/fullscreen and actual iOS device/simulator installation remain
NOT RUN. Regressions for the shared model and extracted macOS/iOS backend
helpers also remain NOT RUN. Previous acceptance records retain their original
revision, platform and scenario scope.
