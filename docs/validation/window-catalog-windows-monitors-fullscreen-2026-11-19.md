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
