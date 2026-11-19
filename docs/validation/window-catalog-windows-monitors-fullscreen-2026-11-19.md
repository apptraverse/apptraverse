# Windows WindowCatalog monitor placement and fullscreen

The Windows catalog uses the existing device LOCAL root for display preferences and observes the current Windows desktop before its first publication. Each window has a preferred monitor, a resolved destination, an observed current monitor/DPI and a normal frame in DIPs. Disconnecting B resolves its windows to the primary monitor. Reconnecting B returns them unless a completed user move has selected C. Close preserves the window identity and its placement. Delete removes the window’s LOCAL records and prevents late native observations from recreating them.

The Windows supply provides an English/Russian fullscreen action for the main window and for each child. Its native button is at the right of the client toolbar directly below the system caption. F11 toggles fullscreen and Escape exits. Monitor-sized bounds, non-client styles and the current session’s maximize/restore information belong to the presenter. The normal placement remains in LOCAL while fullscreen and maximized windows publish their actual monitor. The menu remains accessible in fullscreen.

`CatalogUiState` keeps fullscreen window IDs in its current S0 LOCAL layer. The root class identity stays the same across platform supplies. The Windows supply opts into a constant menu definition containing the actions and toolbar constraints. Other adapters keep their existing UI. A USER-only transfer excludes fullscreen and monitor preferences. Old experimental LOCAL schemas have no migration.

## Windows Debug verification

The runs used the existing VS 2022 v143 14.44 Ninja Debug cache, the ordinary Debug producer and the matching embedded load-only consumer. The host exposed two active monitors. The native tests supplied topology changes to the model and moved real HWNDs between those monitors while both outputs remained enabled.

| Executed boundary | Observed result |
|---|---|
| Device Events and storage | Three modeled displays, B missing/returned, user move to C during absence, per-window isolation, Delete, late observation, replay and cold restore passed. |
| Native placement | Independent HWNDs on two monitors, normal and maximized bounds, deferred placement while minimized, drag concurrent with publication and cold restore passed. Starting and ending a move without changing the frame, or cancelling a move, kept the preferred monitor. A deferred monitor return applied on exit from the move loop. An explicit frame request took priority over the monitor return. |
| Catalog UI and topology | Native entry/exit for main and child, localized labels, preservation of normal placement, pinned exit, fullscreen fallback/return and exit on fallback passed. |
| Process restart and frame restore | Real load-only processes restored the main and child windows in fullscreen. Exiting restored both original normal positions and client sizes within the native test tolerance. A further cold launch restored those frames in normal mode. Importing only USER opened both windows in normal mode. The targeted scenario and the full native Win32 acceptance test passed. |
| Model and supply | Proxy, incremental UI mirror, replay and immediate folding, Close/Open/Delete, LOCAL-only storage and fresh-destination seed in both directions between current supplies with different capabilities passed on Windows. |
| Independent Windows consumers | MinimalApp native acceptance, frame synchronization and publication, interactive drag, native command routing, message host, layout and binary-policy regressions passed. |
| Interactive application | Clicked entry and exit and pressed F11 and Escape in the built Debug application. Windows UI Automation exposed both localized-action states as buttons and exposed native Minimize, Maximize and Close again after exit. The interactive run used English. |
| Static Win7 target | The load-only Debug PE has GUI subsystem 6.01. The new direct display configuration imports are GetDisplayConfigBufferSizes, QueryDisplayConfig and DisplayConfigGetDeviceInfo. |

Physical cable disconnect/reconnect, a third physical monitor, actual Windows 7 and Narrator speech were not exercised.

## Android USER-only transfer

The current DebugLoadOnly build ran on the Android 14 API 34 x86-64 emulator. Actual Windows load-only processes saved a series of profiles with two, three and four children sharing the same holder and window ObjIds. Each Windows snapshot had the main window and the first child fullscreen in its own LOCAL.

The Android baseline loaded the three-child USER snapshot and saved its own Russian locale, soft dark theme and display observations. Each transfer started from a separate copy of that Android baseline. The test replaced only `user.bin` and verified that `local/user.bin` remained byte-identical at the copy boundary. Native catalog contents and saved state were inspected after launch and a second cold launch.

| Windows USER applied to the Android three-child profile | Result |
|---|---|
| Two children | The catalog displayed the expected two children with their original ObjIds. Android kept its locale, theme and display baseline. The old third child's LOCAL observation and placement remained in storage. The catalog had two rows after the second launch. |
| Four children | The catalog displayed all four expected children. Android kept its existing observations and added a native observation and placement for the fourth child. The catalog had four rows after the second launch. |

Both imports retained the emulator's 420 DPI and 1.15 text scale. Both recorded zero fullscreen windows in Android LOCAL. A separate USER-only import on a fresh profile also passed on the emulator. The test used current S0 profiles and matching supplies.

## Canon review

GPT-6 Astra ultra reviewed the Windows delta against §25 of the [architecture Canon](../architecture/AppTraverse_Architecture_RU.md). GPT-6.1 Sol high implemented the changes. During review, a fullscreen move was found to suppress the native callback that would report the new monitor. The presenter now reports the resulting HWND observation explicitly. The two-monitor native test checks the actual monitor on both transitions.

| Rules | Reviewed boundary |
|---|---|
| 1–4 | Distilled required links and native creation, explicit authored display state, and handling of genuinely unavailable monitors, queued input and lazy windows. |
| 5–8 | Native intents use the proxy, LOCAL Event and publication. Constraints and behavior belong to the model. The backend accepts an optional fullscreen action through its existing registration lifecycle. |
| 9–13 | Ordinary Debug fresh start, embedded consumer, ordinary partitioned RAM storage, shutdown save and native serialization. Release packaging was outside this run. |
| 14–17 | HWND/style lifetime, child clone identity, immutable label and toolbar supply, observation and cleanup by ObjId without opening closed children. |
| 18–22 | Immediate folding and replay, Close/unload/Delete, active-language labels, model resolution of placement, independent model and native tests. |
| 23–27 | Current S0, native failure handling, Windows-only capability selection, runtime-only OS resources, incremental Windows Debug builds. |
| 28–30 | Existing network scope, explicit distinction between runtime and source evidence, feature-branch delivery under the standing main publication instruction. |

## Follow-up review of the implementation at `20c37583`

GPT-6 Astra ultra reviewed all 30 rules in Canon §25 against the WindowCatalog Windows monitor/fullscreen change, its common model and the corresponding presenter contracts. GPT-6.1 Sol high implemented the corrections. The following are source dispositions for that scope.

| Rules | Source disposition |
|---|---|
| 1–4 | The Windows presenter consumes the layout and device links supplied by composition. Native readiness and stale input retain their lifecycle checks. The first window observation explicitly assigns the previous-value field serialized by its Event. |
| 5–8 | Commands use the model proxy. The model authors LOCAL state, the UI mirror publishes the result to native chrome, and the common template and registration boundaries are preserved. |
| 9–13 | Debug fresh and load-only policies, immutable embedded supply, independent RAM stores, native serialization and lifecycle-boundary persistence remain in place. |
| 14–18 | Native handles belong to the live presenter. Identity, supply constants, unloaded child references and Event folding/replay retain their existing contracts. |
| 19–22 | Windows now filters late observations using current USER membership. Explicit Delete removes a window’s LOCAL records. Importing a smaller USER leaves the destination’s records available if the same identity returns. Active language and placement resolution continue through the model. Model and native tests cover distinct boundaries. |
| 23–26 | The fixes use the existing S0 schema, native Win32 controls, error handling and compiler policy. The HWND’s nonclient frame is recalculated on fullscreen entry even when the outer rectangle already matches the monitor. |
| 27–30 | Verification uses existing incremental Debug configurations. The changes introduce no network dependency. Execution evidence below has its own scope; publication follows the standing main instruction. |

Three regressions failed before their respective fixes:

- Starting with a smaller imported USER advanced the LOCAL generation and removed the absent window's records. The model test exercises 3 → 2 → 2 (cold restart) → 3 using serialized USER snapshots, both with immediate folding and with retained journal events. It also checks stale native input, monitor loss during absence and monitor return.
- A window sized exactly to the monitor retained its old client origin on fullscreen entry. The native frame test checks the client origin and extent on entry and after exit.
- Giving keyboard focus to a scrolled list in a 300 × 180 client placed its top behind the fixed fullscreen action. The presenter now scrolls the focused control into the visible content below the action. The native test measures both control rectangles.

### Executed Windows Debug checks

- All 11 selected CTest targets passed with the v143 Debug build. They cover native frame synchronization/publication, two-monitor placement, constraints, layout state, device Events, WindowCatalog native acceptance and adaptive chrome, menu model, message host and command routing.
- The full WindowCatalog model executable and the native interactive-drag executable passed separately.
- Three separate load-only processes exercised a destination profile with 3 → 2 → 3 child windows. After enabling fullscreen on the third child, each import replaced only `user.bin`; the destination's `local/user.bin` remained byte-identical at each copy. The third window disappeared with the smaller USER, returned fullscreen with the larger USER, and exited to the original normal rectangle within two pixels. This used an isolated copy of the Windows native transfer fixture.

Actual Windows 7, physical display disconnection, Narrator speech and MinSizeRel were outside this follow-up run.

## Broader Windows Canon corrections

- Catalog graph traversal and layout commands now use the inherited
  `UiPresentedWindow::layout_state` edge. Existing source consumers can read the
  same edge through `CatalogWindow::layout`. The native S0 archive still writes
  the existing trailing reference as a projection of that edge. The headless
  test verifies the single reflected edge and compares the serialized layer
  bytes with the original S0 field sequence. Separate Windows processes verified
  that the new consumer reads a previous three-child USER/LOCAL profile and the
  previous consumer can read a fourth window created by the new consumer.
- Windows Catalog uses the window, list, button and menu registrations it needs.
  Existing aggregate entry points retain optional components for their
  consumers. The MinSizeRel PE and link map passed the tightened runtime policy,
  including exclusion of the text control classes and the separate Catalog
  language button. On this cache, the executable went from 911,360 to 885,248
  bytes.
- The Windows loading shell initially displays the product name. It reads its
  localized caption and message from the first published mirror and paints them
  before mounting native presenters. Host and loading-window tests passed.
  Actual Debug load-only and MinSizeRel cold launches with Russian LOCAL displayed
  the Russian startup caption and opened the Russian main window.
- Desktop order capture checks the native enumeration result before submitting
  the order to the model. Eleven Windows Debug tests passed in the selected
  suite, including the full model and adaptive chrome tests. The twelfth, native
  acceptance, passed in a separate run after the parallel run could not find an
  exposed title-bar point for its focus setup. The enumeration failure branch was
  reviewed in source.
