# Seven-platform Canon review

The review uses `68019646` as its baseline and follows §20 and all 30 rules in
§25 through WindowCatalog, the shared runtime, and the equivalent
MinimalApp entry points. The SharedNode chat change covers the same
publication teardown boundary in its authorized scope.
The integration also includes the intervening UpdateLab commit `38b102f1`.
Rebasing preserved the audited framework and application sources byte for byte.

## Corrected findings

| Rules | Finding and correction |
| --- | --- |
| 4, 5, 14 | Android could block its main looper joining the model worker while the worker finished serializing a publication. Terminal teardown closes submission and publication under the runtime lock. Accepted work drains before the session saves, and an abandoned receiver cannot acquire another snapshot. SharedNode chat drains snapshots during transport shutdown and closes the receiver after transport completions. [Runtime](../../src/application_model_runtime.cpp), [Android host](../../src/platform/android/android_application_host.cpp), [chat session](../../examples/shared_node_chat/chat_ui_session.cpp). |
| 7 | Linux, Web and CTest used the embedding application's CMake source root for AppTraverse-owned files. Examples and fixtures now resolve files against the AppTraverse project. Metadata helpers locate their generator scripts beside the module and accept caller-relative configuration inputs. [Web consumers](../../examples/window_catalog/web/CMakeLists.txt), [CTest](../../tests/CMakeLists.txt), [metadata helper](../../cmake/apptraverse_embed_release_metadata.cmake). |
| 7, 26 | The MinimalApp Android Gradle project passed a Windows-only local dependency path and overrode the repository pin. It now uses the repository pin or the caller's existing initial-cache mechanism. [Android build](../../examples/minimal_app/android/app/build.gradle). |
| 8, 25 | Linux, Android and Web MinimalApp repeated registration during graph production or startup and anchored unused factories. Their product, supply producer and independent test entry points each call the appropriate application registrar before loading or building the graph. [Linux](../../examples/minimal_app/linux/minimal_app_linux_registration.h), [Android](../../examples/minimal_app/android/minimal_app_android_registration.h), [Web](../../examples/minimal_app/web/minimal_app_web_registration.h). |
| 10 | Linux constrained static C++ runtime flags to the optional minimum-size profile. The existing compile policy now propagates them to C++ consumers in Release, RelWithDebInfo and MinSizeRel. GTK and the other system libraries retain their native linkage. [Compile policy](../../cmake/apptraverse_compile_policy.cmake). |
| 4, 6, 20 | WindowCatalog invalidated the Android options menu after every publication, including viewport updates that left all commands unchanged. The native projection compares the published captions, controls and targets. Android rebuilds the menu for an actual change or a newly bound Activity. The appearance acceptance case exercises the visible native overflow control and its radio selections. [Projection](../../examples/window_catalog/android/native/window_catalog_menu_projection.h), [headless case](../../tests/window_catalog_fullscreen_model_cases.h). |
| 14, 22, 29 | Two Android MinimalApp fixtures failed before their assertions because they supplied a fabricated JNI reference or mounted a presenter with no Activity. The first fixture now owns a real native content reference and tests the UI-owner/worker boundary. The second uses a test-only Activity and checks listener release through real remount and teardown operations. [Worker probe](../../examples/minimal_app/android/minimal_app_android_model_thread_start_probe.cpp), [presenter fixture](../../examples/minimal_app/android/app/src/androidTest/cpp/presenter_lifecycle_fixture.cpp). |

## Canon dispositions

| Rule | Source review |
| --- | --- |
| 1 | Composition and Load establish required control, layout, device and localization links before presentation. |
| 2 | Native resource creation and mandatory graph loading retain explicit failure boundaries. |
| 3 | Builders, Events and Load own persistent fields. The new stop state belongs to the runtime. |
| 4 | Terminal teardown handles an active serialization and a queued snapshot explicitly. |
| 5 | Native input reaches the model through the proxy. Presenters consume published state. |
| 6 | Native widgets supply measurements and placement observations to the shared layout path. |
| 7 | Independent applications reuse the hosts and session machinery. Nested CMake resolves AppTraverse-owned files. |
| 8 | MinimalApp Linux, Android and Web use a single application registrar per entry point. Windows and Apple already use their application registrars. |
| 9 | Fresh Debug and explicit load-only consumers retain their distinct launch policies. |
| 10 | Shipping targets consume embedded supply. Linux shipping consumers link the C++ and compiler runtimes statically. |
| 11 | USER, LOCAL and BACKING use ordinary RAM stores with their existing ownership scopes. |
| 12 | Desktop shutdown, Android backgrounding, Web visibility/pagehide and iOS deactivation retain their native save boundaries. |
| 13 | Native graph serialization and the existing USER/LOCAL representation carry the model. |
| 14 | Runtime shutdown and native host ownership keep the graph and storage alive while accepted work drains. |
| 15 | Domain lookup and Clone continue to use native identities and shared external references. |
| 16 | Scope metadata and overlays preserve supply constants and USER ownership. |
| 17 | Lazy windows and localization links remain available without forcing unrelated native activation. |
| 18 | Immediate event compaction and historical base/journal/replay remain active. |
| 19 | Close, unload and delete have separate model and presenter lifecycles. |
| 20 | Required localization follows the published active language. |
| 21 | Platform measurements and work areas reach model Events. Native conversion uses the backend's coordinate capabilities. |
| 22 | Headless runs the same model. Native acceptance exercises the input and presentation route separately. |
| 23 | WindowCatalog and MinimalApp continue using S0. UpdateLab retains its explicitly authorized versioning scope. |
| 24 | The teardown fix uses existing synchronization and channel ownership. Test gates stay in the fixture. |
| 25 | MinimalApp anchors its window, button and list factories. Other consumers retain their own required registrations. |
| 26 | The C++20 compile policy disables RTTI and exceptions. Native handles stay in runtime state. |
| 27 | Targeted builds reuse pinned package sources and run incremental verification. |
| 28 | Optional transport and UpdateLab retain their authorized ownership and build switches. |
| 29 | The platform results below record the executed scope. |
| 30 | Integration uses the feature branch and the existing main history. |

## Executed verification

| Target | Environment and checks |
| --- | --- |
| Windows | 22 selected Debug CTests passed across two runs, including native Win32 acceptance and adaptive chrome. The model stress test exceeded the initial 180-second limit and passed on the separate 600-second run (427.71 seconds). |
| Linux/GTK | Debug in WSL2. Four CTests passed on GTK/X11 with Openbox. MinimalApp saved and cold-restored its embedded supply profile in two processes. WindowCatalog passed the WSLg Wayland sequence for fullscreen exit, child and main geometry, and two cold launches. |
| Headless | Five Linux CTests passed, covering the application runtime, storage, and the two embedded application graphs. The runtime tests explicitly hold an initial serializer, an incremental serializer, and a queued publication across teardown. Windows and Node also ran the runtime tests. Windows and Linux both passed the added Android menu-projection regression. |
| Web | Two Node checks and four Chrome runs passed. The browser runs covered embed/load-only, localization, window layout and fullscreen, LOCAL, MinimalApp remount and cold restore, and real hidden, pagehide, and browser restart boundaries. |
| Android | Eleven distinct WindowCatalog cases passed across the x86-64 API 34 emulator runs. The final menu build passed targeted checks for localization, language persistence, layout actions, all four theme choices, and fullscreen across Back, rotation, and cold LOCAL restore. The corrected fullscreen fixture waited for both checkpoint files before killing the demo process. MinimalApp results follow below. |
| macOS, iOS/iPadOS | Source review only, covering SwiftUI entry points, the shared model and storage path, registrations, and publication shutdown. Build, simulator, and device execution are NOT RUN because the Windows/WSL host has no Xcode or Apple target. |

An external CMake parent configured and generated both Linux and Web builds
with `add_subdirectory`, examples, and tests enabled. A separate parent built
both metadata headers with a caller-relative JSON path and asserted the
embedded release number. A regular Linux Release
WindowCatalog linked with the minimum-size profile disabled. The executable
and map passed the shipping binary inspection, including embedded supply and
the absence of shared `libstdc++` and `libgcc_s` imports.

Four native fixture builds (Windows, Linux, Android x86-64, and Emscripten)
seeded their own profiles and accepted all 16 source/destination combinations
with their input partitions unchanged. Separate Windows ↔ Android transfers
kept the destination's existing LOCAL appearance and used the optional seed
only for an empty destination. The layout constraint override was verified
through USER. This is model and storage evidence from the
[portable fixture](../../tests/window_catalog_portable_user_fixture.cpp).

The opt-in Windows SharedNode chat UI translation unit compiled against the
pinned SDK. Its in-memory chat CTest passed. The application-runtime teardown
tests exercise the publication boundary without a transport or GUI dependency.

On the Android x86-64 API 34 emulator, MinimalApp passed the startup and
Activity-remount cases after an initial emulator ANR, the two label-loading
probes, and both corrected scheduling and presenter-lifecycle fixtures. A
separate DebugLoadOnly run sent native Row B and Apply input, observed USER and
LOCAL on disk after Home, restored the `Applied` state in a new demo PID, and
accepted new row input. Two older `SaveColdReloadInstrumentedTest` cases did
not pass in the initial fresh Debug run. One depends on a historical save log,
and the other lost the Row A control before its overlap assertion. The direct
load-only run covers ordinary cold reload. The overlap case remains without a
PASS in this run.

Execution on Windows 7, physical Android hardware, a native screen reader,
and an OS image upgrade remains NOT RUN. The Android execution excluded the
historical Recents-dismissal cases. Historical evidence retains its original
scope.
