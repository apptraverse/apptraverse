# Serialization schema v0 reset: inventory and scope

Baseline: `8a5a690d396ff09af8bf75f78487f663e6cd4eb7`.

The isolated branch starts at this baseline. The observed remote
`cursor/surfaces-library-controls` was `e4a0830` (one presenter-routing commit
later); none of that routing work is part of this reset. The Windows worktree,
its uncommitted files, stash, acceptance BMPs and build profiles were not accessible
from this environment and were not changed.

## Minimal changes

1. Collapse working object schemas to v0 while preserving the effective set and
   order of current fields, native `dnv(base_, ...)` class layers, ClassId strings,
   references and journal/publication behavior.
2. Remove historical rejection and migration overloads. Normalize the six
   ordinary test models that acquired development-only bumps. Preserve the
   isolated `native_class_layers_reader_v2` evolution fixture.
3. Keep useful serialization, persistence, journal, graph and UI checks; replace
   the obsolete historical-number test with rejection of an unsupported schema
   in an otherwise valid complete graph.
4. Add isolated cross-process application/state transitions, with separate EXE
   revisions and class-schema revisions. Do not modify the native serializer.

## Counts at the baseline

| Area | Registered declarations | Already current v0 | Current > 0 |
| --- | ---: | ---: | ---: |
| Library (`include/`) | 81 | 72 | 9 |
| Examples | 84 | 71 | 13 |
| Tests | 71 | 64 | 7 |

The count is of declarations, including test classes declared separately in
writer/reader processes. Of the seven nonzero test declarations, only
`LayerBase` in `native_class_layers_reader_v2.cpp` is an intentional evolution
fixture; the other six are ordinary models to reset.

## Cases that cannot be changed by replacing the largest number

The pinned aether-objects version iterator invokes all detected contiguous
Save/Load overloads, rather than limiting them to the number in the object macro.
Consequently the effective field set can span multiple stored versions.

| Type | Required v0 field preservation |
| --- | --- |
| `UiBitmapImage` | Keep the former v0 body, including `bgra_bytes`; the former v1 body alone drops pixels. |
| `UiListContainerPresenter` | Keep the v1 body as v0, including `notify_list_selection_change`, although its macro already said 0. |
| `Surfaces` | Keep the current body with `desktop_z_order`, independently of `mobile_current`; remove legacy clearing. |
| `ChatFeedItem` | Keep timestamp and source Event ObjId from the current body. |
| `ChatMessageEvent` | Keep timestamp from the current body. |

`APPTRAVERSE_OBJECT`/`APPTRAVERSE_NAMED_OBJECT` calculate ClassId from the class
name string independently of the schema version. No class name or base changes
are required. Reflectable value structs without their own registered class
schema remain unversioned. Transport envelope versions and JNI/CMake versions
are outside this reset.

## Compatibility limits

This is a new development baseline, not a migration. Old files are not deleted
or automatically rewritten. A historic v0 key is not a universal format marker;
reusing v0 cannot reliably distinguish every old experiment. Use fresh state
directories.

A native Save of a newer schema leaves the older version slot in storage.
An old native reader may therefore accept a stale layer. The evolution fixture
must make its admission policy explicit, and a separate native diagnostic must
report lost/stale values as failures rather than claiming safe rollback.

Checkpoint import and direct DirectoryDomainStorage loading are different paths;
safe refusal in the latter does not establish transactionality of checkpoint
import.

## Complete baseline inventory

`Load`/`Save` columns list explicit overloads or the automatic reflection path.
All working declarations below are reset to v0; their baseline numbers are
retained here for auditability. Tests appear in a separate table.

### Library

| Type | Base | Baseline current | Load | Save | File |
| --- | --- | ---: | --- | --- | --- |
| `Event` | `ae::Obj` | 0 | reflection | reflection | `include/apptraverse/event.h` |
| `Link` | `Node` | 0 | 0 | 0 | `include/apptraverse/link.h` |
| `MemoryLink` | `Link` | 2 | 0,1,2 | 2 | `include/apptraverse/link.h` |
| `Node` | `ae::Obj` | 4 | 0,1,2,3,4 | 4 | `include/apptraverse/node.h` |
| `Win32SurfaceWindowPresenter` | `SurfaceWindowPresenter` | 0 | reflection | reflection | `include/apptraverse/platform/windows/surfaces_win_presenters.h` |
| `Win32SurfaceButtonPresenter` | `SurfaceButtonPresenter` | 0 | reflection | reflection | `include/apptraverse/platform/windows/surfaces_win_presenters.h` |
| `Win32SurfaceLanguageButtonPresenter` | `SurfaceLanguageButtonPresenter` | 0 | reflection | reflection | `include/apptraverse/platform/windows/surfaces_win_presenters.h` |
| `WinWindowPresenter` | `UiWindowPresenter` | 0 | reflection | reflection | `include/apptraverse/platform/windows/win_ui_presenters.h` |
| `WinDesktopWindowPresenter` | `UiDesktopWindowPresenter` | 0 | 0 | 0 | `include/apptraverse/platform/windows/win_ui_presenters.h` |
| `WinEditBoxPresenter` | `UiEditBoxPresenter` | 0 | reflection | reflection | `include/apptraverse/platform/windows/win_ui_presenters.h` |
| `WinPushButtonPresenter` | `UiPushButtonPresenter` | 0 | reflection | reflection | `include/apptraverse/platform/windows/win_ui_presenters.h` |
| `WinListContainerPresenter` | `UiListContainerPresenter` | 0 | reflection | reflection | `include/apptraverse/platform/windows/win_ui_presenters.h` |
| `WinLabelPresenter` | `UiLabelPresenter` | 0 | reflection | reflection | `include/apptraverse/platform/windows/win_ui_presenters.h` |
| `WinBitmapImagePresenter` | `UiBitmapImagePresenter` | 0 | reflection | reflection | `include/apptraverse/platform/windows/win_ui_presenters.h` |
| `Presenter` | `ae::Obj` | 0 | reflection | reflection | `include/apptraverse/presenter.h` |
| `ApplicationRuntimeState` | `Node` | 2 | 0,1,2 | 2 | `include/apptraverse/runtime_lifecycle.h` |
| `NetworkState` | `Node` | 2 | 0,1,2 | 2 | `include/apptraverse/runtime_lifecycle.h` |
| `AetherRegistrationState` | `Node` | 2 | 0,1,2 | 2 | `include/apptraverse/runtime_lifecycle.h` |
| `ApplicationStartedEvent` | `Event` | 0 | reflection | reflection | `include/apptraverse/runtime_lifecycle.h` |
| `NetworkInitializingEvent` | `Event` | 0 | reflection | reflection | `include/apptraverse/runtime_lifecycle.h` |
| `NetworkInterfaceUnavailableEvent` | `Event` | 0 | reflection | reflection | `include/apptraverse/runtime_lifecycle.h` |
| `InternetUnavailableEvent` | `Event` | 0 | reflection | reflection | `include/apptraverse/runtime_lifecycle.h` |
| `NetworkAvailableEvent` | `Event` | 0 | reflection | reflection | `include/apptraverse/runtime_lifecycle.h` |
| `AetherRegistrationStartedEvent` | `Event` | 0 | reflection | reflection | `include/apptraverse/runtime_lifecycle.h` |
| `AetherRegistrationCompletedEvent` | `Event` | 0 | reflection | reflection | `include/apptraverse/runtime_lifecycle.h` |
| `LinkSyncState` | `Node` | 3 | 0,1,2,3 | 3 | `include/apptraverse/shared_node.h` |
| `SetLinkInitialSyncPhaseEvent` | `Event` | 0 | reflection | reflection | `include/apptraverse/shared_node.h` |
| `BeginInitialSyncEvent` | `Event` | 0 | reflection | reflection | `include/apptraverse/shared_node.h` |
| `CompleteInitialSyncEvent` | `Event` | 0 | reflection | reflection | `include/apptraverse/shared_node.h` |
| `NoteInitialSyncReceivedEvent` | `Event` | 0 | reflection | reflection | `include/apptraverse/shared_node.h` |
| `CompleteFromReceivedSnapshotEvent` | `Event` | 0 | 0 | 0 | `include/apptraverse/shared_node.h` |
| `BeginIncrementalEventSyncEvent` | `Event` | 0 | reflection | reflection | `include/apptraverse/shared_node.h` |
| `CompleteIncrementalEventSyncEvent` | `Event` | 0 | reflection | reflection | `include/apptraverse/shared_node.h` |
| `SharedNode` | `Node` | 2 | 0,1,2 | 2 | `include/apptraverse/shared_node.h` |
| `AddShareEvent` | `Event` | 0 | reflection | reflection | `include/apptraverse/shared_node.h` |
| `RemoveShareEvent` | `Event` | 0 | reflection | reflection | `include/apptraverse/shared_node.h` |
| `ChangeShareAccessEvent` | `Event` | 0 | reflection | reflection | `include/apptraverse/shared_node.h` |
| `DesktopSurfacePresenter` | `SurfacePresenter` | 0 | 0 | 0 | `include/apptraverse/surfaces/desktop_surface_presenter.h` |
| `SurfaceButtonPresenter` | `Presenter` | 0 | 0 | 0 | `include/apptraverse/surfaces/surface_button_presenter.h` |
| `SurfaceLanguageButtonPresenter` | `Presenter` | 0 | 0 | 0 | `include/apptraverse/surfaces/surface_language_button_presenter.h` |
| `SurfaceWindowPresenter` | `DesktopSurfacePresenter` | 0 | 0 | 0 | `include/apptraverse/surfaces/surface_window_presenter.h` |
| `Surface` | `Node` | 0 | 0 | 0 | `include/apptraverse/surfaces/surfaces_model.h` |
| `SurfacePresenter` | `Presenter` | 0 | 0 | 0 | `include/apptraverse/surfaces/surfaces_model.h` |
| `Surfaces` | `Node` | 1 | 0,1 | 1 | `include/apptraverse/surfaces/surfaces_model.h` |
| `AddSurfaceEvent` | `Event` | 0 | 0 | 0 | `include/apptraverse/surfaces/surfaces_model.h` |
| `RemoveSurfaceEvent` | `Event` | 0 | 0 | 0 | `include/apptraverse/surfaces/surfaces_model.h` |
| `SetCurrentSurfaceEvent` | `Event` | 0 | 0 | 0 | `include/apptraverse/surfaces/surfaces_model.h` |
| `SetDesktopZOrderEvent` | `Event` | 0 | 0 | 0 | `include/apptraverse/surfaces/surfaces_model.h` |
| `SurfaceBoundsChangedEvent` | `Event` | 0 | 0 | 0 | `include/apptraverse/surfaces/surfaces_model.h` |
| `SurfacePresentationSizeChangedEvent` | `Event` | 0 | 0 | 0 | `include/apptraverse/surfaces/surfaces_model.h` |
| `Application` | `ae::Obj` | 0 | 0 | 0 | `include/apptraverse/surfaces/surfaces_model.h` |
| `UiBitmapImage` | `Node` | 1 | 0,1 | 0,1 | `include/apptraverse/ui/ui_bitmap_image.h` |
| `UiBitmapImagePresenter` | `Presenter` | 0 | 0 | 0 | `include/apptraverse/ui/ui_bitmap_image.h` |
| `UiDesktopWindow` | `UiWindow` | 0 | 0 | 0 | `include/apptraverse/ui/ui_desktop_window.h` |
| `SetUiDesktopWindowFrameEvent` | `Event` | 0 | reflection | reflection | `include/apptraverse/ui/ui_desktop_window.h` |
| `ReportUiDesktopWindowFrameFromNativeEvent` | `Event` | 0 | reflection | reflection | `include/apptraverse/ui/ui_desktop_window.h` |
| `RequestUiDesktopWindowFrameEvent` | `Event` | 0 | reflection | reflection | `include/apptraverse/ui/ui_desktop_window.h` |
| `UiEditBox` | `Node` | 0 | 0 | 0 | `include/apptraverse/ui/ui_edit_box.h` |
| `SetUiEditBoxTextEvent` | `Event` | 0 | reflection | reflection | `include/apptraverse/ui/ui_edit_box.h` |
| `SetUiEditBoxCaretEvent` | `Event` | 0 | reflection | reflection | `include/apptraverse/ui/ui_edit_box.h` |
| `UiLabel` | `Node` | 0 | 0 | 0 | `include/apptraverse/ui/ui_label.h` |
| `UiListRow` | `Node` | 0 | 0 | 0 | `include/apptraverse/ui/ui_list_container.h` |
| `UiListContainer` | `Node` | 0 | 0 | 0 | `include/apptraverse/ui/ui_list_container.h` |
| `RemoveUiListRowEvent` | `Event` | 0 | reflection | reflection | `include/apptraverse/ui/ui_list_container.h` |
| `UiListRowPresenter` | `Presenter` | 0 | 0 | 0 | `include/apptraverse/ui/ui_list_container.h` |
| `UiLocaleSettings` | `Node` | 0 | 0 | 0 | `include/apptraverse/ui/ui_locale_settings.h` |
| `SetUiLanguageIndexEvent` | `Event` | 0 | reflection | reflection | `include/apptraverse/ui/ui_locale_settings.h` |
| `UiLocalizationString` | `Node` | 0 | 0 | 0 | `include/apptraverse/ui/ui_localization_string.h` |
| `SetUiLocalizationSlotUtf8Event` | `Event` | 0 | reflection | reflection | `include/apptraverse/ui/ui_localization_string.h` |
| `UiWindowPresenter` | `Presenter` | 0 | 0 | 0 | `include/apptraverse/ui/ui_presenters.h` |
| `UiDesktopWindowPresenter` | `UiWindowPresenter` | 0 | 0 | 0 | `include/apptraverse/ui/ui_presenters.h` |
| `UiEditBoxPresenter` | `Presenter` | 0 | 0 | 0 | `include/apptraverse/ui/ui_presenters.h` |
| `UiPushButtonPresenter` | `Presenter` | 0 | 0 | 0 | `include/apptraverse/ui/ui_presenters.h` |
| `UiListContainerPresenter` | `Presenter` | 0 | 0,1 | 0,1 | `include/apptraverse/ui/ui_presenters.h` |
| `UiLabelPresenter` | `Presenter` | 0 | 0 | 0 | `include/apptraverse/ui/ui_presenters.h` |
| `UiPushButton` | `Node` | 0 | 0 | 0 | `include/apptraverse/ui/ui_push_button.h` |
| `SetUiPushButtonEnabledEvent` | `Event` | 0 | reflection | reflection | `include/apptraverse/ui/ui_push_button.h` |
| `UiPushButtonClickedEvent` | `Event` | 0 | reflection | reflection | `include/apptraverse/ui/ui_push_button.h` |
| `UiString` | `ae::Obj` | 0 | 0 | 0 | `include/apptraverse/ui/ui_string.h` |
| `UiWindow` | `Node` | 0 | 0 | 0 | `include/apptraverse/ui/ui_window.h` |
| `SetUiWindowClientSizeEvent` | `Event` | 0 | reflection | reflection | `include/apptraverse/ui/ui_window.h` |

### Examples

| Type | Base | Baseline current | Load | Save | File |
| --- | --- | ---: | --- | --- | --- |
| `AetherLink` | `Link` | 0 | 0 | 0 | `examples/chat_demo/aether/aether_link.h` |
| `ChatEntryAddedEvent` | `Event` | 0 | 0 | 0 | `examples/chat_demo/common/chat_commands.h` |
| `ChatSelectedEvent` | `Event` | 0 | 0 | 0 | `examples/chat_demo/common/chat_commands.h` |
| `LocalEndpointBoundEvent` | `Event` | 0 | 0 | 0 | `examples/chat_demo/common/chat_commands.h` |
| `MessageSequenceReservedEvent` | `Event` | 0 | 0 | 0 | `examples/chat_demo/common/chat_commands.h` |
| `DesktopBoundsChangedEvent` | `Event` | 0 | 0 | 0 | `examples/chat_demo/common/chat_commands.h` |
| `DemoRoleConfiguredEvent` | `Event` | 0 | 0 | 0 | `examples/chat_demo/common/chat_commands.h` |
| `HostUidInputChangedEvent` | `Event` | 0 | 0 | 0 | `examples/chat_demo/common/chat_commands.h` |
| `ChatBindingChangedEvent` | `Event` | 0 | 0 | 0 | `examples/chat_demo/common/chat_commands.h` |
| `DraftChangedEvent` | `Event` | 0 | 0 | 0 | `examples/chat_demo/common/chat_commands.h` |
| `ScrollChangedEvent` | `Event` | 0 | 0 | 0 | `examples/chat_demo/common/chat_commands.h` |
| `MessageAddedEvent` | `Event` | 0 | 0 | 0 | `examples/chat_demo/common/chat_commands.h` |
| `ChatDemoRuntimeState` | `Node` | 0 | 0 | 0 | `examples/chat_demo/common/chat_demo_runtime_state.h` |
| `JoinHostRequestedEvent` | `Event` | 0 | 0 | 0 | `examples/chat_demo/common/chat_demo_runtime_state.h` |
| `JoinHostAcceptedEvent` | `Event` | 0 | 0 | 0 | `examples/chat_demo/common/chat_demo_runtime_state.h` |
| `JoinHostCompletedEvent` | `Event` | 0 | 0 | 0 | `examples/chat_demo/common/chat_demo_runtime_state.h` |
| `JoinHostFailedEvent` | `Event` | 0 | 0 | 0 | `examples/chat_demo/common/chat_demo_runtime_state.h` |
| `CopyHostUidRequestedEvent` | `Event` | 0 | 0 | 0 | `examples/chat_demo/common/chat_demo_runtime_state.h` |
| `CopyHostUidCompletedEvent` | `Event` | 0 | 0 | 0 | `examples/chat_demo/common/chat_demo_runtime_state.h` |
| `ChatRoom` | `SharedNode` | 0 | 0 | 0 | `examples/chat_demo/common/chat_model.h` |
| `ChatEntry` | `Node` | 1 | 0,1 | 1 | `examples/chat_demo/common/chat_model.h` |
| `ChatWorkspace` | `Node` | 1 | 0,1 | 1 | `examples/chat_demo/common/chat_model.h` |
| `ClientAddedEvent` | `Event` | 0 | reflection | reflection | `examples/chat_ui_runtime_demo/common/chat_events.h` |
| `ChatMessageEvent` | `Event` | 1 | 0,1 | 0,1 | `examples/chat_ui_runtime_demo/common/chat_events.h` |
| `PresenceChangedEvent` | `Event` | 0 | reflection | reflection | `examples/chat_ui_runtime_demo/common/chat_events.h` |
| `PresenceMonitoringStartedEvent` | `Event` | 0 | reflection | reflection | `examples/chat_ui_runtime_demo/common/chat_events.h` |
| `ImmutableString` | `ae::Obj` | 0 | reflection | reflection | `examples/chat_ui_runtime_demo/common/chat_model.h` |
| `ChatClient` | `Node` | 2 | 0,1,2 | 2 | `examples/chat_ui_runtime_demo/common/chat_model.h` |
| `ChatFeedItem` | `ae::Obj` | 1 | 0,1 | 0,1 | `examples/chat_ui_runtime_demo/common/chat_model.h` |
| `ChatRoom` | `Node` | 2 | 0,1,2 | 2 | `examples/chat_ui_runtime_demo/common/chat_model.h` |
| `ChatApplication` | `ae::Obj` | 0 | reflection | reflection | `examples/chat_ui_runtime_demo/common/chat_model.h` |
| `WinChatWindowPresenter` | `Presenter` | 0 | reflection | reflection | `examples/chat_ui_runtime_demo/windows/win_presenters.h` |
| `WinChatPresentationApplication` | `Presenter` | 0 | reflection | reflection | `examples/chat_ui_runtime_demo/windows/win_presenters.h` |
| `Item` | `ae::Obj` | 0 | 0 | 0 | `examples/dynamic_objects_demo/common/dynamic_model.h` |
| `ItemPresenter` | `Presenter` | 0 | 0 | 0 | `examples/dynamic_objects_demo/common/dynamic_model.h` |
| `ItemList` | `Node` | 2 | 0,1,2 | 2 | `examples/dynamic_objects_demo/common/dynamic_model.h` |
| `ItemListPresenter` | `Presenter` | 0 | 0 | 0 | `examples/dynamic_objects_demo/common/dynamic_model.h` |
| `AddItem` | `ae::Obj` | 0 | 0 | 0 | `examples/dynamic_objects_demo/common/dynamic_model.h` |
| `AddItemPresenter` | `Presenter` | 0 | 0 | 0 | `examples/dynamic_objects_demo/common/dynamic_model.h` |
| `AddItemEvent` | `Event` | 0 | 0 | 0 | `examples/dynamic_objects_demo/common/dynamic_model.h` |
| `RemoveItemEvent` | `Event` | 0 | 0 | 0 | `examples/dynamic_objects_demo/common/dynamic_model.h` |
| `MainWindow` | `Node` | 2 | 0,1,2 | 2 | `examples/dynamic_objects_demo/common/dynamic_model.h` |
| `MainWindowPresenter` | `Presenter` | 0 | 0 | 0 | `examples/dynamic_objects_demo/common/dynamic_model.h` |
| `Application` | `ae::Obj` | 0 | 0 | 0 | `examples/dynamic_objects_demo/common/dynamic_model.h` |
| `Win32MainWindowPresenter` | `MainWindowPresenter` | 0 | reflection | reflection | `examples/dynamic_objects_demo/windows/win_presenters.h` |
| `Win32AddItemPresenter` | `AddItemPresenter` | 0 | reflection | reflection | `examples/dynamic_objects_demo/windows/win_presenters.h` |
| `Win32ItemListPresenter` | `ItemListPresenter` | 0 | reflection | reflection | `examples/dynamic_objects_demo/windows/win_presenters.h` |
| `Win32ItemPresenter` | `ItemPresenter` | 0 | reflection | reflection | `examples/dynamic_objects_demo/windows/win_presenters.h` |
| `MainWindow` | `Node` | 5 | 0,1,2,3,4,5 | 5 | `examples/main_window_runtime_demo/common/main_window_model.h` |
| `MainWindowPresenter` | `Presenter` | 0 | 0 | 0 | `examples/main_window_runtime_demo/common/main_window_model.h` |
| `WindowChangedEvent` | `Event` | 0 | reflection | reflection | `examples/main_window_runtime_demo/common/main_window_model.h` |
| `Application` | `ae::Obj` | 0 | reflection | reflection | `examples/main_window_runtime_demo/common/main_window_model.h` |
| `Win32MainWindowPresenter` | `MainWindowPresenter` | 0 | reflection | reflection | `examples/main_window_runtime_demo/windows/win_presenters.h` |
| `WindowBoundsChangedEvent` | `Event` | 0 | reflection | reflection | `examples/model_ui_runtime_demo/common/demo_events.h` |
| `ColorChangedEvent` | `Event` | 0 | reflection | reflection | `examples/model_ui_runtime_demo/common/demo_events.h` |
| `TextReplacedEvent` | `Event` | 0 | reflection | reflection | `examples/model_ui_runtime_demo/common/demo_events.h` |
| `CenterStripAddedEvent` | `Event` | 0 | reflection | reflection | `examples/model_ui_runtime_demo/common/demo_events.h` |
| `CenterStripRemovedEvent` | `Event` | 0 | reflection | reflection | `examples/model_ui_runtime_demo/common/demo_events.h` |
| `ImmutableString` | `ae::Obj` | 0 | reflection | reflection | `examples/model_ui_runtime_demo/common/demo_model.h` |
| `TextToolbar` | `Node` | 0 | 0 | 0 | `examples/model_ui_runtime_demo/common/demo_model.h` |
| `ColorToolbar` | `Node` | 0 | 0 | 0 | `examples/model_ui_runtime_demo/common/demo_model.h` |
| `CenterStrip` | `Node` | 0 | 0 | 0 | `examples/model_ui_runtime_demo/common/demo_model.h` |
| `Window` | `Node` | 0 | 0 | 0 | `examples/model_ui_runtime_demo/common/demo_model.h` |
| `PaintWindow` | `Window` | 0 | 0 | 0 | `examples/model_ui_runtime_demo/common/demo_model.h` |
| `LayoutWindow` | `Window` | 0 | 0 | 0 | `examples/model_ui_runtime_demo/common/demo_model.h` |
| `Application` | `ae::Obj` | 0 | reflection | reflection | `examples/model_ui_runtime_demo/common/demo_model.h` |
| `WinTextToolbarPresenter` | `Presenter` | 0 | reflection | reflection | `examples/model_ui_runtime_demo/windows/win_presenters.h` |
| `WinColorToolbarPresenter` | `Presenter` | 0 | reflection | reflection | `examples/model_ui_runtime_demo/windows/win_presenters.h` |
| `WinPaintWindowPresenter` | `Presenter` | 0 | reflection | reflection | `examples/model_ui_runtime_demo/windows/win_presenters.h` |
| `WinLayoutWindowPresenter` | `Presenter` | 0 | reflection | reflection | `examples/model_ui_runtime_demo/windows/win_presenters.h` |
| `WinPresentationApplication` | `Presenter` | 0 | reflection | reflection | `examples/model_ui_runtime_demo/windows/win_presenters.h` |
| `SharedValueNode` | `SharedNode` | 2 | 0,1,2 | 2 | `examples/shared_node_demo/shared_node_demo_model.h` |
| `SetValueEvent` | `Event` | 0 | reflection | reflection | `examples/shared_node_demo/shared_node_demo_model.h` |
| `Client` | `Node` | 2 | 0,1,2 | 2 | `examples/shared_node_demo/shared_node_demo_model.h` |
| `ChildSharedNode` | `SharedNode` | 2 | 0,1,2 | 2 | `examples/shared_node_demo/shared_node_demo_model.h` |
| `RootSharedNode` | `SharedNode` | 2 | 0,1,2 | 2 | `examples/shared_node_demo/shared_node_demo_model.h` |
| `LocalOnlyPayload` | `ae::Obj` | 0 | reflection | reflection | `examples/shared_node_demo/shared_node_demo_model.h` |
| `LocalPtrHolder` | `ae::Obj` | 0 | reflection | reflection | `examples/shared_node_demo/shared_node_demo_model.h` |
| `AndroidSurfacePresenter` | `MobileSurfacePresenter` | 0 | reflection | reflection | `examples/surfaces_demo/android/native/android_surface_presenter.h` |
| `IOSSurfacePresenter` | `MobileSurfacePresenter` | 0 | 0 | 0 | `examples/surfaces_demo/ios/ios_presenters.h` |
| `LinuxSurfacePresenter` | `DesktopSurfacePresenter` | 0 | reflection | reflection | `examples/surfaces_demo/linux/linux_presenters.h` |
| `MacSurfacePresenter` | `DesktopSurfacePresenter` | 0 | reflection | reflection | `examples/surfaces_demo/macos/mac_presenters.h` |
| `MobileSurfacePresenter` | `SurfacePresenter` | 0 | 0 | 0 | `examples/surfaces_demo/mobile/mobile_surface_presenter.h` |
| `WebSurfacePresenter` | `SurfacePresenter` | 0 | 0 | 0 | `examples/surfaces_demo/web/web_surface_presenter.h` |

### Tests

| Type | Base | Baseline current | Load | Save | File |
| --- | --- | ---: | --- | --- | --- |
| `CycleApp` | `Node` | 0 | 0 | 0 | `tests/checkpoint_load_cycle_persistence_test.cpp` |
| `CycleWindow` | `Node` | 0 | 0 | 0 | `tests/checkpoint_load_cycle_persistence_test.cpp` |
| `CyclePresenter` | `Presenter` | 0 | 0 | 0 | `tests/checkpoint_load_cycle_persistence_test.cpp` |
| `RehydrationCycleApp` | `Node` | 0 | 0 | 0 | `tests/checkpoint_load_rehydration_test.cpp` |
| `RehydrationCycleWindow` | `Node` | 0 | 0 | 0 | `tests/checkpoint_load_rehydration_test.cpp` |
| `RehydrationCyclePresenter` | `Presenter` | 0 | 0 | 0 | `tests/checkpoint_load_rehydration_test.cpp` |
| `Metadata` | `ae::Obj` | 0 | 0 | 0 | `tests/closed_event_graph_test.cpp` |
| `Detail` | `ae::Obj` | 0 | 0 | 0 | `tests/closed_event_graph_test.cpp` |
| `Item` | `Node` | 1 | 0,1,0 | 1,0 | `tests/closed_event_graph_test.cpp` |
| `BaseObjWithRef` | `ae::Obj` | 0 | 0 | 0 | `tests/closed_event_graph_test.cpp` |
| `DerivedObjWithRef` | `BaseObjWithRef` | 0 | 0 | 0 | `tests/closed_event_graph_test.cpp` |
| `AddItemEvent` | `Event` | 0 | 0 | 0 | `tests/closed_event_graph_test.cpp` |
| `ContainerNode` | `Node` | 1 | 0,1 | 1 | `tests/closed_event_graph_test.cpp` |
| `ExternalObject` | `ae::Obj` | 0 | 0 | 0 | `tests/closed_event_graph_test.cpp` |
| `EventWithExternalRef` | `Event` | 0 | 0 | 0 | `tests/closed_event_graph_test.cpp` |
| `SentinelObject` | `ae::Obj` | 0 | 0 | 0 | `tests/closed_event_graph_test.cpp` |
| `TestMainWindowPresenter` | `MainWindowPresenter` | 0 | reflection | reflection | `tests/dynamic_objects_add_test.cpp` |
| `TestAddItemPresenter` | `AddItemPresenter` | 0 | reflection | reflection | `tests/dynamic_objects_add_test.cpp` |
| `TestItemListPresenter` | `ItemListPresenter` | 0 | reflection | reflection | `tests/dynamic_objects_add_test.cpp` |
| `TestItemPresenter` | `ItemPresenter` | 0 | reflection | reflection | `tests/dynamic_objects_add_test.cpp` |
| `CounterDocument` | `Node` | 2 | 0,1,2 | 2 | `tests/event_sourced_core_test.cpp` |
| `AddEvent` | `Event` | 0 | reflection | reflection | `tests/event_sourced_core_test.cpp` |
| `RetentionDoc` | `Node` | 3 | 0,1,2,3 | 3 | `tests/journal_retention_test.cpp` |
| `AddEvent` | `Event` | 0 | reflection | reflection | `tests/journal_retention_test.cpp` |
| `TestMainWindowPresenter` | `MainWindowPresenter` | 0 | reflection | reflection | `tests/main_window_lifecycle_load_only_test.cpp` |
| `TestMainWindowPresenter` | `MainWindowPresenter` | 0 | reflection | reflection | `tests/main_window_lifecycle_test.cpp` |
| `TestMainWindowPresenter` | `MainWindowPresenter` | 0 | reflection | reflection | `tests/main_window_window_changed_test.cpp` |
| `PersistentDoc` | `Node` | 0 | 0 | 0 | `tests/model_persistence_runtime_ref_test.cpp` |
| `PersistentDocChangedEvent` | `Event` | 0 | reflection | reflection | `tests/model_persistence_runtime_ref_test.cpp` |
| `DocChildNode` | `Node` | 0 | 0 | 0 | `tests/model_persistence_test.cpp` |
| `DocNode` | `Node` | 0 | 0 | 0 | `tests/model_persistence_test.cpp` |
| `DocChangedEvent` | `Event` | 0 | reflection | reflection | `tests/model_persistence_test.cpp` |
| `Counter` | `Node` | 1 | 0,1 | 1 | `tests/model_runtime_stop_test.cpp` |
| `BumpEvent` | `Event` | 0 | reflection | reflection | `tests/model_runtime_stop_test.cpp` |
| `RuntimeRoot` | `ae::Obj` | 0 | 0 | 0 | `tests/model_runtime_stop_test.cpp` |
| `LayerBase` | `ae::Obj` | 1 | 0,1 | 1 | `tests/native_class_layers_reader_v2.cpp` |
| `LayerDerived` | `LayerBase` | 0 | 0 | 0 | `tests/native_class_layers_reader_v2.cpp` |
| `LayerBase` | `ae::Obj` | 0 | 0 | 0 | `tests/native_class_layers_test.cpp` |
| `LayerDerived` | `LayerBase` | 0 | 0 | 0 | `tests/native_class_layers_test.cpp` |
| `BrokenBase` | `ae::Obj` | 0 | 0 | 0 | `tests/native_class_layers_test.cpp` |
| `BrokenDerived` | `BrokenBase` | 0 | 0 | 0 | `tests/native_class_layers_test.cpp` |
| `LayerBase` | `ae::Obj` | 0 | 0 | 0 | `tests/native_class_layers_writer_v1.cpp` |
| `LayerDerived` | `LayerBase` | 0 | 0 | 0 | `tests/native_class_layers_writer_v1.cpp` |
| `NativeSentinel` | `ae::Obj` | 0 | 0 | 0 | `tests/native_object_graph_serialization_test.cpp` |
| `NativeTestDetail` | `ae::Obj` | 0 | 0 | 0 | `tests/native_object_graph_serialization_test.cpp` |
| `NativeTestMetadata` | `ae::Obj` | 0 | 0,0 | 0,0 | `tests/native_object_graph_serialization_test.cpp` |
| `NativeTestItem` | `Node` | 0 | 0 | 0 | `tests/native_object_graph_serialization_test.cpp` |
| `NativeTestEvent` | `Event` | 0 | 0 | 0 | `tests/native_object_graph_serialization_test.cpp` |
| `PresenterGraphRoot` | `Node` | 0 | 0 | 0 | `tests/presenter_graph_persistence_test.cpp` |
| `OrderRootPresenter` | `Presenter` | 0 | 0 | 0 | `tests/presenter_load_order_test.cpp` |
| `OrderChildPresenter` | `Presenter` | 0 | 0 | 0 | `tests/presenter_load_order_test.cpp` |
| `OrderChild` | `ae::Obj` | 0 | 0 | 0 | `tests/presenter_load_order_test.cpp` |
| `OrderRoot` | `ae::Obj` | 0 | 0 | 0 | `tests/presenter_load_order_test.cpp` |
| `PlatformPersistRoot` | `Node` | 0 | 0 | 0 | `tests/presenter_platform_persist_shared.h` |
| `SaveBackendDesktopWindowPresenter` | `ui::UiDesktopWindowPresenter` | 0 | reflection | reflection | `tests/presenter_platform_persist_shared.h` |
| `LoadBackendDesktopWindowPresenter` | `ui::UiDesktopWindowPresenter` | 0 | reflection | reflection | `tests/presenter_platform_persist_shared.h` |
| `FramePubRoot` | `Node` | 0 | 0 | 0 | `tests/presenter_win_desktop_frame_publication_e2e_test.cpp` |
| `FrameSyncRoot` | `Node` | 0 | 0 | 0 | `tests/presenter_win_desktop_frame_sync_test.cpp` |
| `DragRoot` | `Node` | 0 | 0 | 0 | `tests/presenter_win_desktop_interactive_drag_e2e_test.cpp` |
| `WinPersistRoot` | `Node` | 0 | 0 | 0 | `tests/presenter_win_hwnd_persistence_test.cpp` |
| `NotePayload` | `ae::Obj` | 0 | reflection | reflection | `tests/shared_node_incremental_event_test.cpp` |
| `NoteTargetNode` | `Node` | 0 | 0 | 0 | `tests/shared_node_incremental_event_test.cpp` |
| `SetValueWithNoteEvent` | `Event` | 0 | reflection | reflection | `tests/shared_node_incremental_event_test.cpp` |
| `StateDependentNode` | `SharedNode` | 2 | 1,2 | 2 | `tests/shared_node_incremental_event_test.cpp` |
| `TransitionEvent` | `Event` | 0 | reflection | reflection | `tests/shared_node_incremental_event_test.cpp` |
| `TestSurfaceWindowPresenter` | `SurfaceWindowPresenter` | 0 | reflection | reflection | `tests/surfaces_test_presentation.cpp` |
| `TestSurfaceButtonPresenter` | `SurfaceButtonPresenter` | 0 | reflection | reflection | `tests/surfaces_test_presentation.cpp` |
| `TestSurfaceLanguageButtonPresenter` | `SurfaceLanguageButtonPresenter` | 0 | reflection | reflection | `tests/surfaces_test_presentation.cpp` |
| `TestClickButton` | `UiPushButton` | 0 | reflection | reflection | `tests/ui/ui_control_materialization_test.cpp` |
| `BitmapPublicationRoot` | `Node` | 0 | 0 | 0 | `tests/ui_bitmap_publication_test.cpp` |
| `BitmapPublicationRoot` | `Node` | 0 | 0 | 0 | `tests/ui_bitmap_structural_publication_test.cpp` |
